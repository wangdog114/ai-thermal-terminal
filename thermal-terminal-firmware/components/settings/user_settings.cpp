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

} // namespace thermal_terminal
