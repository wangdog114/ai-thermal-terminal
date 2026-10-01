#pragma once

#include <cstdint>

namespace thermal_terminal {

enum class LogicalKey : std::uint8_t {
  kDigit0,
  kDigit1,
  kDigit2,
  kDigit3,
  kDigit4,
  kDigit5,
  kDigit6,
  kDigit7,
  kDigit8,
  kDigit9,
  kUp,
  kDown,
  kLeft,
  kRight,
  kConfirm,
  kBack,
  kMenu,
  kSend,
  kPrint,
  kClear,
  kHome,
  kPower
};

struct KeyEvent {
  LogicalKey key{};
  std::uint8_t command{};
  bool long_press{};
  bool repeat{};
};

class InputDevice {
public:
  virtual ~InputDevice() = default;
  virtual bool initialize() = 0;
  virtual bool read(KeyEvent &event, std::uint32_t timeout_ms) = 0;
};

} // namespace thermal_terminal
