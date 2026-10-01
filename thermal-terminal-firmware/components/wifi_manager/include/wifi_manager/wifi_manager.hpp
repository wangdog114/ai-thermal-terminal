#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "settings/user_settings.hpp"

namespace thermal_terminal {

struct WifiAccessPoint {
  std::string ssid;
  std::int8_t rssi{};
  bool secured{};
};

class WifiManager {
public:
  bool initialize();
  bool connect(const NetworkSettings &settings);
  bool scan(std::vector<WifiAccessPoint> &access_points);
  void disconnect();
  [[nodiscard]] bool connected() const;
};

} // namespace thermal_terminal
