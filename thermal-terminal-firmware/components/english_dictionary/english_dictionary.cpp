#include "english_dictionary/english_dictionary.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#endif

namespace thermal_terminal {
namespace {
constexpr std::uint32_t kHeaderSize = 16;
constexpr std::uint32_t kIndexSize = 10000 * 8;

std::uint32_t read_u32(const std::array<std::uint8_t, 4> &bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

bool code_matches(const std::vector<std::uint8_t> &packed, std::uint8_t length,
                  const std::string &digits, bool exact) {
  if (exact ? length != digits.size() : length < digits.size())
    return false;
  for (std::size_t index = 0; index < digits.size(); ++index) {
    const auto value = index % 2 == 0 ? packed[index / 2] >> 4
                                      : packed[index / 2] & 0x0F;
    if (value != static_cast<std::uint8_t>(digits[index] - '0'))
      return false;
  }
  return true;
}

void add_range(const std::uint8_t *data, std::size_t data_size,
               std::uint32_t start, std::uint32_t end,
               const std::string &digits, std::vector<std::string> &words,
               std::size_t limit, bool exact) {
  if (start == 0 || end <= start || words.size() >= limit)
    return;
  if (end > data_size)
    return;
  const auto *cursor = data + start;
  const auto *finish = data + end;
  while (cursor + 4 <= finish && words.size() < limit) {
    const auto code_length = cursor[0];
    const auto word_length = cursor[1];
    cursor += 4;
    const auto packed_size = (code_length + 1) / 2;
    if (cursor + packed_size + word_length > finish)
      return;
    std::vector<std::uint8_t> packed(cursor, cursor + packed_size);
    cursor += packed_size;
    std::string word(reinterpret_cast<const char *>(cursor), word_length);
    cursor += word_length;
    if (code_matches(packed, code_length, digits, exact))
      words.push_back(std::move(word));
  }
}
} // namespace

EnglishDictionary::~EnglishDictionary() {
#ifdef ESP_PLATFORM
  if (data_ != nullptr)
    heap_caps_free(data_);
#else
  std::free(data_);
#endif
}

bool EnglishDictionary::initialize(const char *path) {
#ifdef ESP_PLATFORM
  static bool mounted = false;
  if (!mounted) {
    esp_vfs_spiffs_conf_t config{};
    config.base_path = "/assets";
    config.partition_label = "assets";
    config.max_files = 2;
    config.format_if_mount_failed = false;
    if (esp_vfs_spiffs_register(&config) != ESP_OK)
      return false;
    mounted = true;
  }
#endif
  auto *file = std::fopen(path, "rb");
  if (file == nullptr)
    return false;
  std::array<std::uint8_t, 4> magic{};
  const bool valid = std::fread(magic.data(), 1, magic.size(), file) == magic.size() &&
                     std::memcmp(magic.data(), "T9D1", 4) == 0;
  if (!valid) {
    std::fclose(file);
    return false;
  }
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }
  const auto size = static_cast<std::size_t>(std::ftell(file));
  if (size < kHeaderSize + kIndexSize || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }
  std::uint8_t *data = nullptr;
#ifdef ESP_PLATFORM
  data = static_cast<std::uint8_t *>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (data == nullptr)
    data = static_cast<std::uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
#else
  data = static_cast<std::uint8_t *>(std::malloc(size));
#endif
  if (data == nullptr || std::fread(data, 1, size, file) != size) {
#ifdef ESP_PLATFORM
    if (data != nullptr) heap_caps_free(data);
#else
    std::free(data);
#endif
    std::fclose(file);
    return false;
  }
  std::fclose(file);
  std::vector<std::uint32_t> index(10000 * 2);
  for (std::size_t i = 0; i < index.size(); ++i) {
    std::array<std::uint8_t, 4> bytes{{data[kHeaderSize + i * 4],
                                       data[kHeaderSize + i * 4 + 1],
                                       data[kHeaderSize + i * 4 + 2],
                                       data[kHeaderSize + i * 4 + 3]}};
    index[i] = read_u32(bytes);
  }
  if (data_ != nullptr) {
#ifdef ESP_PLATFORM
    heap_caps_free(data_);
#else
    std::free(data_);
#endif
  }
  data_ = data;
  data_size_ = size;
  index_ = std::move(index);
  return true;
}

bool EnglishDictionary::query(const std::string &digits,
                              std::vector<std::string> &words,
                              std::size_t limit) const {
  words.clear();
  if (data_ == nullptr || digits.empty() || digits.size() > 31 ||
      std::any_of(digits.begin(), digits.end(), [](char c) {
        return c < '2' || c > '9';
      }))
    return false;
  std::vector<std::pair<std::uint16_t, std::uint16_t>> ranges;
  if (digits.size() < 4) {
    ranges.emplace_back(0, 0);
    unsigned prefix = 0;
    for (const char digit : digits)
      prefix = prefix * 10 + static_cast<unsigned>(digit - '0');
    unsigned multiplier = 1;
    for (std::size_t i = digits.size(); i < 4; ++i)
      multiplier *= 10;
    ranges.emplace_back(static_cast<std::uint16_t>(prefix * multiplier),
                        static_cast<std::uint16_t>((prefix + 1) * multiplier - 1));
  } else {
    unsigned group = 0;
    for (std::size_t i = 0; i < 4; ++i)
      group = group * 10 + static_cast<unsigned>(digits[i] - '0');
    ranges.emplace_back(static_cast<std::uint16_t>(group),
                        static_cast<std::uint16_t>(group));
  }
  auto read_range = [&](std::uint16_t first_group, std::uint16_t last_group,
                        std::uint32_t &start, std::uint32_t &end) {
    start = end = 0;
    for (std::uint32_t group = first_group; group <= last_group; ++group) {
      const auto candidate_start = index_[group * 2];
      const auto candidate_end = index_[group * 2 + 1];
      if (candidate_start == 0)
        continue;
      if (start == 0)
        start = candidate_start;
      end = candidate_end;
    }
    return true;
  };
  for (const auto [first_group, last_group] : ranges) {
    std::uint32_t start = 0, end = 0;
    if (!read_range(first_group, last_group, start, end))
      return false;
    add_range(data_, data_size_, start, end, digits, words, limit, true);
  }
  if (words.size() < limit) {
    for (const auto [first_group, last_group] : ranges) {
      std::uint32_t start = 0, end = 0;
      if (!read_range(first_group, last_group, start, end))
        return false;
      add_range(data_, data_size_, start, end, digits, words, limit, false);
      if (words.size() >= limit)
        break;
    }
  }
  return true;
}

} // namespace thermal_terminal
