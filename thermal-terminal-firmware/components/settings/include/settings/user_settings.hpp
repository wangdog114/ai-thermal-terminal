#pragma once

#include <cstdint>
#include <string>

namespace thermal_terminal {

enum class DitherMode : std::uint8_t { kThreshold, kBayer4 };
enum class CompressionMode : std::uint8_t { kNone, kPackBits };
enum class UiLanguage : std::uint8_t { kEnglish, kChinese };

struct RenderSettings {
  std::uint8_t font_size{22};
  float line_height{1.45F};
  std::uint8_t margin{12};
  std::uint8_t bottom_feed{24};
  std::uint8_t threshold{185};
  DitherMode dither{DitherMode::kThreshold};
  CompressionMode compression{CompressionMode::kPackBits};
  std::uint16_t band_height{256};
};

struct PrinterSettings {
  bool auto_print{false};
  std::uint8_t density{8};
  std::uint8_t speed{2};
};

struct UserSettings {
  std::string model_id{"OpenAI:gpt-5.4-mini"};
  std::uint8_t reasoning_level{0};
  bool use_context{true};
  bool use_https{true};
  UiLanguage ui_language{UiLanguage::kEnglish};
  RenderSettings render;
  PrinterSettings printer;
};

struct NetworkSettings {
  std::string ssid;
  std::string password;
  std::string worker_url;
  std::string terminal_token;
};

struct SessionSettings {
  std::string session_id;
  std::string model_catalog_etag;
};

void normalize_settings(UserSettings &settings);
std::string worker_url_for_protocol(const std::string &url, bool use_https);

} // namespace thermal_terminal
