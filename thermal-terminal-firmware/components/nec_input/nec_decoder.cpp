#include "nec_input/nec_decoder.hpp"

namespace thermal_terminal {
namespace {

bool near(std::uint16_t actual, std::uint16_t expected, std::uint16_t margin) {
  return actual >= expected - margin && actual <= expected + margin;
}

} // namespace

bool NecDecoder::decode(const NecPulse *pulses, std::size_t count,
                        std::uint64_t now_ms, NecFrame &frame) {
  if (pulses == nullptr || count < 2 || !near(pulses[0].mark_us, 9000, 1800)) {
    return false;
  }
  if (near(pulses[0].space_us, 2250, 600)) {
    if (!has_last_ || now_ms < last_ms_ || now_ms - last_ms_ > 300 ||
        !near(pulses[1].mark_us, 560, 250)) {
      return false;
    }
    frame = last_;
    frame.repeat = true;
    last_ms_ = now_ms;
    return true;
  }
  if (count < 33 || !near(pulses[0].space_us, 4500, 1000)) {
    return false;
  }

  std::uint32_t bits = 0;
  for (std::size_t index = 0; index < 32; ++index) {
    const auto &pulse = pulses[index + 1];
    if (!near(pulse.mark_us, 560, 250))
      return false;
    if (near(pulse.space_us, 1690, 450)) {
      bits |= 1UL << index;
    } else if (!near(pulse.space_us, 560, 250)) {
      return false;
    }
  }

  const std::uint8_t address_low = bits & 0xFFU;
  const std::uint8_t address_high = (bits >> 8) & 0xFFU;
  const std::uint8_t command = (bits >> 16) & 0xFFU;
  const std::uint8_t command_inverse = (bits >> 24) & 0xFFU;
  if (static_cast<std::uint8_t>(command ^ command_inverse) != 0xFFU) {
    return false;
  }
  frame.address =
      static_cast<std::uint8_t>(address_low ^ address_high) == 0xFFU
          ? address_low
          : static_cast<std::uint16_t>(address_low | (address_high << 8));
  frame.command = command;
  frame.repeat = false;
  last_ = frame;
  last_ms_ = now_ms;
  has_last_ = true;
  return true;
}

} // namespace thermal_terminal
