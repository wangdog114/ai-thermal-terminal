#include "nec_input/remote_keymap.hpp"

namespace thermal_terminal {
const RemoteKeyBinding *remote_key_bindings(std::size_t &count) {
  count = board_config::kRemoteButtonCount;
  return board_config::kRemoteButtons;
}

bool map_nec_key(const NecFrame &frame, KeyEvent &event) {
  if (frame.address != board_config::kRemoteAddress)
    return false;
  std::size_t count = 0;
  const auto *bindings = remote_key_bindings(count);
  for (std::size_t index = 0; index < count; ++index) {
    if (bindings[index].command != frame.command)
      continue;
    event.key = bindings[index].key;
    event.command = frame.command;
    event.long_press = frame.repeat;
    event.repeat = frame.repeat;
    return true;
  }
  return false;
}

const char *logical_key_name(LogicalKey key) {
  switch (key) {
  case LogicalKey::kDigit0:
    return "0";
  case LogicalKey::kDigit1:
    return "1";
  case LogicalKey::kDigit2:
    return "2";
  case LogicalKey::kDigit3:
    return "3";
  case LogicalKey::kDigit4:
    return "4";
  case LogicalKey::kDigit5:
    return "5";
  case LogicalKey::kDigit6:
    return "6";
  case LogicalKey::kDigit7:
    return "7";
  case LogicalKey::kDigit8:
    return "8";
  case LogicalKey::kDigit9:
    return "9";
  case LogicalKey::kUp:
    return "up";
  case LogicalKey::kDown:
    return "down";
  case LogicalKey::kLeft:
    return "left";
  case LogicalKey::kRight:
    return "right";
  case LogicalKey::kConfirm:
    return "confirm";
  case LogicalKey::kBack:
    return "back";
  case LogicalKey::kMenu:
    return "menu";
  case LogicalKey::kSend:
    return "send";
  case LogicalKey::kPrint:
    return "print";
  case LogicalKey::kClear:
    return "clear";
  case LogicalKey::kHome:
    return "home";
  case LogicalKey::kPower:
    return "power";
  }
  return "unknown";
}

} // namespace thermal_terminal
