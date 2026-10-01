#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "nec_input/nec_decoder.hpp"
#include "nec_input/remote_keymap.hpp"
#include "oled_display/bitmap_window.hpp"
#include "terminal_ui/terminal_ui.hpp"
#include "board_config/board_config.hpp"
#include "english_dictionary/english_dictionary.hpp"

using thermal_terminal::BitmapWindow;
using thermal_terminal::NecDecoder;
using thermal_terminal::NecFrame;
using thermal_terminal::NecPulse;

void set_tpb_pixel(std::vector<std::uint8_t> &bitmap, std::uint16_t x,
                   std::uint16_t y) {
  bitmap[static_cast<std::size_t>(y) * 48 + x / 8] |=
      static_cast<std::uint8_t>(0x80U >> (x & 7));
}

void test_preview_window() {
  std::vector<std::uint8_t> bitmap(48 * 72, 0);
  set_tpb_pixel(bitmap, 0, 0);
  set_tpb_pixel(bitmap, 127, 7);
  set_tpb_pixel(bitmap, 128, 8);
  set_tpb_pixel(bitmap, 383, 63);
  set_tpb_pixel(bitmap, 256, 64);

  BitmapWindow window;
  assert(window.draw_tpb_window(bitmap.data(), bitmap.size(), 384, 72, 0, 0));
  assert(window.data()[0] == 0x01);
  assert(window.data()[127] == 0x80);
  assert(window.data()[128] == 0);

  assert(window.draw_tpb_window(bitmap.data(), bitmap.size(), 384, 72, 128, 0));
  assert(window.data()[128] == 0x01);
  assert(window.data()[0] == 0);

  assert(window.draw_tpb_window(bitmap.data(), bitmap.size(), 384, 72, 256, 0));
  assert(window.data()[7 * 128 + 127] == 0x80);
  assert(window.data()[0] == 0);

  assert(
      window.draw_tpb_window(bitmap.data(), bitmap.size(), 384, 72, 256, 64));
  assert(window.data()[0] == 0x01);
  assert(window.data()[128] == 0);

  assert(!window.draw_tpb_window(bitmap.data(), 2, 384, 72, 0, 0));
  assert(window.data()[0] == 0);
  assert(
      !window.draw_tpb_window(bitmap.data(), bitmap.size(), 384, 72, 257, 0));
}

std::array<NecPulse, 34> make_nec_frame(std::uint32_t bits) {
  std::array<NecPulse, 34> pulses{};
  pulses[0] = {9000, 4500};
  for (unsigned index = 0; index < 32; ++index) {
    pulses[index + 1] = {
        560, static_cast<std::uint16_t>((bits & (1UL << index)) ? 1690 : 560)};
  }
  pulses[33] = {560, 0};
  return pulses;
}

void test_nec_frames() {
  NecDecoder decoder;
  NecFrame frame;
  auto full = make_nec_frame(0xED12CB34U);
  assert(decoder.decode(full.data(), full.size(), 100, frame));
  assert(frame.address == 0x34 && frame.command == 0x12 && !frame.repeat);

  const std::array<NecPulse, 2> repeat{{{9000, 2250}, {560, 0}}};
  assert(decoder.decode(repeat.data(), repeat.size(), 210, frame));
  assert(frame.address == 0x34 && frame.command == 0x12 && frame.repeat);
  assert(!decoder.decode(repeat.data(), repeat.size(), 700, frame));

  full = make_nec_frame(0xED121234U);
  assert(decoder.decode(full.data(), full.size(), 800, frame));
  assert(frame.address == 0x1234 && frame.command == 0x12 && !frame.repeat);

  full = make_nec_frame(0xEC121234U);
  assert(!decoder.decode(full.data(), full.size(), 900, frame));
}

void test_remote_keymap() {
  std::size_t count = 0;
  const auto *bindings = thermal_terminal::remote_key_bindings(count);
  assert(count == thermal_terminal::board_config::kRemoteButtonCount);
  assert(count == 21);
  assert(bindings[0].command == 0x45);

  NecFrame frame{thermal_terminal::board_config::kRemoteAddress, 0x45, false};
  thermal_terminal::KeyEvent event;
  assert(thermal_terminal::map_nec_key(frame, event));
  assert(event.key == thermal_terminal::LogicalKey::kUp);
  assert(event.command == 0x45);
  assert(!event.repeat);

  frame = {0, 0x19, true};
  assert(thermal_terminal::map_nec_key(frame, event));
  assert(event.key == thermal_terminal::LogicalKey::kPrint);
  assert(event.long_press && event.repeat);

  frame = {0, 0x01, false};
  assert(!thermal_terminal::map_nec_key(frame, event));
  frame = {0x1234, 0x45, false};
  assert(!thermal_terminal::map_nec_key(frame, event));
}

void test_terminal_ui() {
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  event.key = thermal_terminal::LogicalKey::kConfirm;
  assert(ui.on_key(event, 0, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
  assert(ui.screen() == thermal_terminal::UiScreen::kCompose);
  event.key = thermal_terminal::LogicalKey::kDigit2;
  ui.on_key(event, 100, settings, models, false, 0);
  event.key = thermal_terminal::LogicalKey::kDigit2;
  ui.on_key(event, 200, settings, models, false, 0);
  assert(ui.draft() == "B");
  event.key = thermal_terminal::LogicalKey::kConfirm;
  assert(ui.on_key(event, 300, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
  assert(ui.draft() == "B");
  event.key = thermal_terminal::LogicalKey::kSend;
  assert(ui.on_key(event, 1000, settings, models, false, 0) ==
         thermal_terminal::UiAction::kSend);
  event.key = thermal_terminal::LogicalKey::kMenu;
  ui.on_key(event, 1100, settings, models, false, 0);
  assert(ui.screen() == thermal_terminal::UiScreen::kHome);

  event.key = thermal_terminal::LogicalKey::kDown;
  ui.on_key(event, 1200, settings, models, true, 200);
  event.key = thermal_terminal::LogicalKey::kDown;
  ui.on_key(event, 1300, settings, models, true, 200);
  event.key = thermal_terminal::LogicalKey::kDown;
  ui.on_key(event, 1400, settings, models, true, 200);
  event.key = thermal_terminal::LogicalKey::kConfirm;
  ui.on_key(event, 1500, settings, models, true, 200);
  assert(ui.screen() == thermal_terminal::UiScreen::kPreview);
  event.key = thermal_terminal::LogicalKey::kDigit6;
  ui.on_key(event, 1600, settings, models, true, 200);
  assert(ui.preview_x() == 32);
  event.key = thermal_terminal::LogicalKey::kRight;
  ui.on_key(event, 1650, settings, models, true, 200);
  assert(ui.preview_x() == 32);
  event.key = thermal_terminal::LogicalKey::kDigit6;
  for (int i = 0; i < 12; ++i)
    ui.on_key(event, 1700 + i * 100, settings, models, true, 200);
  assert(ui.preview_x() == 256);
  event.key = thermal_terminal::LogicalKey::kDigit8;
  for (int i = 0; i < 12; ++i)
    ui.on_key(event, 3000 + i * 100, settings, models, true, 200);
  assert(ui.preview_y() == 136);
  event.key = thermal_terminal::LogicalKey::kDown;
  ui.on_key(event, 4300, settings, models, true, 200);
  assert(ui.preview_y() == 136);
  event.key = thermal_terminal::LogicalKey::kDigit4;
  event.repeat = true;
  ui.on_key(event, 4400, settings, models, true, 200);
  assert(ui.preview_x() == 224);
  event.repeat = false;
  event.key = thermal_terminal::LogicalKey::kDigit2;
  ui.on_key(event, 4500, settings, models, true, 200);
  assert(ui.preview_y() == 104);
  event.key = thermal_terminal::LogicalKey::kClear;
  ui.on_key(event, 5000, settings, models, true, 200);
  assert(ui.screen() == thermal_terminal::UiScreen::kConfirmClear);
  event.key = thermal_terminal::LogicalKey::kConfirm;
  assert(ui.on_key(event, 5100, settings, models, true, 200) ==
         thermal_terminal::UiAction::kNone);

  BitmapWindow canvas;
  ui.render(canvas, true, settings, models, true);
  canvas.clear();
  canvas.draw_text(0, 0, "A");
  assert(canvas.data()[0] != 0);
  canvas.fill_rect(0, 0, 6, 8, false);
  assert(canvas.data()[0] == 0);

  ui.go_home();
  event.key = thermal_terminal::LogicalKey::kDown;
  ui.on_key(event, 6100, settings, models, false, 0);
  event.key = thermal_terminal::LogicalKey::kConfirm;
  assert(ui.on_key(event, 6300, settings, models, false, 0) ==
         thermal_terminal::UiAction::kLoadHistory);
  thermal_terminal::HistoryMessage message;
  message.id = "1";
  message.sequence = 1;
  message.content = "HELLO FROM HISTORY";
  ui.set_history({message}, "", false);
  ui.on_key(event, 6400, settings, models, false, 0);
  assert(ui.screen() == thermal_terminal::UiScreen::kHistoryText);
}

void test_retry_long_press() {
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  event.key = thermal_terminal::LogicalKey::kSend;
  event.long_press = false;
  event.repeat = false;
  assert(ui.on_key(event, 0, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
  event.long_press = true;
  event.repeat = true;
  assert(ui.on_key(event, 100, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
  assert(ui.on_key(event, 799, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
  assert(ui.on_key(event, 800, settings, models, false, 0) ==
         thermal_terminal::UiAction::kRetry);
  assert(ui.on_key(event, 900, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
  event.long_press = false;
  event.repeat = false;
  assert(ui.on_key(event, 300, settings, models, false, 0) ==
         thermal_terminal::UiAction::kNone);
}

void test_dictionary_input() {
  thermal_terminal::EnglishDictionary dictionary;
  assert(dictionary.initialize("../../assets/en.t9"));
  std::vector<std::string> broad_candidates;
  assert(dictionary.query("4", broad_candidates));
  assert(!broad_candidates.empty());
  assert(dictionary.query("43", broad_candidates));
  assert(!broad_candidates.empty());
  thermal_terminal::TerminalUi ui;
  ui.set_dictionary(&dictionary);
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](thermal_terminal::LogicalKey key, std::uint64_t at) {
    event.key = key;
    event.command = 0;
    event.long_press = false;
    event.repeat = false;
    return ui.on_key(event, at, settings, models, false, 0);
  };
  press(thermal_terminal::LogicalKey::kConfirm, 0);
  for (int i = 0; i < 4; ++i)
    press(thermal_terminal::LogicalKey::kPrint, 10 + i);
  assert(ui.input_mode() == thermal_terminal::InputMode::kDictionary);
  press(thermal_terminal::LogicalKey::kDigit4, 30);
  press(thermal_terminal::LogicalKey::kDigit3, 40);
  press(thermal_terminal::LogicalKey::kDigit5, 50);
  press(thermal_terminal::LogicalKey::kDigit5, 60);
  press(thermal_terminal::LogicalKey::kDigit6, 70);
  assert(ui.dictionary_candidate_count() > 0);
  bool found_hello = false;
  for (std::size_t i = 0; i < ui.dictionary_candidate_count(); ++i)
    found_hello = found_hello || ui.dictionary_candidate(i) == "hello";
  assert(found_hello);
  press(thermal_terminal::LogicalKey::kConfirm, 80);
  assert(ui.draft() == "hello");
}

void test_dictionary_candidate_scroll() {
  thermal_terminal::EnglishDictionary dictionary;
  assert(dictionary.initialize("../../assets/en.t9"));
  thermal_terminal::TerminalUi ui;
  ui.set_dictionary(&dictionary);
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](thermal_terminal::LogicalKey key, std::uint64_t at) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    ui.on_key(event, at, settings, models, false, 0);
  };
  press(thermal_terminal::LogicalKey::kConfirm, 0);
  for (int i = 0; i < 4; ++i)
    press(thermal_terminal::LogicalKey::kPrint, 10 + i);

  constexpr char digits[] = "46837628466254928466";
  std::uint64_t at = 100;
  for (const char digit : digits) {
    if (digit == '\0') break;
    press(static_cast<thermal_terminal::LogicalKey>(
              static_cast<unsigned>(thermal_terminal::LogicalKey::kDigit0) +
              static_cast<unsigned>(digit - '0')),
          at);
    at += 10;
  }
  assert(ui.dictionary_candidate_count() >= 1);
  assert(ui.dictionary_candidate(0) == "internationalization");

  BitmapWindow canvas;
  ui.render(canvas, true, settings, models, false);
  BitmapWindow expected;
  expected.fill_rect(0, 48, 128, 16, true);
  expected.draw_utf8_text(0, 48, "internationa", false, 96);
  for (std::uint16_t y = 48; y < 64; ++y) {
    for (std::uint16_t x = 0; x < 96; ++x) {
      const auto offset = static_cast<std::size_t>(y / 8) * 128 + x;
      const auto mask = static_cast<std::uint8_t>(1U << (y & 7));
      assert((canvas.data()[offset] & mask) == (expected.data()[offset] & mask));
    }
  }

  for (int i = 1; i <= 8; ++i)
    ui.tick(at - 10 + static_cast<std::uint64_t>(i) * 450);
  ui.render(canvas, true, settings, models, false);
  expected.clear();
  expected.fill_rect(0, 48, 128, 16, true);
  expected.draw_utf8_text(0, 48, "ionalization", false, 96);
  for (std::uint16_t y = 48; y < 64; ++y) {
    for (std::uint16_t x = 0; x < 96; ++x) {
      const auto offset = static_cast<std::size_t>(y / 8) * 128 + x;
      const auto mask = static_cast<std::uint8_t>(1U << (y & 7));
      assert((canvas.data()[offset] & mask) == (expected.data()[offset] & mask));
    }
  }
}

void test_dictionary_selection_crosses_pages() {
  using thermal_terminal::LogicalKey;
  thermal_terminal::EnglishDictionary dictionary;
  assert(dictionary.initialize("../../assets/en.t9"));

  for (const std::size_t target : {4U, 8U, 12U}) {
    thermal_terminal::TerminalUi ui;
    ui.set_dictionary(&dictionary);
    thermal_terminal::UserSettings settings;
    const std::vector<thermal_terminal::ModelInfo> models;
    thermal_terminal::KeyEvent event;
    auto press = [&](LogicalKey key) {
      event.key = key;
      event.command = 0;
      event.repeat = false;
      ui.on_key(event, 0, settings, models, false, 0);
    };
    press(LogicalKey::kConfirm);
    for (int i = 0; i < 4; ++i)
      press(LogicalKey::kPrint);
    press(LogicalKey::kDigit4);
    assert(ui.dictionary_candidate_count() > target);
    const std::string expected = ui.dictionary_candidate(target);
    for (std::size_t i = 0; i < target; ++i)
      press(LogicalKey::kDown);
    press(LogicalKey::kUp);
    press(LogicalKey::kDown);
    press(LogicalKey::kConfirm);
    assert(ui.draft() == expected);
  }

  thermal_terminal::TerminalUi ui;
  ui.set_dictionary(&dictionary);
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](LogicalKey key) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    ui.on_key(event, 0, settings, models, false, 0);
  };
  press(LogicalKey::kConfirm);
  for (int i = 0; i < 4; ++i)
    press(LogicalKey::kPrint);
  press(LogicalKey::kDigit4);
  const auto last = ui.dictionary_candidate(ui.dictionary_candidate_count() - 1);
  press(LogicalKey::kUp);
  press(LogicalKey::kConfirm);
  assert(ui.draft() == last);
}

void test_repeated_backspace_does_not_move_cursor() {
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  event.key = thermal_terminal::LogicalKey::kConfirm;
  ui.on_key(event, 0, settings, models, false, 0);
  event.key = thermal_terminal::LogicalKey::kDigit2;
  for (int i = 0; i < 5; ++i)
    ui.on_key(event, 1000 + i * 1000, settings, models, false, 0);
  assert(ui.draft() == "AAAAA" && ui.cursor() == 5);

  event.key = thermal_terminal::LogicalKey::kUp;
  event.command = thermal_terminal::board_config::kEditBackspaceCommand;
  ui.on_key(event, 6000, settings, models, false, 0);
  assert(ui.draft() == "AAAA" && ui.cursor() == 4);
  event.repeat = true;
  event.long_press = true;
  ui.on_key(event, 6100, settings, models, false, 0);
  assert(ui.draft() == "AAA" && ui.cursor() == 3);
  ui.on_key(event, 6200, settings, models, false, 0);
  assert(ui.draft() == "AA" && ui.cursor() == 2);
}

void test_compose_modes_and_cursor() {
  using thermal_terminal::InputMode;
  using thermal_terminal::LogicalKey;
  using thermal_terminal::UiAction;
  using thermal_terminal::UiScreen;
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](LogicalKey key, std::uint64_t at, std::uint8_t command = 0) {
    event.key = key;
    event.command = command;
    event.repeat = false;
    return ui.on_key(event, at, settings, models, false, 0);
  };
  press(LogicalKey::kConfirm, 0);
  assert(ui.screen() == UiScreen::kCompose);
  assert(ui.input_mode() == InputMode::kUpper);

  press(LogicalKey::kDigit0, 100);
  assert(ui.draft() == " " && ui.multi_tap_pending());
  press(LogicalKey::kDigit0, 200);
  assert(ui.draft() == "0" && ui.cursor() == 1);
  press(LogicalKey::kDigit1, 300);
  assert(ui.draft() == "0.");
  press(LogicalKey::kDigit1, 400);
  press(LogicalKey::kDigit1, 500);
  press(LogicalKey::kDigit1, 600);
  press(LogicalKey::kDigit1, 700);
  assert(ui.draft() == "01");
  ui.tick(1600);
  assert(!ui.multi_tap_pending());
  press(LogicalKey::kDigit1, 1700);
  assert(ui.draft() == "01.");

  press(LogicalKey::kPrint, 1800);
  assert(ui.input_mode() == InputMode::kLower);
  press(LogicalKey::kDigit2, 1900);
  assert(ui.draft() == "01.a");
  press(LogicalKey::kPrint, 2000);
  assert(ui.input_mode() == InputMode::kNumeric);
  press(LogicalKey::kDigit2, 2100);
  press(LogicalKey::kDigit2, 2200);
  assert(ui.draft() == "01.a22");
  press(LogicalKey::kPrint, 2300);
  assert(ui.input_mode() == InputMode::kStroke);
  press(LogicalKey::kPrint, 2350);
  assert(ui.input_mode() == InputMode::kDictionary);
  press(LogicalKey::kPrint, 2360);
  assert(ui.input_mode() == InputMode::kUpper);

  press(LogicalKey::kLeft, 2400);
  press(LogicalKey::kLeft, 2500);
  assert(ui.cursor() == 4);
  press(LogicalKey::kClear, 2600);
  assert(ui.symbol_panel());
  press(LogicalKey::kMenu, 2650);
  assert(!ui.symbol_panel() && ui.screen() == UiScreen::kCompose);
  press(LogicalKey::kClear, 2675);
  assert(ui.symbol_panel());
  press(LogicalKey::kClear, 2680,
        thermal_terminal::board_config::kEditSymbolsCommand);
  assert(!ui.symbol_panel() && ui.screen() == UiScreen::kCompose);
  press(LogicalKey::kClear, 2690);
  press(LogicalKey::kRight, 2700);
  press(LogicalKey::kConfirm, 2800);
  assert(!ui.symbol_panel());
  assert(ui.draft() == "01.a,22" && ui.cursor() == 5);
  press(LogicalKey::kUp, 2900, thermal_terminal::board_config::kEditBackspaceCommand);
  assert(ui.draft() == "01.a22" && ui.cursor() == 4);
  press(LogicalKey::kDown, 3000, thermal_terminal::board_config::kEditSpaceCommand);
  assert(ui.draft() == "01.a 22" && ui.cursor() == 5);
  press(LogicalKey::kSend, 3100);
  assert(ui.draft() == "01.a 22");
  press(LogicalKey::kMenu, 3200);
  assert(ui.screen() == UiScreen::kHome);

  ui.go_home();
  press(LogicalKey::kConfirm, 3300);
  ui.clear_draft();
  for (std::size_t i = 0; i < thermal_terminal::board_config::kMaxDraftLength; ++i)
    press(LogicalKey::kDigit2, 4000 + i * 1000);
  assert(ui.draft().size() == thermal_terminal::board_config::kMaxDraftLength);
  press(LogicalKey::kDigit2, 4000 + thermal_terminal::board_config::kMaxDraftLength * 1000);
  assert(ui.draft().size() == thermal_terminal::board_config::kMaxDraftLength);
}

void test_stroke_input_and_language() {
  using thermal_terminal::InputMode;
  using thermal_terminal::LogicalKey;
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](LogicalKey key, std::uint64_t at) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    ui.on_key(event, at, settings, models, false, 0);
  };
  press(LogicalKey::kConfirm, 0);
  press(LogicalKey::kPrint, 10);
  press(LogicalKey::kPrint, 20);
  press(LogicalKey::kPrint, 30);
  assert(ui.input_mode() == InputMode::kStroke);
  // 中: 竖、折、横、竖 in Rime's stroke notation szhs.
  press(LogicalKey::kDigit2, 40);
  press(LogicalKey::kDigit5, 50);
  press(LogicalKey::kDigit1, 60);
  press(LogicalKey::kDigit2, 70);
  assert(ui.stroke_candidate_count() > 0);
  press(LogicalKey::kConfirm, 80);
  assert(ui.draft() == "中");

  press(LogicalKey::kDigit3, 90);
  press(LogicalKey::kDigit2, 100);
  press(LogicalKey::kDigit1, 110);
  press(LogicalKey::kDigit2, 120);
  assert(ui.stroke_candidate_exact_count() == 2);
  assert(std::string(ui.stroke_candidate_text(0)) == "什");
  assert(std::string(ui.stroke_candidate_text(1)) == "仃");
  std::size_t mound_index = ui.stroke_candidate_count();
  for (std::size_t i = 0; i < ui.stroke_candidate_count(); ++i)
    if (std::string(ui.stroke_candidate_text(i)) == "丘") mound_index = i;
  assert(mound_index >= ui.stroke_candidate_exact_count());

  settings.ui_language = thermal_terminal::UiLanguage::kChinese;
  thermal_terminal::BitmapWindow canvas;
  ui.render(canvas, true, settings, models, false);
  bool has_pixels = false;
  for (const auto value : canvas.data())
    has_pixels = has_pixels || value != 0;
  assert(has_pixels);
  canvas.clear();
  canvas.draw_utf8_text(0, 0, "中");
  has_pixels = false;
  bool lower_half_pixels = false;
  for (std::size_t i = 0; i < canvas.data().size(); ++i) {
    has_pixels = has_pixels || canvas.data()[i] != 0;
    lower_half_pixels = lower_half_pixels || (i >= 128 && canvas.data()[i] != 0);
  }
  assert(has_pixels);
  assert(lower_half_pixels);
  canvas.clear();
  canvas.draw_utf8_text(0, 0, "，");
  lower_half_pixels = false;
  for (std::size_t i = 128; i < canvas.data().size(); ++i)
    lower_half_pixels = lower_half_pixels || canvas.data()[i] != 0;
  assert(lower_half_pixels);

  thermal_terminal::TerminalUi settings_ui;
  auto settings_press = [&](LogicalKey key, std::uint64_t at) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    return settings_ui.on_key(event, at, settings, models, false, 0);
  };
  settings_press(LogicalKey::kDown, 100);
  settings_press(LogicalKey::kConfirm, 120);
  for (int i = 0; i < 8; ++i)
    settings_press(LogicalKey::kDown, 130 + i);
  assert(settings_press(LogicalKey::kConfirm, 150) ==
         thermal_terminal::UiAction::kSaveSettings);
  assert(settings.ui_language == thermal_terminal::UiLanguage::kEnglish);
}

void test_utf8_cell_size_and_model_settings() {
  thermal_terminal::BitmapWindow canvas;
  canvas.draw_utf8_text(0, 0, "A", true, 7);
  for (const auto value : canvas.data())
    assert(value == 0);

  canvas.draw_utf8_text(0, 0, "A", true, 8);
  bool ascii_lower_pixels = false;
  for (std::size_t i = 128; i < canvas.data().size(); ++i)
    ascii_lower_pixels = ascii_lower_pixels || canvas.data()[i] != 0;
  assert(ascii_lower_pixels);
  bool outside_ascii_cell = false;
  for (std::uint16_t y = 0; y < 16; ++y) {
    for (std::uint16_t x = 8; x < 16; ++x) {
      const auto offset = static_cast<std::size_t>(y / 8) * 128 + x;
      outside_ascii_cell = outside_ascii_cell ||
                           (canvas.data()[offset] & (1U << (y & 7))) != 0;
    }
  }
  assert(!outside_ascii_cell);

  canvas.clear();
  canvas.draw_utf8_text(0, 0, "A中B", true, 32);
  bool second_cell_pixels = false;
  for (std::uint16_t y = 0; y < 16; ++y) {
    for (std::uint16_t x = 8; x < 24; ++x) {
      const auto offset = static_cast<std::size_t>(y / 8) * 128 + x;
      second_cell_pixels = second_cell_pixels ||
                           (canvas.data()[offset] & (1U << (y & 7))) != 0;
    }
  }
  assert(second_cell_pixels);
  bool third_cell_pixels = false;
  for (std::uint16_t y = 0; y < 16; ++y) {
    for (std::uint16_t x = 24; x < 32; ++x) {
      const auto offset = static_cast<std::size_t>(y / 8) * 128 + x;
      third_cell_pixels = third_cell_pixels ||
                          (canvas.data()[offset] & (1U << (y & 7))) != 0;
    }
  }
  assert(third_cell_pixels);

  thermal_terminal::ModelInfo first;
  first.id = "test:first";
  first.name = "FIRST-MODEL-WITH-A-VERY-LONG-NAME";
  first.reasoning_levels = {{"0", "最低"}, {"1", "低"},
                            {"2", "中"}, {"3", "高"}};
  thermal_terminal::ModelInfo second;
  second.id = "test:second";
  second.name = "SECOND-MODEL-ALSO-VERY-LONG";
  second.reasoning_levels = {{"0", "最低"}, {"1", "低"},
                             {"2", "中"}, {"3", "高"}};
  const std::vector<thermal_terminal::ModelInfo> models{first, second};
  thermal_terminal::UserSettings settings;
  settings.model_id = first.id;
  settings.reasoning_level = 2;
  thermal_terminal::TerminalUi ui;
  thermal_terminal::KeyEvent event;
  auto press = [&](thermal_terminal::LogicalKey key, std::uint64_t at) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    return ui.on_key(event, at, settings, models, false, 0);
  };
  press(thermal_terminal::LogicalKey::kDown, 10);
  press(thermal_terminal::LogicalKey::kDown, 20);
  assert(press(thermal_terminal::LogicalKey::kConfirm, 30) ==
         thermal_terminal::UiAction::kLoadModels);
  assert(ui.screen() == thermal_terminal::UiScreen::kSettings);
  press(thermal_terminal::LogicalKey::kRight, 40);
  assert(settings.model_id == second.id);
  assert(settings.reasoning_level == 2);

  ui.render(canvas, true, settings, models, false);
  const auto first_model_frame = canvas.data();
  ui.tick(600);
  ui.render(canvas, true, settings, models, false);
  assert(canvas.data() != first_model_frame);

  press(thermal_terminal::LogicalKey::kDown, 700);
  ui.render(canvas, true, settings, models, false);
  thermal_terminal::BitmapWindow expected_row;
  expected_row.fill_rect(0, 22, 128, 10, true);
  expected_row.draw_text(4, 23, "REASONING MEDIUM", false, 120);
  const auto &actual = canvas.data();
  const auto &expected = expected_row.data();
  for (std::uint16_t y = 22; y < 32; ++y) {
    for (std::uint16_t x = 0; x < 128; ++x) {
      const auto offset = static_cast<std::size_t>(y / 8) * 128 + x;
      const bool actual_pixel = (actual[offset] & (1U << (y & 7))) != 0;
      const bool expected_pixel = (expected[offset] & (1U << (y & 7))) != 0;
      assert(actual_pixel == expected_pixel);
    }
  }
}

void test_wifi_setup_ui_flow() {
  using thermal_terminal::LogicalKey;
  using thermal_terminal::UiAction;
  using thermal_terminal::UiScreen;
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](LogicalKey key, std::uint64_t at) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    return ui.on_key(event, at, settings, models, false, 0);
  };

  press(LogicalKey::kDown, 1);
  press(LogicalKey::kDown, 2);
  assert(press(LogicalKey::kConfirm, 3) == UiAction::kLoadModels);
  for (int i = 0; i < 7; ++i)
    press(LogicalKey::kDown, 10 + i);
  assert(press(LogicalKey::kConfirm, 30) == UiAction::kScanWifi);
  assert(ui.screen() == UiScreen::kWifiNetworks);

  ui.set_wifi_networks({{"open-net", -42, false},
                        {"locked-net", -55, true}});
  assert(ui.screen() == UiScreen::kWifiNetworks);
  press(LogicalKey::kDown, 40);
  press(LogicalKey::kConfirm, 50);
  assert(ui.screen() == UiScreen::kWifiPassword);
  assert(ui.wifi_ssid() == "locked-net");
  press(LogicalKey::kMenu, 60);
  assert(ui.screen() == UiScreen::kWifiNetworks);

  press(LogicalKey::kDown, 70);
  press(LogicalKey::kConfirm, 80);
  assert(ui.screen() == UiScreen::kWifiPassword);
  for (int i = 0; i < 7; ++i)
    press(LogicalKey::kDigit2, 1000 + i * 900);
  assert(ui.wifi_password().size() == 7);
  assert(press(LogicalKey::kConfirm, 7400) == UiAction::kNone);
  assert(ui.screen() == UiScreen::kNotice);
  press(LogicalKey::kConfirm, 7500);
  assert(ui.screen() == UiScreen::kWifiPassword);
  press(LogicalKey::kDigit2, 8300);
  assert(ui.wifi_password().size() == 8);
  assert(press(LogicalKey::kConfirm, 9200) == UiAction::kConnectWifi);
  assert(ui.wifi_ssid() == "locked-net");

  ui.finish_wifi_setup(true);
  assert(ui.screen() == UiScreen::kNotice);
  press(LogicalKey::kConfirm, 9300);
  assert(ui.screen() == UiScreen::kSettings);

  ui.go_home();
  press(LogicalKey::kDown, 9400);
  press(LogicalKey::kDown, 9401);
  assert(press(LogicalKey::kConfirm, 9402) == UiAction::kLoadModels);
  for (int i = 0; i < 7; ++i)
    press(LogicalKey::kDown, 9410 + i);
  press(LogicalKey::kConfirm, 9430);
  ui.set_wifi_networks({{"open-net", -42, false}});
  assert(press(LogicalKey::kConfirm, 9440) == UiAction::kConnectWifi);
  assert(ui.wifi_ssid() == "open-net" && ui.wifi_password().empty());
}

void test_https_setting_and_url() {
  using thermal_terminal::LogicalKey;
  using thermal_terminal::UiAction;
  thermal_terminal::TerminalUi ui;
  thermal_terminal::UserSettings settings;
  const std::vector<thermal_terminal::ModelInfo> models;
  thermal_terminal::KeyEvent event;
  auto press = [&](LogicalKey key) {
    event.key = key;
    event.command = 0;
    event.repeat = false;
    return ui.on_key(event, 0, settings, models, false, 0);
  };

  assert(settings.use_https);
  press(LogicalKey::kDown);
  press(LogicalKey::kDown);
  assert(press(LogicalKey::kConfirm) == UiAction::kLoadModels);
  press(LogicalKey::kUp);
  assert(press(LogicalKey::kConfirm) == UiAction::kSaveSettings);
  assert(!settings.use_https);
  assert(press(LogicalKey::kLeft) == UiAction::kSaveSettings);
  assert(settings.use_https);

  using thermal_terminal::worker_url_for_protocol;
  assert(worker_url_for_protocol("https://example.com/api/", false) ==
         "http://example.com/api/");
  assert(worker_url_for_protocol("http://example.com:8787", true) ==
         "https://example.com:8787");
  assert(worker_url_for_protocol("example.com", false) ==
         "http://example.com");
  assert(worker_url_for_protocol("", true).empty());
}

int main() {
  test_preview_window();
  test_nec_frames();
  test_remote_keymap();
  test_terminal_ui();
  test_retry_long_press();
  test_dictionary_input();
  test_dictionary_candidate_scroll();
  test_dictionary_selection_crosses_pages();
  test_repeated_backspace_does_not_move_cursor();
  test_compose_modes_and_cursor();
  test_stroke_input_and_language();
  test_utf8_cell_size_and_model_settings();
  test_wifi_setup_ui_flow();
  test_https_setting_and_url();
}
