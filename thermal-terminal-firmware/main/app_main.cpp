#include "app_core/app_state.hpp"
#include "board_config/board_config.hpp"
#include "bitmap_store/bitmap_store.hpp"
#include "csn_a2_printer/csn_a2_printer.hpp"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nec_input/nec_receiver.hpp"
#include "nec_input/remote_keymap.hpp"
#include "oled_display/ssd1306_display.hpp"
#include "settings/settings_store.hpp"
#include "settings/user_settings.hpp"
#include "terminal_client/http_terminal_client.hpp"
#include "terminal_ui/terminal_ui.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include "esp_timer.h"
#include "english_dictionary/english_dictionary.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr char kTag[] = "thermal-terminal";

struct RuntimeContext {
  thermal_terminal::SettingsStore *store{};
  thermal_terminal::NetworkSettings *network{};
  thermal_terminal::SessionSettings *session{};
  thermal_terminal::WifiManager *wifi{};
};

RuntimeContext *s_runtime = nullptr;
thermal_terminal::UserSettings s_settings;
thermal_terminal::NetworkSettings s_network;
thermal_terminal::SessionSettings s_session;
thermal_terminal::SettingsStore s_store;
thermal_terminal::WifiManager s_wifi;
thermal_terminal::EnglishDictionary s_dictionary;
thermal_terminal::BitmapStore s_bitmap;
thermal_terminal::TerminalUi s_ui;
SemaphoreHandle_t s_data_mutex = nullptr;
SemaphoreHandle_t s_ir_mutex = nullptr;
thermal_terminal::Ssd1306Display s_oled(thermal_terminal::board_config::kOledSdaGpio,
                                        thermal_terminal::board_config::kOledSclGpio,
                                        thermal_terminal::board_config::kOledI2cAddress);
thermal_terminal::NecReceiver s_ir(thermal_terminal::board_config::kIrReceiverGpio);
thermal_terminal::CsnA2Printer s_printer(thermal_terminal::board_config::kPrinterUart,
                                         thermal_terminal::board_config::kPrinterTxGpio,
                                         thermal_terminal::board_config::kPrinterRxGpio,
                                         thermal_terminal::board_config::kPrinterBaudrate,
                                         thermal_terminal::board_config::kPrinterDensity);
std::vector<thermal_terminal::ModelInfo> s_models;
thermal_terminal::NetworkSettings s_wifi_previous_network;
thermal_terminal::NetworkSettings s_wifi_pending_network;
bool s_wifi_setup_pending = false;
std::uint64_t s_wifi_setup_started_ms = 0;
RuntimeContext s_context{&s_store, &s_network, &s_session, &s_wifi};

class DataGuard {
public:
  DataGuard() {
    if (s_data_mutex != nullptr)
      xSemaphoreTakeRecursive(s_data_mutex, portMAX_DELAY);
  }
  ~DataGuard() {
    if (s_data_mutex != nullptr)
      xSemaphoreGiveRecursive(s_data_mutex);
  }
};

void draw_ui() {
  if (!s_oled.ready())
    return;
  if (s_ui.screen() == thermal_terminal::UiScreen::kPreview && s_bitmap.ready()) {
    const auto &doc = s_bitmap.document();
    s_oled.draw_bitmap_window(s_bitmap.data(), s_bitmap.size(), doc.width,
                              doc.height, s_ui.preview_x(), s_ui.preview_y());
  } else {
    s_ui.render(s_oled.canvas(), s_wifi.connected(), s_settings, s_models,
                s_bitmap.ready());
  }
  s_oled.present();
  s_ui.mark_clean();
}

bool require_network() {
  if (s_runtime == nullptr || !s_runtime->wifi->connected()) {
    printf("Wi-Fi is not connected\n");
    return false;
  }
  if (s_runtime->network->worker_url.empty() ||
      s_runtime->network->terminal_token.empty()) {
    printf("Worker URL or Token is not configured\n");
    return false;
  }
  return true;
}

bool require_printer_interface() {
  if (thermal_terminal::board_config::kPrinterInterfaceReady)
    return true;
  printf("Printer UART disabled: confirm the replacement printer uses TTL, "
         "then set kPrinterInterfaceReady=true in board_config.hpp\n");
  return false;
}

void print_error(const thermal_terminal::ClientResult &result) {
  printf("HTTP %u: %s: %s\n", result.http_status, result.error_code.c_str(),
         result.error_message.c_str());
}

bool load_models() {
  if (!require_network())
    return false;
  thermal_terminal::HttpTerminalClient client(*s_runtime->network);
  std::string default_model;
  std::string response_etag;
  std::vector<thermal_terminal::ModelInfo> models;
  const auto result =
      client.fetch_models("", models, default_model, response_etag);
  if (!result.ok) {
    print_error(result);
    return false;
  }
  if (models.empty()) {
    printf("Worker returned an empty model catalog\n");
    return false;
  }
  s_models = std::move(models);
  if (!response_etag.empty() &&
      s_runtime->session->model_catalog_etag != response_etag) {
    s_runtime->session->model_catalog_etag = response_etag;
    if (!s_runtime->store->save_session(*s_runtime->session)) {
      printf("Warning: could not save model catalog ETag\n");
    }
  }
  bool current_available = false;
  for (const auto &model : s_models) {
    if (model.id == s_settings.model_id)
      current_available = true;
  }
  if (!current_available) {
    s_settings.model_id =
        default_model.empty() ? s_models.front().id : default_model;
    if (!s_runtime->store->save_user(s_settings)) {
      printf("Warning: could not save selected model\n");
    }
  }
  return true;
}

int cmd_status(int, char **) {
  DataGuard guard;
  if (s_runtime == nullptr)
    return 1;
  printf("wifi_ssid=%s\n", s_runtime->network->ssid.c_str());
  printf("worker_url=%s\n", s_runtime->network->worker_url.c_str());
  printf("worker_token=%s\n",
         s_runtime->network->terminal_token.empty() ? "not-set" : "set");
  printf("wifi_connected=%s\n", s_runtime->wifi->connected() ? "yes" : "no");
  printf("session_id=%s\n", s_runtime->session->session_id.c_str());
  return 0;
}

int cmd_set_wifi(int argc, char **argv) {
  DataGuard guard;
  if (s_runtime == nullptr || argc != 3) {
    printf("usage: set_wifi <ssid> <password>\n");
    return 1;
  }
  s_runtime->network->ssid = argv[1];
  s_runtime->network->password = argv[2];
  if (!s_runtime->store->save_network(*s_runtime->network) ||
      !s_runtime->wifi->connect(*s_runtime->network)) {
    printf("failed to save or start Wi-Fi\n");
    return 1;
  }
  printf("Wi-Fi configuration saved; connecting\n");
  return 0;
}

int cmd_set_worker(int argc, char **argv) {
  DataGuard guard;
  if (s_runtime == nullptr || argc != 3) {
    printf("usage: set_worker <worker_url> <terminal_token>\n");
    return 1;
  }
  s_runtime->network->worker_url = argv[1];
  s_runtime->network->terminal_token = argv[2];
  if (!s_runtime->store->save_network(*s_runtime->network)) {
    printf("failed to save Worker configuration\n");
    return 1;
  }
  printf("Worker configuration saved\n");
  return 0;
}

int cmd_models(int, char **) {
  DataGuard guard;
  if (!load_models())
    return 1;
  printf("Models (%u):\n", static_cast<unsigned>(s_models.size()));
  for (const auto &model : s_models) {
    printf("%s %s (%s), reasoning levels: %u\n",
           model.id == s_settings.model_id ? "*" : " ", model.id.c_str(),
           model.label.c_str(),
           static_cast<unsigned>(model.reasoning_levels.size()));
  }
  return 0;
}

int cmd_ask(int argc, char **argv) {
  DataGuard guard;
  if (argc < 2) {
    printf("usage: ask <message>\n");
    return 1;
  }
  if (!require_network())
    return 1;
  if (s_models.empty() && !load_models())
    return 1;

  std::string prompt;
  for (int index = 1; index < argc; ++index) {
    if (index > 1)
      prompt += ' ';
    prompt += argv[index];
  }

  thermal_terminal::SendRequest request;
  request.session_id = s_session.session_id;
  request.model_id = s_settings.model_id;
  request.reasoning_level = s_settings.reasoning_level;
  request.use_context = s_settings.use_context;
  request.render = s_settings.render;
  request.content = std::move(prompt);

  s_bitmap.reset();
  printf("Sending to %s; waiting for TPB1...\n", request.model_id.c_str());
  fflush(stdout);
  thermal_terminal::HttpTerminalClient client(s_network);
  thermal_terminal::BinaryResponseMetadata metadata;
  const auto result = client.send(request, s_bitmap, metadata);
  if (!result.ok || !s_bitmap.ready()) {
    print_error(result);
    s_bitmap.reset();
    return 1;
  }

  if (!metadata.session_id.empty() &&
      metadata.session_id != s_session.session_id) {
    s_session.session_id = metadata.session_id;
    if (!s_store.save_session(s_session)) {
      printf("Warning: could not save session ID\n");
    }
  }
  const auto &document = s_bitmap.document();
  printf("TPB1 ready: %u x %lu, %u bands, %u bytes\n", document.width,
         static_cast<unsigned long>(document.height), document.band_count,
         static_cast<unsigned>(s_bitmap.size()));
  printf("assistant_message_id=%s\n", metadata.assistant_message_id.c_str());
  printf("auto_print=%s\n", s_settings.printer.auto_print ? "on" : "off");
  if (s_oled.ready()) {
    s_oled.draw_bitmap_window(s_bitmap.data(), s_bitmap.size(), document.width,
                              document.height, 0, 0);
    s_oled.present();
    printf("OLED preview updated at x=0 y=0\n");
  }
  return 0;
}

int cmd_show_last(int, char **) {
  DataGuard guard;
  if (!require_network())
    return 1;
  if (s_session.session_id.empty()) {
    printf("No session ID yet\n");
    return 1;
  }
  thermal_terminal::HttpTerminalClient client(s_network);
  std::vector<thermal_terminal::HistoryMessage> messages;
  std::string next_cursor;
  const auto history =
      client.fetch_history(s_session.session_id, "", messages, next_cursor);
  if (!history.ok) {
    print_error(history);
    return 1;
  }
  const thermal_terminal::HistoryMessage *last_reply = nullptr;
  for (const auto &message : messages) {
    if (message.assistant)
      last_reply = &message;
  }
  if (last_reply == nullptr) {
    printf("No assistant message in the latest history page\n");
    return 1;
  }

  s_bitmap.reset();
  printf("Rendering saved reply %s...\n", last_reply->id.c_str());
  fflush(stdout);
  thermal_terminal::BinaryResponseMetadata metadata;
  const auto result =
      client.render_message(s_session.session_id, last_reply->id,
                            s_settings.render, s_bitmap, metadata);
  if (!result.ok || !s_bitmap.ready()) {
    if (!result.ok) {
      print_error(result);
    } else {
      printf("Renderer returned no validated bitmap\n");
    }
    s_bitmap.reset();
    return 1;
  }
  const auto &document = s_bitmap.document();
  printf("Saved TPB1 ready: %u x %lu, %u bands\n", document.width,
         static_cast<unsigned long>(document.height), document.band_count);
  if (s_oled.ready()) {
    s_oled.draw_bitmap_window(s_bitmap.data(), s_bitmap.size(), document.width,
                              document.height, 0, 0);
    s_oled.present();
    printf("OLED preview updated at x=0 y=0\n");
  } else {
    printf("OLED is not connected; bitmap is available in PSRAM\n");
  }
  return 0;
}

int cmd_printer_test(int, char **) {
  DataGuard guard;
  if (!require_printer_interface()) return 1;
  if (!s_printer.initialize()) {
    printf("CSN-A2 UART initialization failed\n");
    return 1;
  }
  printf("Printing CSN-A2 test pattern...\n");
  if (!s_printer.print_test_pattern()) {
    printf("CSN-A2 test pattern failed\n");
    return 1;
  }
  printf("CSN-A2 test pattern sent\n");
  return 0;
}

int cmd_printer_text(int, char **) {
  DataGuard guard;
  if (!require_printer_interface()) return 1;
  if (!s_printer.initialize()) {
    printf("CSN-A2 UART initialization failed\n");
    return 1;
  }
  printf("Printing ASCII diagnostic at %d baud...\n", s_printer.baudrate());
  if (!s_printer.print_text("CSN-A2 UART TEST 123")) {
    printf("CSN-A2 text diagnostic failed\n");
    return 1;
  }
  printf("CSN-A2 text diagnostic sent\n");
  return 0;
}

int cmd_printer_selftest(int, char **) {
  DataGuard guard;
  if (!require_printer_interface()) return 1;
  if (!s_printer.initialize()) {
    printf("CSN-A2 UART initialization failed\n");
    return 1;
  }
  printf("Requesting CSN-A2 built-in test page at %d baud...\n",
         s_printer.baudrate());
  if (!s_printer.print_test_page()) {
    printf("CSN-A2 test-page command failed\n");
    return 1;
  }
  printf("CSN-A2 test-page command sent\n");
  return 0;
}

int cmd_printer_status(int, char **) {
  DataGuard guard;
  if (!require_printer_interface()) return 1;
  if (!s_printer.initialize()) {
    printf("CSN-A2 UART initialization failed\n");
    return 1;
  }
  std::uint8_t status = 0;
  if (!s_printer.query_status(status)) {
    printf("No CSN-A2 status byte at %d baud. This alone does not prove "
           "the printer RX path failed; check the interface type and "
           "printer TXD to ESP32 RX path.\n",
           s_printer.baudrate());
    return 1;
  }
  printf("CSN-A2 status=0x%02X online=%s paper_out=%s over_voltage=%s "
         "over_temp=%s\n",
         status, (status & 0x01) ? "yes" : "no",
         (status & 0x04) ? "yes" : "no",
         (status & 0x08) ? "yes" : "no",
         (status & 0x40) ? "yes" : "no");
  return 0;
}

int cmd_printer_baud(int argc, char **argv) {
  DataGuard guard;
  if (argc != 2) {
    printf("usage: printer_baud <baudrate>\n");
    return 1;
  }
  char *end = nullptr;
  const long value = std::strtol(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || value < 1200 || value > 921600) {
    printf("baudrate must be an integer from 1200 to 921600\n");
    return 1;
  }
  if (!s_printer.set_baudrate(static_cast<int>(value))) {
    printf("failed to set printer UART baudrate\n");
    return 1;
  }
  printf("Printer UART is now %d baud; the CSN-A2 must use the same rate\n",
         s_printer.baudrate());
  return 0;
}

int cmd_print_last(int, char **) {
  DataGuard guard;
  if (!require_printer_interface()) return 1;
  if (!s_bitmap.ready()) {
    printf("No validated TPB1 bitmap in PSRAM; run ask or show_last first\n");
    return 1;
  }
  if (!s_printer.initialize()) {
    printf("CSN-A2 UART initialization failed\n");
    return 1;
  }
  const auto &document = s_bitmap.document();
  constexpr std::uint16_t kRowsPerTransfer = 256;
  const std::size_t row_bytes = document.width / 8;
  for (std::uint32_t y = 0; y < document.height;) {
    const auto rows = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(kRowsPerTransfer, document.height - y));
    if (!s_printer.print_rows(s_bitmap.row(y), row_bytes * rows, rows)) {
      s_printer.cancel();
      printf("CSN-A2 print failed at y=%lu\n", static_cast<unsigned long>(y));
      return 1;
    }
    y += rows;
  }
  if (!s_printer.feed(s_settings.render.bottom_feed)) {
    printf("CSN-A2 feed failed\n");
    return 1;
  }
  printf("Printed %lu rows\n", static_cast<unsigned long>(document.height));
  return 0;
}

int cmd_oled_test(int, char **) {
  DataGuard guard;
  if (!s_oled.ready() && !s_oled.initialize()) {
    printf("SSD1306 not found; check SDA/SCL, 3.3V and I2C address\n");
    return 1;
  }
  s_oled.show_test_pattern();
  printf("SSD1306 test pattern displayed\n");
  return 0;
}

int cmd_i2c_scan(int, char **) {
  DataGuard guard;
  const auto addresses = s_oled.scan_addresses();
  printf("I2C scan GPIO%d/GPIO%d: %u responding address(es)",
         thermal_terminal::board_config::kOledSdaGpio,
         thermal_terminal::board_config::kOledSclGpio,
         static_cast<unsigned>(addresses.size()));
  for (const auto address : addresses) {
    printf(" 0x%02X", address);
  }
  printf("\n");
  return 0;
}

int cmd_preview(int argc, char **argv) {
  DataGuard guard;
  if (!s_bitmap.ready()) {
    printf("No validated TPB1 bitmap in PSRAM; run ask first\n");
    return 1;
  }
  if (!s_oled.ready() && !s_oled.initialize()) {
    printf("SSD1306 not found\n");
    return 1;
  }
  if (argc != 1 && argc != 3) {
    printf("usage: preview [x y]\n");
    return 1;
  }
  unsigned long x = 0;
  unsigned long y = 0;
  if (argc == 3) {
    char *x_end = nullptr;
    char *y_end = nullptr;
    x = std::strtoul(argv[1], &x_end, 10);
    y = std::strtoul(argv[2], &y_end, 10);
    if (*x_end != '\0' || *y_end != '\0') {
      printf("preview coordinates must be decimal integers\n");
      return 1;
    }
  }
  const auto &doc = s_bitmap.document();
  if (x > doc.width - 128 || y >= doc.height) {
    printf("preview position out of range; x: 0..%u, y: 0..%lu\n",
           doc.width - 128, static_cast<unsigned long>(doc.height - 1));
    return 1;
  }
  s_oled.draw_bitmap_window(s_bitmap.data(), s_bitmap.size(), doc.width,
                            doc.height, static_cast<std::uint16_t>(x), y);
  s_oled.present();
  printf("OLED preview x=%lu y=%lu\n", x, y);
  return 0;
}

int cmd_ir_scan(int, char **) {
  xSemaphoreTake(s_ir_mutex, portMAX_DELAY);
  if (!s_ir.initialize()) {
    printf("1838B/RMT initialization failed; check IR OUT GPIO\n");
    xSemaphoreGive(s_ir_mutex);
    return 1;
  }
  printf("Press NEC remote keys for 10 seconds...\n");
  fflush(stdout);
  unsigned count = 0;
  for (unsigned elapsed = 0; elapsed < 10000; elapsed += 250) {
    thermal_terminal::NecFrame frame;
    if (s_ir.read(frame, 250)) {
      thermal_terminal::KeyEvent key_event;
      const bool mapped = thermal_terminal::map_nec_key(frame, key_event);
      printf("NEC address=0x%04X command=0x%02X repeat=%s\n", frame.address,
             frame.command, frame.repeat ? "yes" : "no");
      printf("  mapped_key=%s\n",
             mapped ? thermal_terminal::logical_key_name(key_event.key)
                    : "unassigned");
      ++count;
    }
  }
  printf("NEC scan complete: %u frames\n", count);
  xSemaphoreGive(s_ir_mutex);
  return 0;
}

int cmd_ir_map(int, char **) {
  std::size_t count = 0;
  const auto *bindings = thermal_terminal::remote_key_bindings(count);
  printf("NEC address filter: 0x%04X\n",
         thermal_terminal::board_config::kRemoteAddress);
  for (std::size_t index = 0; index < count; ++index) {
    printf("command=0x%02X remote=%s logical=%s\n", bindings[index].command,
           bindings[index].name,
           thermal_terminal::logical_key_name(bindings[index].key));
  }
  return 0;
}

int cmd_history(int, char **) {
  DataGuard guard;
  if (!require_network())
    return 1;
  if (s_session.session_id.empty()) {
    printf("No session ID yet\n");
    return 1;
  }
  thermal_terminal::HttpTerminalClient client(s_network);
  std::vector<thermal_terminal::HistoryMessage> messages;
  std::string next_cursor;
  const auto result =
      client.fetch_history(s_session.session_id, "", messages, next_cursor);
  if (!result.ok) {
    print_error(result);
    return 1;
  }
  printf("History: %u messages, older cursor: %s\n",
         static_cast<unsigned>(messages.size()),
         next_cursor.empty() ? "none" : next_cursor.c_str());
  for (const auto &message : messages) {
    printf("%lu %s %s (%.80s)\n", static_cast<unsigned long>(message.sequence),
           message.assistant ? "assistant" : "user", message.id.c_str(),
           message.content.c_str());
  }
  return 0;
}

void handle_ui_action(thermal_terminal::UiAction action) {
  using thermal_terminal::UiAction;
  if (action == UiAction::kNone)
    return;
  if (action == UiAction::kSaveSettings) {
    if (!s_store.save_user(s_settings)) s_ui.show_notice("SAVE FAILED");
    return;
  }
  if (action == UiAction::kLoadModels) {
    if (s_models.empty() && s_wifi.connected()) load_models();
    return;
  }
  if (action == UiAction::kScanWifi) {
    s_ui.show_notice("WIFI SCANNING");
    draw_ui();
    std::vector<thermal_terminal::WifiAccessPoint> access_points;
    if (!s_wifi.scan(access_points)) {
      s_ui.show_notice("WIFI SCAN FAILED");
      return;
    }
    std::vector<thermal_terminal::WifiNetworkOption> networks;
    networks.reserve(access_points.size());
    for (auto &point : access_points) {
      networks.push_back(
          {std::move(point.ssid), point.rssi, point.secured});
    }
    s_ui.set_wifi_networks(std::move(networks));
    return;
  }
  if (action == UiAction::kConnectWifi) {
    if (s_wifi_setup_pending)
      return;
    s_wifi_previous_network = s_network;
    s_wifi_pending_network = s_network;
    s_wifi_pending_network.ssid = s_ui.wifi_ssid();
    s_wifi_pending_network.password = s_ui.wifi_password();
    if (!s_wifi.connect(s_wifi_pending_network)) {
      s_ui.finish_wifi_setup(false);
      return;
    }
    s_wifi_setup_pending = true;
    s_wifi_setup_started_ms =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    s_ui.show_notice("WIFI CONNECTING");
    return;
  }
  if (action == UiAction::kPrint) {
    if (!thermal_terminal::board_config::kPrinterInterfaceReady) {
      s_ui.show_notice("PRINTER NOT READY");
      return;
    }
    if (cmd_print_last(0, nullptr) != 0) s_ui.show_notice("PRINT FAILED");
    return;
  }
  if (action == UiAction::kSend) {
    if (!require_network()) {
      s_ui.show_notice("WIFI OFFLINE");
      return;
    }
    s_ui.show_notice("SENDING...");
    draw_ui();
    const std::string prompt = s_ui.draft();
    char command[] = "ask";
    char *args[] = {command, const_cast<char *>(prompt.c_str())};
    if (cmd_ask(2, args) == 0) {
      if (s_settings.printer.auto_print && cmd_print_last(0, nullptr) != 0)
        printf("Automatic printing failed; reply remains available\n");
      s_ui.clear_draft();
      s_ui.show_preview();
    } else {
      s_ui.show_notice("SEND FAILED");
    }
    return;
  }
  if (action == UiAction::kRetry) {
    if (!require_network() || s_session.session_id.empty()) {
      s_ui.show_notice("NO MESSAGE TO RETRY");
      return;
    }
    s_ui.show_notice("RETRYING...");
    draw_ui();
    s_bitmap.reset();
    thermal_terminal::RequestOptions request;
    request.session_id = s_session.session_id;
    request.model_id = s_settings.model_id;
    request.reasoning_level = s_settings.reasoning_level;
    request.use_context = s_settings.use_context;
    request.render = s_settings.render;
    thermal_terminal::HttpTerminalClient client(s_network);
    thermal_terminal::BinaryResponseMetadata metadata;
    const auto result = client.retry(request, s_bitmap, metadata);
    if (result.ok && s_bitmap.ready()) {
      if (!metadata.session_id.empty() &&
          metadata.session_id != s_session.session_id) {
        s_session.session_id = metadata.session_id;
        s_store.save_session(s_session);
      }
      if (s_settings.printer.auto_print && cmd_print_last(0, nullptr) != 0)
        printf("Automatic retry printing failed; reply remains available\n");
      s_ui.show_preview();
    } else {
      print_error(result);
      s_bitmap.reset();
      s_ui.show_notice(result.error_code == "NO_MESSAGE_TO_RETRY"
                           ? "NO MESSAGE TO RETRY"
                           : "RETRY FAILED");
    }
    return;
  }
  if (action == UiAction::kLoadHistory || action == UiAction::kLoadOlderHistory) {
    if (!require_network() || s_session.session_id.empty()) {
      s_ui.show_notice("NO SESSION");
      return;
    }
    const bool append = action == UiAction::kLoadOlderHistory;
    const std::string before = append ? s_ui.next_cursor() : "";
    s_ui.show_notice("LOADING...");
    draw_ui();
    thermal_terminal::HttpTerminalClient client(s_network);
    std::vector<thermal_terminal::HistoryMessage> messages;
    std::string next_cursor;
    const auto result = client.fetch_history(s_session.session_id, before,
                                             messages, next_cursor);
    if (result.ok) {
      s_ui.set_history(std::move(messages), std::move(next_cursor), append);
      s_ui.return_from_notice();
    } else {
      print_error(result);
      s_ui.show_notice("HISTORY FAILED");
    }
    return;
  }
  if (action == UiAction::kRenderHistory) {
    const auto *message = s_ui.selected_message();
    if (message == nullptr || !message->assistant) return;
    const std::string message_id = message->id;
    s_ui.show_notice("RENDERING...");
    draw_ui();
    s_bitmap.reset();
    thermal_terminal::HttpTerminalClient client(s_network);
    thermal_terminal::BinaryResponseMetadata metadata;
    const auto result = client.render_message(s_session.session_id, message_id,
                                              s_settings.render, s_bitmap, metadata);
    if (result.ok && s_bitmap.ready()) s_ui.show_preview();
    else {
      print_error(result);
      s_ui.show_notice("RENDER FAILED");
    }
    return;
  }
  if (action == UiAction::kClearHistory) {
    if (!require_network()) {
      s_ui.show_notice("WIFI OFFLINE");
      return;
    }
    if (s_session.session_id.empty()) {
      s_ui.show_notice("NO SESSION");
      return;
    }
    s_ui.show_notice("CLEARING...");
    draw_ui();
    thermal_terminal::HttpTerminalClient client(s_network);
    std::uint32_t deleted = 0;
    const auto result = client.clear_history(s_session.session_id, deleted);
    if (result.ok) {
      s_ui.set_history({}, "", false);
      s_bitmap.reset();
      s_ui.go_home();
      s_ui.show_notice("HISTORY CLEARED");
    } else {
      print_error(result);
      s_ui.show_notice("CLEAR FAILED");
    }
  }
}

void ui_task(void *) {
  if (!s_ir.initialize())
    ESP_LOGW(kTag, "IR receiver unavailable; console remains active");
  bool wifi_connected = s_wifi.connected();
  for (;;) {
    thermal_terminal::NecFrame frame;
    bool received = false;
    if (s_ir_mutex != nullptr && xSemaphoreTake(s_ir_mutex, 0) == pdTRUE) {
      received = s_ir.read(frame, 80);
      xSemaphoreGive(s_ir_mutex);
    }
    {
      DataGuard guard;
      const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
      s_ui.tick(now_ms);
      if (received) {
        thermal_terminal::KeyEvent event;
        if (thermal_terminal::map_nec_key(frame, event)) {
          const auto action = s_ui.on_key(
              event, now_ms, s_settings, s_models, s_bitmap.ready(),
              s_bitmap.ready() ? s_bitmap.document().height : 0);
          handle_ui_action(action);
        }
      }
      if (s_wifi_setup_pending) {
        if (s_wifi.connected()) {
          if (s_store.save_network(s_wifi_pending_network)) {
            s_network = s_wifi_pending_network;
            s_wifi_setup_pending = false;
            s_ui.finish_wifi_setup(true);
          } else {
            s_wifi.disconnect();
            if (!s_wifi_previous_network.ssid.empty())
              s_wifi.connect(s_wifi_previous_network);
            s_wifi_setup_pending = false;
            s_ui.show_notice("SAVE FAILED");
          }
        } else if (now_ms >= s_wifi_setup_started_ms + 25000) {
          s_wifi.disconnect();
          if (!s_wifi_previous_network.ssid.empty())
            s_wifi.connect(s_wifi_previous_network);
          s_wifi_setup_pending = false;
          s_ui.finish_wifi_setup(false);
        }
      }
      const bool connected = s_wifi.connected();
      if (s_ui.dirty() || connected != wifi_connected) draw_ui();
      wifi_connected = connected;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

int cmd_reboot(int, char **) {
  printf("restarting\n");
  fflush(stdout);
  esp_restart();
  return 0;
}

bool start_console(RuntimeContext &runtime) {
  s_runtime = &runtime;
  const esp_console_cmd_t status_command = {
      .command = "status",
      .help = "show Wi-Fi and Worker configuration status",
      .hint = nullptr,
      .func = &cmd_status,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t wifi_command = {.command = "set_wifi",
                                          .help =
                                              "save Wi-Fi SSID and password",
                                          .hint = "<ssid> <password>",
                                          .func = &cmd_set_wifi,
                                          .argtable = nullptr,
                                          .func_w_context = nullptr,
                                          .context = nullptr};
  const esp_console_cmd_t worker_command = {
      .command = "set_worker",
      .help = "save Worker URL and terminal token",
      .hint = "<worker_url> <terminal_token>",
      .func = &cmd_set_worker,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t reboot_command = {.command = "reboot",
                                            .help = "restart the terminal",
                                            .hint = nullptr,
                                            .func = &cmd_reboot,
                                            .argtable = nullptr,
                                            .func_w_context = nullptr,
                                            .context = nullptr};
  const esp_console_cmd_t models_command = {
      .command = "models",
      .help = "fetch available models from Worker",
      .hint = nullptr,
      .func = &cmd_models,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t ask_command = {
      .command = "ask",
      .help = "send a short message and validate TPB1 in PSRAM",
      .hint = "<message>",
      .func = &cmd_ask,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t history_command = {
      .command = "history",
      .help = "show the latest D1 history page",
      .hint = nullptr,
      .func = &cmd_history,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t oled_test_command = {
      .command = "oled_test",
      .help = "show SSD1306 border and diagonal test pattern",
      .hint = nullptr,
      .func = &cmd_oled_test,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t i2c_scan_command = {
      .command = "i2c_scan",
      .help = "scan SSD1306 I2C bus for responding 7-bit addresses",
      .hint = nullptr,
      .func = &cmd_i2c_scan,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t preview_command = {
      .command = "preview",
      .help = "show validated TPB1 bitmap window on OLED",
      .hint = "[x y]",
      .func = &cmd_preview,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t show_last_command = {
      .command = "show_last",
      .help = "render the latest saved assistant reply to the OLED",
      .hint = nullptr,
      .func = &cmd_show_last,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t ir_scan_command = {
      .command = "ir_scan",
      .help = "scan 1838B NEC remote codes for 10 seconds",
      .hint = nullptr,
      .func = &cmd_ir_scan,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t ir_map_command = {
      .command = "ir_map",
      .help = "print the current NEC command mapping",
      .hint = nullptr,
      .func = &cmd_ir_map,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t printer_test_command = {
      .command = "printer_test",
      .help = "print a CSN-A2 hardware test pattern",
      .hint = nullptr,
      .func = &cmd_printer_test,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t printer_text_command = {
      .command = "printer_text",
      .help = "print an ASCII CSN-A2 UART diagnostic line",
      .hint = nullptr,
      .func = &cmd_printer_text,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t printer_selftest_command = {
      .command = "printer_selftest",
      .help = "request the CSN-A2 built-in test page (DC2 T)",
      .hint = nullptr,
      .func = &cmd_printer_selftest,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t printer_status_command = {
      .command = "printer_status",
      .help = "read the CSN-A2 status byte (ESC v 0)",
      .hint = nullptr,
      .func = &cmd_printer_status,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t printer_baud_command = {
      .command = "printer_baud",
      .help = "change the printer UART baudrate for diagnostics",
      .hint = "<baudrate>",
      .func = &cmd_printer_baud,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  const esp_console_cmd_t print_last_command = {
      .command = "print_last",
      .help = "print the validated bitmap currently in PSRAM",
      .hint = nullptr,
      .func = &cmd_print_last,
      .argtable = nullptr,
      .func_w_context = nullptr,
      .context = nullptr};
  if (esp_console_cmd_register(&status_command) != ESP_OK ||
      esp_console_cmd_register(&wifi_command) != ESP_OK ||
      esp_console_cmd_register(&worker_command) != ESP_OK ||
      esp_console_cmd_register(&reboot_command) != ESP_OK ||
      esp_console_cmd_register(&models_command) != ESP_OK ||
      esp_console_cmd_register(&ask_command) != ESP_OK ||
      esp_console_cmd_register(&history_command) != ESP_OK ||
      esp_console_cmd_register(&oled_test_command) != ESP_OK ||
      esp_console_cmd_register(&i2c_scan_command) != ESP_OK ||
      esp_console_cmd_register(&preview_command) != ESP_OK ||
      esp_console_cmd_register(&show_last_command) != ESP_OK ||
      esp_console_cmd_register(&ir_scan_command) != ESP_OK ||
      esp_console_cmd_register(&ir_map_command) != ESP_OK ||
      esp_console_cmd_register(&printer_test_command) != ESP_OK ||
      esp_console_cmd_register(&printer_text_command) != ESP_OK ||
      esp_console_cmd_register(&printer_selftest_command) != ESP_OK ||
      esp_console_cmd_register(&printer_status_command) != ESP_OK ||
      esp_console_cmd_register(&printer_baud_command) != ESP_OK ||
      esp_console_cmd_register(&print_last_command) != ESP_OK) {
    return false;
  }

  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "terminal> ";
  repl_config.max_cmdline_length = 512;
  repl_config.task_stack_size = 16384;
  esp_console_dev_usb_serial_jtag_config_t hardware_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  esp_console_repl_t *repl = nullptr;
  if (esp_console_new_repl_usb_serial_jtag(&hardware_config, &repl_config,
                                           &repl) != ESP_OK) {
    return false;
  }
  esp_console_register_help_command();
  return esp_console_start_repl(repl) == ESP_OK;
}
} // namespace

extern "C" void app_main() {
  ESP_LOGI(kTag, "firmware boot");
  if (!s_store.initialize() ||
      !s_store.load(s_settings, s_network, s_session)) {
    ESP_LOGE(kTag, "NVS settings initialization failed");
  }
  s_ui.set_dictionary(&s_dictionary);
  if (!s_dictionary.initialize())
    ESP_LOGW(kTag, "English T9 dictionary unavailable");
  ESP_LOGI(kTag, "auto print: %s",
           s_settings.printer.auto_print ? "on" : "off");
  ESP_LOGI(kTag, "initial state: %s",
           thermal_terminal::app_state_name(thermal_terminal::AppState::kBoot));

  if (!s_wifi.initialize()) {
    ESP_LOGE(kTag, "Wi-Fi initialization failed");
  } else if (s_network.ssid.empty()) {
    ESP_LOGW(kTag, "Wi-Fi is not configured yet");
  } else if (!s_wifi.connect(s_network)) {
    ESP_LOGE(kTag, "Wi-Fi connection could not be started");
  }

  s_data_mutex = xSemaphoreCreateRecursiveMutex();
  s_ir_mutex = xSemaphoreCreateMutex();
  if (s_data_mutex == nullptr || s_ir_mutex == nullptr) {
    ESP_LOGE(kTag, "UI mutex initialization failed");
    return;
  }
  if (!start_console(s_context)) {
    ESP_LOGE(kTag, "console initialization failed");
  }

  if (s_oled.initialize()) draw_ui();
  if (xTaskCreate(&ui_task, "terminal_ui", 20480, nullptr, 4, nullptr) != pdPASS)
    ESP_LOGE(kTag, "UI task creation failed");
}
