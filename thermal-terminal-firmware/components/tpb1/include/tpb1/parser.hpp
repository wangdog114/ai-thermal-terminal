#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tpb1/crc32.hpp"

namespace thermal_terminal::tpb1 {

constexpr std::uint16_t kRequiredWidth = 384;
constexpr std::uint16_t kRowBytes = kRequiredWidth / 8;
constexpr std::uint32_t kMaximumHeight = 20000;
constexpr std::uint16_t kMaximumBandHeight = 1024;
constexpr std::uint32_t kMaximumBitmapBytes = kRowBytes * kMaximumHeight;

enum class Compression : std::uint8_t { kNone = 0, kPackBits = 1 };

struct DocumentInfo {
  std::uint16_t width{};
  std::uint32_t height{};
  std::uint16_t band_height{};
  std::uint16_t band_count{};
  std::uint32_t raw_length{};
  std::uint32_t source_crc{};
  Compression compression{Compression::kNone};
};

struct BandInfo {
  std::uint16_t index{};
  std::uint16_t rows{};
  std::uint32_t y{};
  std::uint32_t raw_length{};
};

class Sink {
public:
  virtual ~Sink() = default;
  virtual bool begin_document(const DocumentInfo &info) = 0;
  virtual bool write_band(const BandInfo &info, const std::uint8_t *data,
                          std::size_t size) = 0;
  virtual bool end_document() = 0;
};

enum class Error : std::uint8_t {
  kNone,
  kInvalidFileHeader,
  kUnsupportedVersion,
  kUnsupportedLayout,
  kInvalidDimensions,
  kInvalidLength,
  kHeaderCrcMismatch,
  kUnexpectedRecord,
  kInvalidBand,
  kInvalidCompression,
  kBandCrcMismatch,
  kBitmapCrcMismatch,
  kSinkRejected,
  kTruncatedDocument,
  kTrailingData
};

class Parser {
public:
  explicit Parser(Sink &sink);

  bool feed(const std::uint8_t *data, std::size_t size);
  bool finish();
  void reset();

  [[nodiscard]] bool complete() const;
  [[nodiscard]] Error error() const;
  [[nodiscard]] const std::string &error_message() const;
  [[nodiscard]] const DocumentInfo &document() const;

private:
  enum class Phase : std::uint8_t {
    kFileHeader,
    kRecordPrefix,
    kBandHeader,
    kBandPayload,
    kEndRecord,
    kComplete,
    kError
  };

  bool consume_file_header();
  bool consume_record_prefix();
  bool consume_band_header();
  bool consume_band_payload();
  bool consume_end_record();
  bool fail(Error error, const char *message);

  Sink &sink_;
  Phase phase_{Phase::kFileHeader};
  Error error_{Error::kNone};
  std::string error_message_;
  DocumentInfo document_;
  BandInfo current_band_;
  std::uint32_t current_band_crc_{};
  std::uint32_t current_payload_length_{};
  std::uint16_t next_band_index_{};
  std::uint32_t received_rows_{};
  std::uint32_t received_raw_length_{};
  Crc32 bitmap_crc_;
  std::array<std::uint8_t, 32> record_buffer_{};
  std::size_t record_size_{};
  std::vector<std::uint8_t> payload_;
  std::vector<std::uint8_t> decoded_;
};

} // namespace thermal_terminal::tpb1
