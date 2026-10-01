#include "wifi_manager/wifi_manager.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace thermal_terminal {
namespace {

constexpr char kTag[] = "wifi-manager";
bool s_initialized = false;
std::atomic<bool> s_connected{false};
std::atomic<bool> s_started{false};
std::atomic<bool> s_should_reconnect{false};

void on_event(void *, esp_event_base_t event_base, int32_t event_id, void *) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    s_started = true;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_connected = false;
    ESP_LOGW(kTag, "Wi-Fi disconnected");
    if (s_initialized && s_should_reconnect) {
      const esp_err_t result = esp_wifi_connect();
      if (result != ESP_OK && result != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(kTag, "Wi-Fi reconnect request failed: %s",
                 esp_err_to_name(result));
      }
    }
  }
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP)
    s_started = false;
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_connected = true;
    ESP_LOGI(kTag, "Wi-Fi got IP");
  }
}

} // namespace

bool WifiManager::initialize() {
  if (s_initialized)
    return true;
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    return false;
  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    return false;
  if (esp_netif_create_default_wifi_sta() == nullptr)
    return false;
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&config) != ESP_OK)
    return false;
  if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event,
                                 nullptr) != ESP_OK ||
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event,
                                 nullptr) != ESP_OK) {
    return false;
  }
  s_initialized = true;
  return true;
}

bool WifiManager::connect(const NetworkSettings &settings) {
  wifi_config_t config{};
  if (!s_initialized || settings.ssid.empty() ||
      settings.ssid.size() > sizeof(config.sta.ssid) ||
      settings.password.size() >= sizeof(config.sta.password))
    return false;
  std::memcpy(config.sta.ssid, settings.ssid.data(), settings.ssid.size());
  std::memcpy(config.sta.password, settings.password.data(),
              settings.password.size());
  config.sta.threshold.authmode =
      settings.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    return false;

  s_should_reconnect = false;
  if (s_started)
    esp_wifi_disconnect();
  if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK)
    return false;
  if (!s_started) {
    const esp_err_t start_result = esp_wifi_start();
    if (start_result != ESP_OK)
      return false;
    s_started = true;
  }
  s_connected = false;
  s_should_reconnect = true;
  const esp_err_t result = esp_wifi_connect();
  if (result != ESP_OK && result != ESP_ERR_WIFI_CONN)
    return false;
  ESP_LOGI(kTag, "connecting to configured SSID");
  return true;
}

bool WifiManager::scan(std::vector<WifiAccessPoint> &access_points) {
  access_points.clear();
  if (!s_initialized || esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    return false;
  if (!s_started) {
    const esp_err_t start_result = esp_wifi_start();
    if (start_result != ESP_OK)
      return false;
    s_started = true;
  }

  wifi_scan_config_t config{};
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 100;
  config.scan_time.active.max = 300;
  config.show_hidden = false;
  if (esp_wifi_scan_start(&config, true) != ESP_OK)
    return false;

  std::uint16_t count = 0;
  if (esp_wifi_scan_get_ap_num(&count) != ESP_OK)
    return false;
  count = std::min<std::uint16_t>(count, 32);
  if (count == 0)
    return true;

  std::vector<wifi_ap_record_t> records(count);
  if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK)
    return false;
  access_points.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto &record = records[i];
    const auto length = strnlen(reinterpret_cast<const char *>(record.ssid),
                                sizeof(record.ssid));
    if (length == 0)
      continue;
    WifiAccessPoint point;
    point.ssid.assign(reinterpret_cast<const char *>(record.ssid), length);
    point.rssi = record.rssi;
    point.secured = record.authmode != WIFI_AUTH_OPEN;
    const auto existing = std::find_if(
        access_points.begin(), access_points.end(), [&](const auto &candidate) {
          return candidate.ssid == point.ssid;
        });
    if (existing == access_points.end())
      access_points.push_back(std::move(point));
    else if (point.rssi > existing->rssi)
      *existing = std::move(point);
  }
  std::sort(access_points.begin(), access_points.end(),
            [](const auto &left, const auto &right) {
              return left.rssi > right.rssi;
            });
  return true;
}

void WifiManager::disconnect() {
  if (!s_initialized)
    return;
  s_should_reconnect = false;
  esp_wifi_disconnect();
  esp_wifi_stop();
  s_started = false;
  s_connected = false;
}

bool WifiManager::connected() const { return s_connected.load(); }

} // namespace thermal_terminal
