#pragma once

#include <cstdint>

namespace thermal_terminal {

enum class EventType : std::uint8_t {
  kBootCompleted,
  kWifiConnected,
  kWifiDisconnected,
  kModelsUpdated,
  kHistoryPageLoaded,
  kKeyPressed,
  kKeyLongPressed,
  kSendRequested,
  kRetryRequested,
  kPrintRequested,
  kClearRequested,
  kResponseStarted,
  kResponseReady,
  kPrintCompleted,
  kLowBattery,
  kError
};

struct AppEvent {
  EventType type{};
  std::uint32_t argument0{};
  std::uint32_t argument1{};
};

static_assert(sizeof(AppEvent) <= 12, "AppEvent must remain cheap to queue");

} // namespace thermal_terminal
