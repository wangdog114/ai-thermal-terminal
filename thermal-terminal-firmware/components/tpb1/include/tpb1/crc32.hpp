#pragma once

#include <cstddef>
#include <cstdint>

namespace thermal_terminal::tpb1 {

class Crc32 {
public:
  void reset();
  void update(const std::uint8_t *data, std::size_t size);
  [[nodiscard]] std::uint32_t value() const;

private:
  std::uint32_t state_{0xFFFFFFFFU};
};

std::uint32_t crc32(const std::uint8_t *data, std::size_t size);

} // namespace thermal_terminal::tpb1
