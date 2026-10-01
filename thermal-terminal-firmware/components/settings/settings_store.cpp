#include "settings/settings_store.hpp"

#include "nvs.h"
#include "nvs_flash.h"

namespace thermal_terminal {
namespace {

constexpr char kUserNamespace[] = "user";
constexpr char kNetworkNamespace[] = "network";
constexpr char kSessionNamespace[] = "session";

bool get_string(nvs_handle_t handle, const char *key, std::string &value) {
  std::size_t size = 0;
  const esp_err_t size_error = nvs_get_str(handle, key, nullptr, &size);
  if (size_error == ESP_ERR_NVS_NOT_FOUND) {
    value.clear();
    return true;
  }
  if (size_error != ESP_OK || size == 0 || size > 4096)
    return false;
  value.resize(size);
  if (nvs_get_str(handle, key, value.data(), &size) != ESP_OK)
    return false;
  value.resize(size - 1);
  return true;
}

bool set_string(nvs_handle_t handle, const char *key,
                const std::string &value) {
  return nvs_set_str(handle, key, value.c_str()) == ESP_OK;
}

bool get_u8(nvs_handle_t handle, const char *key, std::uint8_t &value) {
  const esp_err_t result = nvs_get_u8(handle, key, &value);
  return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

bool set_u8(nvs_handle_t handle, const char *key, std::uint8_t value) {
  return nvs_set_u8(handle, key, value) == ESP_OK;
}

bool get_u16(nvs_handle_t handle, const char *key, std::uint16_t &value) {
  const esp_err_t result = nvs_get_u16(handle, key, &value);
  return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

bool set_u16(nvs_handle_t handle, const char *key, std::uint16_t value) {
  return nvs_set_u16(handle, key, value) == ESP_OK;
}

bool get_bool(nvs_handle_t handle, const char *key, bool &value) {
  std::uint8_t raw = value ? 1 : 0;
  if (!get_u8(handle, key, raw))
    return false;
  value = raw != 0;
  return true;
}

bool set_bool(nvs_handle_t handle, const char *key, bool value) {
  return set_u8(handle, key, value ? 1 : 0);
}

bool open_read(const char *name, nvs_handle_t &handle) {
  return nvs_open(name, NVS_READONLY, &handle) == ESP_OK;
}

bool open_write(const char *name, nvs_handle_t &handle) {
  return nvs_open(name, NVS_READWRITE, &handle) == ESP_OK;
}

} // namespace

bool SettingsStore::initialize() {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() != ESP_OK)
      return false;
    result = nvs_flash_init();
  }
  return result == ESP_OK;
}

bool SettingsStore::load(UserSettings &user, NetworkSettings &network,
                         SessionSettings &session) {
  nvs_handle_t handle{};
  if (open_read(kUserNamespace, handle)) {
    std::uint8_t ui_language = static_cast<std::uint8_t>(user.ui_language);
    std::uint16_t line_height_hundredths =
        static_cast<std::uint16_t>(user.render.line_height * 100 + 0.5F);
    const bool ok = get_string(handle, "model", user.model_id) &&
                    get_u8(handle, "reasoning", user.reasoning_level) &&
                    get_bool(handle, "context", user.use_context) &&
                    get_u8(handle, "ui_language", ui_language) &&
                    get_bool(handle, "auto_print", user.printer.auto_print) &&
                    get_u8(handle, "font_size", user.render.font_size) &&
                    get_u16(handle, "line_height", line_height_hundredths) &&
                    get_u8(handle, "margin", user.render.margin) &&
                    get_u8(handle, "bottom_feed", user.render.bottom_feed) &&
                    get_u8(handle, "threshold", user.render.threshold);
    nvs_close(handle);
    if (!ok)
      return false;
    user.ui_language = ui_language == static_cast<std::uint8_t>(UiLanguage::kChinese)
                           ? UiLanguage::kChinese
                           : UiLanguage::kEnglish;
    user.render.line_height = line_height_hundredths / 100.0F;
  }
  if (open_read(kNetworkNamespace, handle)) {
    const bool ok = get_string(handle, "ssid", network.ssid) &&
                    get_string(handle, "password", network.password) &&
                    get_string(handle, "worker_url", network.worker_url) &&
                    get_string(handle, "token", network.terminal_token);
    nvs_close(handle);
    if (!ok)
      return false;
  }
  if (open_read(kSessionNamespace, handle)) {
    const bool ok =
        get_string(handle, "session_id", session.session_id) &&
        get_string(handle, "catalog_etag", session.model_catalog_etag);
    nvs_close(handle);
    if (!ok)
      return false;
  }
  normalize_settings(user);
  return true;
}

bool SettingsStore::save_user(const UserSettings &user) {
  nvs_handle_t handle{};
  if (!open_write(kUserNamespace, handle))
    return false;
  const bool ok = set_string(handle, "model", user.model_id) &&
                  set_u8(handle, "reasoning", user.reasoning_level) &&
                  set_bool(handle, "context", user.use_context) &&
                  set_u8(handle, "ui_language", static_cast<std::uint8_t>(user.ui_language)) &&
                  set_bool(handle, "auto_print", user.printer.auto_print) &&
                  set_u8(handle, "font_size", user.render.font_size) &&
                  set_u16(handle, "line_height", static_cast<std::uint16_t>(
                      user.render.line_height * 100 + 0.5F)) &&
                  set_u8(handle, "margin", user.render.margin) &&
                  set_u8(handle, "bottom_feed", user.render.bottom_feed) &&
                  set_u8(handle, "threshold", user.render.threshold);
  const bool committed = ok && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return committed;
}

bool SettingsStore::save_network(const NetworkSettings &network) {
  nvs_handle_t handle{};
  if (!open_write(kNetworkNamespace, handle))
    return false;
  const bool ok = set_string(handle, "ssid", network.ssid) &&
                  set_string(handle, "password", network.password) &&
                  set_string(handle, "worker_url", network.worker_url) &&
                  set_string(handle, "token", network.terminal_token);
  const bool committed = ok && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return committed;
}

bool SettingsStore::save_session(const SessionSettings &session) {
  nvs_handle_t handle{};
  if (!open_write(kSessionNamespace, handle))
    return false;
  const bool ok =
      set_string(handle, "session_id", session.session_id) &&
      set_string(handle, "catalog_etag", session.model_catalog_etag);
  const bool committed = ok && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return committed;
}

} // namespace thermal_terminal
