#include "tpb1/parser.hpp"

#include <algorithm>
#include <cstring>

#include "tpb1/packbits.hpp"

namespace thermal_terminal::tpb1 {
namespace {

constexpr std::size_t kFileHeaderSize = 32;
constexpr std::size_t kBandHeaderSize = 24;
constexpr std::size_t kEndRecordSize = 16;

std::uint16_t read_u16(const std::uint8_t *data) {
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t *data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) |
         (static_cast<std::uint32_t>(data[3]) << 24U);
}

bool has_magic(const std::uint8_t *data, const char *magic) {
  return std::memcmp(data, magic, 4) == 0;
}

} // namespace

Parser::Parser(Sink &sink) : sink_(sink) {}

void Parser::reset() {
  phase_ = Phase::kFileHeader;
  error_ = Error::kNone;
  error_message_.clear();
  document_ = {};
  current_band_ = {};
  current_band_crc_ = 0;
  current_payload_length_ = 0;
  next_band_index_ = 0;
  received_rows_ = 0;
  received_raw_length_ = 0;
  bitmap_crc_.reset();
  record_buffer_.fill(0);
  record_size_ = 0;
  payload_.clear();
  decoded_.clear();
}

bool Parser::feed(const std::uint8_t *data, std::size_t size) {
  if (size == 0)
    return phase_ != Phase::kError;
  if (data == nullptr)
    return fail(Error::kInvalidLength, "null input");
  if (phase_ == Phase::kComplete)
    return fail(Error::kTrailingData, "data after END record");
  if (phase_ == Phase::kError)
    return false;

  std::size_t position = 0;
  while (position < size && phase_ != Phase::kError) {
    if (phase_ == Phase::kBandPayload) {
      const std::size_t needed = current_payload_length_ - payload_.size();
      const std::size_t count = std::min(needed, size - position);
      payload_.insert(payload_.end(), data + position, data + position + count);
      position += count;
      if (payload_.size() == current_payload_length_ &&
          !consume_band_payload()) {
        return false;
      }
      continue;
    }

    std::size_t target = 0;
    switch (phase_) {
    case Phase::kFileHeader:
      target = kFileHeaderSize;
      break;
    case Phase::kRecordPrefix:
      target = 4;
      break;
    case Phase::kBandHeader:
      target = kBandHeaderSize;
      break;
    case Phase::kEndRecord:
      target = kEndRecordSize;
      break;
    case Phase::kComplete:
      return fail(Error::kTrailingData, "data after END record");
    case Phase::kError:
      return false;
    case Phase::kBandPayload:
      break;
    }

    const std::size_t count = std::min(target - record_size_, size - position);
    std::memcpy(record_buffer_.data() + record_size_, data + position, count);
    record_size_ += count;
    position += count;
    if (record_size_ != target)
      continue;

    bool ok = false;
    switch (phase_) {
    case Phase::kFileHeader:
      ok = consume_file_header();
      break;
    case Phase::kRecordPrefix:
      ok = consume_record_prefix();
      break;
    case Phase::kBandHeader:
      ok = consume_band_header();
      break;
    case Phase::kEndRecord:
      ok = consume_end_record();
      break;
    default:
      ok = false;
      break;
    }
    if (!ok)
      return false;
  }
  return phase_ != Phase::kError;
}

bool Parser::finish() {
  if (phase_ == Phase::kComplete)
    return true;
  if (phase_ == Phase::kError)
    return false;
  return fail(Error::kTruncatedDocument, "HTTP body ended before END record");
}

bool Parser::complete() const { return phase_ == Phase::kComplete; }
Error Parser::error() const { return error_; }
const std::string &Parser::error_message() const { return error_message_; }
const DocumentInfo &Parser::document() const { return document_; }

bool Parser::consume_file_header() {
  const auto *header = record_buffer_.data();
  if (!has_magic(header, "TPB1") || read_u16(header + 8) != kFileHeaderSize) {
    return fail(Error::kInvalidFileHeader, "invalid TPB1 header");
  }
  if (header[4] != 1U)
    return fail(Error::kUnsupportedVersion, "unsupported TPB1 version");
  if (header[5] != 0x07U || header[6] != 1U) {
    return fail(Error::kUnsupportedLayout, "unsupported bitmap layout");
  }
  if (header[7] > static_cast<std::uint8_t>(Compression::kPackBits)) {
    return fail(Error::kInvalidCompression, "unsupported compression");
  }
  if (crc32(header, 28) != read_u32(header + 28)) {
    return fail(Error::kHeaderCrcMismatch, "file header CRC mismatch");
  }

  document_.compression = static_cast<Compression>(header[7]);
  document_.width = read_u16(header + 10);
  document_.height = read_u32(header + 12);
  document_.band_height = read_u16(header + 16);
  document_.band_count = read_u16(header + 18);
  document_.raw_length = read_u32(header + 20);
  document_.source_crc = read_u32(header + 24);

  if (document_.width != kRequiredWidth || document_.height == 0 ||
      document_.height > kMaximumHeight || document_.band_height == 0 ||
      document_.band_height > kMaximumBandHeight) {
    return fail(Error::kInvalidDimensions, "invalid bitmap dimensions");
  }
  const std::uint32_t expected_raw = kRowBytes * document_.height;
  const std::uint32_t expected_bands =
      (document_.height + document_.band_height - 1U) / document_.band_height;
  if (document_.raw_length != expected_raw ||
      document_.band_count != expected_bands ||
      document_.raw_length > kMaximumBitmapBytes) {
    return fail(Error::kInvalidLength, "inconsistent TPB1 document length");
  }
  if (!sink_.begin_document(document_)) {
    return fail(Error::kSinkRejected, "bitmap sink rejected document");
  }
  phase_ = Phase::kRecordPrefix;
  record_size_ = 0;
  return true;
}

bool Parser::consume_record_prefix() {
  if (has_magic(record_buffer_.data(), "BAND")) {
    phase_ = Phase::kBandHeader;
    record_size_ = 4;
    return true;
  }
  if (has_magic(record_buffer_.data(), "END!")) {
    phase_ = Phase::kEndRecord;
    record_size_ = 4;
    return true;
  }
  return fail(Error::kUnexpectedRecord, "expected BAND or END record");
}

bool Parser::consume_band_header() {
  const auto *header = record_buffer_.data();
  current_band_.index = read_u16(header + 4);
  current_band_.rows = read_u16(header + 6);
  current_band_.y = read_u32(header + 8);
  current_band_.raw_length = read_u32(header + 12);
  current_payload_length_ = read_u32(header + 16);
  current_band_crc_ = read_u32(header + 20);

  const std::uint16_t expected_rows =
      static_cast<std::uint16_t>(std::min<std::uint32_t>(
          document_.band_height, document_.height - received_rows_));
  if (current_band_.index != next_band_index_ ||
      current_band_.rows != expected_rows ||
      current_band_.y != received_rows_ ||
      current_band_.raw_length != kRowBytes * expected_rows) {
    return fail(Error::kInvalidBand, "invalid BAND position or length");
  }
  const std::uint32_t maximum_payload = current_band_.raw_length * 2U + 16U;
  if (current_payload_length_ == 0 ||
      current_payload_length_ > maximum_payload ||
      (document_.compression == Compression::kNone &&
       current_payload_length_ != current_band_.raw_length)) {
    return fail(Error::kInvalidLength, "invalid BAND payload length");
  }

  payload_.clear();
  payload_.reserve(current_payload_length_);
  record_size_ = 0;
  phase_ = Phase::kBandPayload;
  return true;
}

bool Parser::consume_band_payload() {
  if (document_.compression == Compression::kPackBits) {
    if (decode_packbits(payload_.data(), payload_.size(),
                        current_band_.raw_length,
                        decoded_) != PackBitsError::kNone) {
      return fail(Error::kInvalidCompression, "invalid PackBits payload");
    }
  } else {
    decoded_ = payload_;
  }

  if (crc32(decoded_.data(), decoded_.size()) != current_band_crc_) {
    return fail(Error::kBandCrcMismatch, "BAND CRC mismatch");
  }
  if (!sink_.write_band(current_band_, decoded_.data(), decoded_.size())) {
    return fail(Error::kSinkRejected, "bitmap sink rejected BAND");
  }
  bitmap_crc_.update(decoded_.data(), decoded_.size());
  received_rows_ += current_band_.rows;
  received_raw_length_ += current_band_.raw_length;
  next_band_index_++;
  payload_.clear();
  decoded_.clear();
  phase_ = Phase::kRecordPrefix;
  record_size_ = 0;
  return true;
}

bool Parser::consume_end_record() {
  const auto *record = record_buffer_.data();
  if (read_u16(record + 4) != document_.band_count ||
      read_u16(record + 6) != 0 ||
      read_u32(record + 8) != document_.raw_length ||
      next_band_index_ != document_.band_count ||
      received_rows_ != document_.height ||
      received_raw_length_ != document_.raw_length) {
    return fail(Error::kInvalidLength, "inconsistent END record");
  }
  if (read_u32(record + 12) != bitmap_crc_.value()) {
    return fail(Error::kBitmapCrcMismatch, "bitmap CRC mismatch");
  }
  if (!sink_.end_document()) {
    return fail(Error::kSinkRejected, "bitmap sink rejected END record");
  }
  phase_ = Phase::kComplete;
  record_size_ = 0;
  return true;
}

bool Parser::fail(Error error, const char *message) {
  error_ = error;
  error_message_ = message;
  phase_ = Phase::kError;
  return false;
}

} // namespace thermal_terminal::tpb1
