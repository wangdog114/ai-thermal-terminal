#include "settings/user_settings.hpp"

#include <algorithm>

namespace thermal_terminal {

void normalize_settings(UserSettings &settings) {
  settings.render.font_size =
      std::clamp<std::uint8_t>(settings.render.font_size, 14, 30);
  settings.render.line_height =
      std::clamp(settings.render.line_height, 1.1F, 1.9F);
  settings.render.margin =
      std::clamp<std::uint8_t>(settings.render.margin, 0, 32);
  settings.render.bottom_feed =
      std::clamp<std::uint8_t>(settings.render.bottom_feed, 0, 96);
  settings.render.band_height =
      std::clamp<std::uint16_t>(settings.render.band_height, 32, 1024);
}

std::string worker_url_for_protocol(const std::string &url, bool use_https) {
  if (url.empty())
    return {};
  std::string host = url;
  if (host.compare(0, 8, "https://") == 0)
    host.erase(0, 8);
  else if (host.compare(0, 7, "http://") == 0)
    host.erase(0, 7);
  return (use_https ? "https://" : "http://") + host;
}

} // namespace thermal_terminal
