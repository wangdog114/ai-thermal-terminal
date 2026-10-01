#pragma once

#include <cstdint>

namespace thermal_terminal {

enum class AppState : std::uint8_t {
  kBoot,
  kLoadingConfig,
  kWifiConnecting,
  kIdle,
  kEditing,
  kLoadingHistory,
  kBrowsingHistory,
  kSending,
  kReceiving,
  kValidating,
  kPreviewing,
  kPrinting,
  kSettings,
  kLowBattery,
  kError
};

constexpr const char *app_state_name(AppState state) {
  switch (state) {
  case AppState::kBoot:
    return "boot";
  case AppState::kLoadingConfig:
    return "loading_config";
  case AppState::kWifiConnecting:
    return "wifi_connecting";
  case AppState::kIdle:
    return "idle";
  case AppState::kEditing:
    return "editing";
  case AppState::kLoadingHistory:
    return "loading_history";
  case AppState::kBrowsingHistory:
    return "browsing_history";
  case AppState::kSending:
    return "sending";
  case AppState::kReceiving:
    return "receiving";
  case AppState::kValidating:
    return "validating";
  case AppState::kPreviewing:
    return "previewing";
  case AppState::kPrinting:
    return "printing";
  case AppState::kSettings:
    return "settings";
  case AppState::kLowBattery:
    return "low_battery";
  case AppState::kError:
    return "error";
  }
  return "unknown";
}

} // namespace thermal_terminal
