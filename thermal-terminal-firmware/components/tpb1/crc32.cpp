#include "tpb1/crc32.hpp"

namespace thermal_terminal::tpb1 {

void Crc32::reset() { state_ = 0xFFFFFFFFU; }

void Crc32::update(const std::uint8_t *data, std::size_t size) {
  for (std::size_t index = 0; index < size; ++index) {
    state_ ^= data[index];
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (state_ & 1U);
      state_ = (state_ >> 1U) ^ (0xEDB88320U & mask);
    }
  }
}

std::uint32_t Crc32::value() const { return state_ ^ 0xFFFFFFFFU; }

std::uint32_t crc32(const std::uint8_t *data, std::size_t size) {
  Crc32 crc;
  crc.update(data, size);
  return crc.value();
}

} // namespace thermal_terminal::tpb1
