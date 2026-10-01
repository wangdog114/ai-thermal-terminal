#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "interfaces/input.hpp"
#include "interfaces/terminal_client.hpp"
#include "oled_display/bitmap_window.hpp"

namespace thermal_terminal {

class EnglishDictionary;

enum class InputMode : std::uint8_t {
  kUpper,
  kLower,
  kNumeric,
  kStroke,
  kDictionary,
};

enum class UiScreen : std::uint8_t {
  kHome,
  kCompose,
  kHistory,
  kHistoryText,
  kSettings,
  kWifiNetworks,
  kWifiPassword,
  kPreview,
  kConfirmClear,
  kNotice
};

enum class UiAction : std::uint8_t {
  kNone,
  kLoadHistory,
  kLoadOlderHistory,
  kLoadModels,
  kSend,
  kRetry,
  kRenderHistory,
  kPrint,
  kClearHistory,
  kSaveSettings,
  kScanWifi,
  kConnectWifi
};

struct WifiNetworkOption {
  std::string ssid;
  std::int8_t rssi{};
  bool secured{};
};

class TerminalUi {
public:
  UiAction on_key(const KeyEvent &event, std::uint64_t now_ms,
                  UserSettings &settings, const std::vector<ModelInfo> &models,
                  bool bitmap_ready, std::uint32_t bitmap_height);
  void set_dictionary(EnglishDictionary *dictionary) { dictionary_ = dictionary; }
  void set_wifi_networks(std::vector<WifiNetworkOption> networks);
  void finish_wifi_setup(bool connected);
  void set_history(std::vector<HistoryMessage> messages,
                   std::string next_cursor, bool append);
  void show_preview();
  void clear_draft();
  void go_home();
  void show_notice(const char *message);
  void return_from_notice();
  void tick(std::uint64_t now_ms);
  void render(BitmapWindow &canvas, bool wifi_connected,
              const UserSettings &settings,
              const std::vector<ModelInfo> &models, bool bitmap_ready) const;

  [[nodiscard]] UiScreen screen() const { return screen_; }
  [[nodiscard]] const std::string &draft() const { return draft_; }
  [[nodiscard]] std::size_t cursor() const { return cursor_; }
  [[nodiscard]] InputMode input_mode() const { return input_mode_; }
  [[nodiscard]] bool symbol_panel() const { return symbol_panel_; }
  [[nodiscard]] const std::string &stroke_sequence() const { return stroke_sequence_; }
  [[nodiscard]] const std::string &dictionary_sequence() const {
    return dictionary_sequence_;
  }
  [[nodiscard]] std::size_t dictionary_candidate_count() const {
    return dictionary_candidates_.size();
  }
  [[nodiscard]] const std::string &dictionary_candidate(std::size_t index) const;
  [[nodiscard]] std::size_t stroke_candidate_count() const { return stroke_candidates_.size(); }
  [[nodiscard]] std::size_t stroke_candidate_exact_count() const {
    return stroke_candidate_exact_count_;
  }
  [[nodiscard]] std::size_t stroke_candidate_page() const { return stroke_candidate_page_; }
  [[nodiscard]] std::size_t stroke_candidate_selection() const { return stroke_candidate_selection_; }
  [[nodiscard]] const char *stroke_candidate_text(std::size_t index) const;
  [[nodiscard]] const std::string &wifi_ssid() const { return wifi_ssid_; }
  [[nodiscard]] const std::string &wifi_password() const { return wifi_password_; }
  [[nodiscard]] bool multi_tap_pending() const {
    return pending_key_ != LogicalKey::kBack;
  }
  [[nodiscard]] const HistoryMessage *selected_message() const;
  [[nodiscard]] const std::string &next_cursor() const { return next_cursor_; }
  [[nodiscard]] std::uint16_t preview_x() const { return preview_x_; }
  [[nodiscard]] std::uint32_t preview_y() const { return preview_y_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  void mark_clean() { dirty_ = false; }

private:
  void handle_digit(LogicalKey key, std::uint64_t now_ms);
  void commit_pending();
  void move_cursor(std::ptrdiff_t delta);
  void backspace();
  void insert_text(char ch);
  void switch_input_mode();
  void open_symbol_panel();
  void handle_symbol_key(LogicalKey key);
  void update_stroke_candidates();
  void handle_stroke_key(LogicalKey key);
  void accept_stroke_candidate();
  void clear_stroke_composition();
  void update_dictionary_candidates(std::uint64_t now_ms);
  void handle_dictionary_key(LogicalKey key, std::uint64_t now_ms);
  void clear_dictionary_composition();
  void insert_text(const char *text);
  [[nodiscard]] bool is_text_entry() const;
  std::string &active_text();
  std::size_t &active_cursor();
  void pan(LogicalKey key, std::uint32_t bitmap_height);

  UiScreen screen_{UiScreen::kHome};
  UiScreen notice_return_{UiScreen::kHome};
  std::uint8_t selected_{};
  std::uint8_t settings_selected_{};
  std::size_t settings_name_scroll_{};
  std::uint64_t settings_name_scroll_at_{};
  std::size_t history_selected_{};
  std::size_t history_text_scroll_{};
  std::vector<HistoryMessage> history_;
  std::vector<WifiNetworkOption> wifi_networks_;
  std::size_t wifi_network_selected_{};
  std::string wifi_ssid_;
  std::string wifi_password_;
  std::size_t wifi_password_cursor_{};
  std::string next_cursor_;
  std::string draft_;
  std::size_t cursor_{};
  std::string notice_;
  std::string stroke_sequence_;
  std::vector<std::uint16_t> stroke_candidates_;
  std::size_t stroke_candidate_exact_count_{};
  std::size_t stroke_candidate_page_{};
  std::size_t stroke_candidate_selection_{};
  std::string dictionary_sequence_;
  std::vector<std::string> dictionary_candidates_;
  std::size_t dictionary_candidate_page_{};
  std::size_t dictionary_candidate_selection_{};
  std::size_t dictionary_scroll_{};
  std::uint64_t dictionary_scroll_at_{};
  EnglishDictionary *dictionary_{};
  LogicalKey pending_key_{LogicalKey::kBack};
  std::size_t pending_position_{};
  std::uint64_t pending_at_{};
  std::uint8_t pending_index_{};
  InputMode input_mode_{InputMode::kUpper};
  bool symbol_panel_{};
  std::size_t symbol_selected_{};
  bool cursor_visible_{true};
  std::uint64_t last_cursor_blink_{};
  std::uint16_t preview_x_{};
  std::uint32_t preview_y_{};
  bool clear_yes_{};
  bool retry_latched_{};
  bool send_pressed_{};
  std::uint64_t send_pressed_at_{};
  bool dirty_{true};
};

} // namespace thermal_terminal
