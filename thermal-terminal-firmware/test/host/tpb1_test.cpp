#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "app_core/app_event.hpp"
#include "app_core/app_state.hpp"
#include "bitmap_store/bitmap_store.hpp"
#include "interfaces/terminal_client.hpp"
#include "settings/user_settings.hpp"
#include "tpb1/crc32.hpp"
#include "tpb1/packbits.hpp"
#include "tpb1/parser.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": check failed: " << #condition << '\n';                   \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

void write_u16(std::vector<std::uint8_t> &data, std::size_t offset,
               std::uint16_t value) {
  data[offset] = static_cast<std::uint8_t>(value);
  data[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::vector<std::uint8_t> &data, std::size_t offset,
               std::uint32_t value) {
  data[offset] = static_cast<std::uint8_t>(value);
  data[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
  data[offset + 2] = static_cast<std::uint8_t>(value >> 16U);
  data[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

void append_u16(std::vector<std::uint8_t> &data, std::uint16_t value) {
  data.push_back(static_cast<std::uint8_t>(value));
  data.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t> &data, std::uint32_t value) {
  data.push_back(static_cast<std::uint8_t>(value));
  data.push_back(static_cast<std::uint8_t>(value >> 8U));
  data.push_back(static_cast<std::uint8_t>(value >> 16U));
  data.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void append_magic(std::vector<std::uint8_t> &data, const char *magic) {
  data.insert(data.end(), magic, magic + 4);
}

std::vector<std::uint8_t> encode_literals(const std::uint8_t *data,
                                          std::size_t size) {
  std::vector<std::uint8_t> encoded;
  std::size_t position = 0;
  while (position < size) {
    const std::size_t count = std::min<std::size_t>(128, size - position);
    encoded.push_back(static_cast<std::uint8_t>(count - 1));
    encoded.insert(encoded.end(), data + position, data + position + count);
    position += count;
  }
  return encoded;
}

std::vector<std::uint8_t> make_document() {
  constexpr std::uint32_t height = 3;
  constexpr std::uint16_t band_height = 2;
  constexpr std::uint16_t band_count = 2;
  std::vector<std::uint8_t> bitmap(thermal_terminal::tpb1::kRowBytes * height);
  for (std::size_t index = 0; index < bitmap.size(); ++index) {
    bitmap[index] = static_cast<std::uint8_t>((index * 37U) & 0xFFU);
  }

  std::vector<std::uint8_t> document(32, 0);
  std::memcpy(document.data(), "TPB1", 4);
  document[4] = 1;
  document[5] = 0x07;
  document[6] = 1;
  document[7] = 1;
  write_u16(document, 8, 32);
  write_u16(document, 10, thermal_terminal::tpb1::kRequiredWidth);
  write_u32(document, 12, height);
  write_u16(document, 16, band_height);
  write_u16(document, 18, band_count);
  write_u32(document, 20, static_cast<std::uint32_t>(bitmap.size()));
  write_u32(document, 24, 0x12345678U);
  write_u32(document, 28, thermal_terminal::tpb1::crc32(document.data(), 28));

  for (std::uint16_t band = 0; band < band_count; ++band) {
    const std::uint32_t y = band * band_height;
    const std::uint16_t rows = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(band_height, height - y));
    const std::size_t raw_size = rows * thermal_terminal::tpb1::kRowBytes;
    const auto *raw = bitmap.data() + y * thermal_terminal::tpb1::kRowBytes;
    const auto payload = encode_literals(raw, raw_size);

    append_magic(document, "BAND");
    append_u16(document, band);
    append_u16(document, rows);
    append_u32(document, y);
    append_u32(document, static_cast<std::uint32_t>(raw_size));
    append_u32(document, static_cast<std::uint32_t>(payload.size()));
    append_u32(document, thermal_terminal::tpb1::crc32(raw, raw_size));
    document.insert(document.end(), payload.begin(), payload.end());
  }

  append_magic(document, "END!");
  append_u16(document, band_count);
  append_u16(document, 0);
  append_u32(document, static_cast<std::uint32_t>(bitmap.size()));
  append_u32(document,
             thermal_terminal::tpb1::crc32(bitmap.data(), bitmap.size()));
  return document;
}

class MemorySink final : public thermal_terminal::tpb1::Sink {
public:
  bool
  begin_document(const thermal_terminal::tpb1::DocumentInfo &info) override {
    began = true;
    bytes.assign(info.raw_length, 0);
    return accept;
  }

  bool write_band(const thermal_terminal::tpb1::BandInfo &info,
                  const std::uint8_t *data, std::size_t size) override {
    const std::size_t offset = info.y * thermal_terminal::tpb1::kRowBytes;
    if (offset + size > bytes.size())
      return false;
    std::copy(data, data + size, bytes.begin() + offset);
    ++bands;
    return accept;
  }

  bool end_document() override {
    ended = true;
    return accept;
  }

  bool accept{true};
  bool began{false};
  bool ended{false};
  std::uint16_t bands{};
  std::vector<std::uint8_t> bytes;
};

bool feed_in_network_chunks(thermal_terminal::tpb1::Parser &parser,
                            const std::vector<std::uint8_t> &document) {
  std::size_t position = 0;
  std::size_t chunk = 1;
  while (position < document.size()) {
    const std::size_t count = std::min(chunk, document.size() - position);
    if (!parser.feed(document.data() + position, count))
      return false;
    position += count;
    chunk = chunk == 17 ? 1 : chunk + 1;
  }
  return parser.finish();
}

void test_crc32() {
  const std::string input = "123456789";
  CHECK(thermal_terminal::tpb1::crc32(
            reinterpret_cast<const std::uint8_t *>(input.data()),
            input.size()) == 0xCBF43926U);
}

void test_packbits() {
  const std::uint8_t encoded[] = {254, 0xAA, 2, 1, 2, 3};
  std::vector<std::uint8_t> decoded;
  CHECK(thermal_terminal::tpb1::decode_packbits(encoded, sizeof(encoded), 6,
                                                decoded) ==
        thermal_terminal::tpb1::PackBitsError::kNone);
  CHECK(decoded == std::vector<std::uint8_t>({0xAA, 0xAA, 0xAA, 1, 2, 3}));

  const std::uint8_t reserved[] = {128};
  CHECK(thermal_terminal::tpb1::decode_packbits(reserved, sizeof(reserved), 0,
                                                decoded) ==
        thermal_terminal::tpb1::PackBitsError::kReservedControl);
}

void test_valid_document() {
  MemorySink sink;
  thermal_terminal::tpb1::Parser parser(sink);
  const auto document = make_document();
  CHECK(feed_in_network_chunks(parser, document));
  CHECK(parser.complete());
  CHECK(parser.document().width == 384);
  CHECK(parser.document().height == 3);
  CHECK(sink.began && sink.ended);
  CHECK(sink.bands == 2);
  CHECK(sink.bytes.size() == 144);
}

void test_bitmap_store_is_committed_only_after_end() {
  const auto document = make_document();
  thermal_terminal::BitmapStore store;
  thermal_terminal::tpb1::Parser parser(store);

  CHECK(parser.feed(document.data(), document.size() - 16));
  CHECK(!store.ready());
  CHECK(store.data() == nullptr);
  CHECK(parser.feed(document.data() + document.size() - 16, 16));
  CHECK(parser.finish());
  CHECK(store.ready());
  CHECK(store.size() == 144);
  CHECK(store.row(0) != nullptr);
  CHECK(store.row(3) == nullptr);

  auto bad_end = document;
  bad_end.back() ^= 1U;
  thermal_terminal::tpb1::Parser bad_parser(store);
  CHECK(!bad_parser.feed(bad_end.data(), bad_end.size()));
  CHECK(!store.ready());
  CHECK(store.data() == nullptr);
}

void test_corruption_and_truncation() {
  auto corrupted = make_document();
  corrupted[57] ^= 0x01U;
  MemorySink corrupt_sink;
  thermal_terminal::tpb1::Parser corrupt_parser(corrupt_sink);
  CHECK(!feed_in_network_chunks(corrupt_parser, corrupted));
  CHECK(corrupt_parser.error() ==
        thermal_terminal::tpb1::Error::kBandCrcMismatch);

  auto truncated = make_document();
  truncated.pop_back();
  MemorySink truncated_sink;
  thermal_terminal::tpb1::Parser truncated_parser(truncated_sink);
  CHECK(!feed_in_network_chunks(truncated_parser, truncated));
  CHECK(truncated_parser.error() ==
        thermal_terminal::tpb1::Error::kTruncatedDocument);
}

void test_header_and_bitmap_validation() {
  auto bad_header_crc = make_document();
  bad_header_crc[12] ^= 0x01U;
  MemorySink header_sink;
  thermal_terminal::tpb1::Parser header_parser(header_sink);
  CHECK(!feed_in_network_chunks(header_parser, bad_header_crc));
  CHECK(header_parser.error() ==
        thermal_terminal::tpb1::Error::kHeaderCrcMismatch);

  auto bad_width = make_document();
  write_u16(bad_width, 10, 376);
  write_u32(bad_width, 28, thermal_terminal::tpb1::crc32(bad_width.data(), 28));
  MemorySink width_sink;
  thermal_terminal::tpb1::Parser width_parser(width_sink);
  CHECK(!feed_in_network_chunks(width_parser, bad_width));
  CHECK(width_parser.error() ==
        thermal_terminal::tpb1::Error::kInvalidDimensions);

  auto bad_bitmap_crc = make_document();
  bad_bitmap_crc.back() ^= 0x01U;
  MemorySink bitmap_sink;
  thermal_terminal::tpb1::Parser bitmap_parser(bitmap_sink);
  CHECK(!feed_in_network_chunks(bitmap_parser, bad_bitmap_crc));
  CHECK(bitmap_parser.error() ==
        thermal_terminal::tpb1::Error::kBitmapCrcMismatch);
}

void test_trailing_data_and_sink_rejection() {
  const auto document = make_document();
  MemorySink sink;
  thermal_terminal::tpb1::Parser parser(sink);
  CHECK(parser.feed(document.data(), document.size()));
  const std::uint8_t extra = 0;
  CHECK(!parser.feed(&extra, 1));
  CHECK(parser.error() == thermal_terminal::tpb1::Error::kTrailingData);

  MemorySink rejecting_sink;
  rejecting_sink.accept = false;
  thermal_terminal::tpb1::Parser rejecting_parser(rejecting_sink);
  CHECK(!rejecting_parser.feed(document.data(), document.size()));
  CHECK(rejecting_parser.error() ==
        thermal_terminal::tpb1::Error::kSinkRejected);
}

void test_settings_and_events() {
  thermal_terminal::UserSettings settings;
  CHECK(!settings.printer.auto_print);
  CHECK(settings.use_context);
  settings.render.font_size = 255;
  settings.render.line_height = 0.1F;
  settings.render.band_height = 1;
  thermal_terminal::normalize_settings(settings);
  CHECK(settings.render.font_size == 30);
  CHECK(settings.render.line_height == 1.1F);
  CHECK(settings.render.band_height == 32);
  CHECK(std::string(thermal_terminal::app_state_name(
            thermal_terminal::AppState::kPreviewing)) == "previewing");
  CHECK(sizeof(thermal_terminal::AppEvent) <= 12);
  thermal_terminal::ClientResult result;
  CHECK(!result.ok && result.http_status == 0);
}

} // namespace

int main() {
  test_crc32();
  test_packbits();
  test_valid_document();
  test_bitmap_store_is_committed_only_after_end();
  test_corruption_and_truncation();
  test_header_and_bitmap_validation();
  test_trailing_data_and_sink_rejection();
  test_settings_and_events();

  if (failures != 0) {
    std::cerr << failures << " test check(s) failed\n";
    return 1;
  }
  std::cout << "all host checks passed\n";
  return 0;
}
