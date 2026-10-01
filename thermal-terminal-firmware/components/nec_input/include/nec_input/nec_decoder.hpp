#pragma once

#include <cstddef>
#include <cstdint>

namespace thermal_terminal {

struct NecPulse {
  std::uint16_t mark_us{};
  std::uint16_t space_us{};
};

struct NecFrame {
  std::uint16_t address{};
  std::uint8_t command{};
  bool repeat{};
};

class NecDecoder {
public:
  bool decode(const NecPulse *pulses, std::size_t count, std::uint64_t now_ms,
              NecFrame &frame);

private:
  NecFrame last_{};
  std::uint64_t last_ms_{};
  bool has_last_{};
};

} // namespace thermal_terminal
