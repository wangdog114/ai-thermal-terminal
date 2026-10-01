#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace thermal_terminal {

class EnglishDictionary {
public:
  ~EnglishDictionary();
  EnglishDictionary() = default;
  EnglishDictionary(const EnglishDictionary &) = delete;
  EnglishDictionary &operator=(const EnglishDictionary &) = delete;
  bool initialize(const char *path = "/assets/en.t9");
  [[nodiscard]] bool ready() const { return data_ != nullptr; }
  bool query(const std::string &digits, std::vector<std::string> &words,
             std::size_t limit = 8) const;

private:
  std::uint8_t *data_{};
  std::size_t data_size_{};
  std::vector<std::uint32_t> index_;
};

} // namespace thermal_terminal
