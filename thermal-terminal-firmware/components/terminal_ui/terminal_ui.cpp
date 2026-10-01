#include "terminal_ui/terminal_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "board_config/board_config.hpp"
#include "english_dictionary/english_dictionary.hpp"
#include "terminal_ui/stroke_data.hpp"

namespace thermal_terminal {
namespace {

constexpr const char *kUpperMultiTap[] = {
    " 0", ".,?!1", "ABC2", "DEF3", "GHI4", "JKL5",
    "MNO6", "PQRS7", "TUV8", "WXYZ9"};
constexpr const char *kLowerMultiTap[] = {
    " 0", ".,?!1", "abc2", "def3", "ghi4", "jkl5",
    "mno6", "pqrs7", "tuv8", "wxyz9"};
constexpr const char *kNumericMultiTap[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
constexpr const char kHalfWidthSymbols[] =
    ".,?!:;\"'()-_/@#%&*+=<>[]{}\\|~`^";
constexpr const char *kFullWidthSymbols[] = {
    "，", "。", "？", "！", "：", "；", "（", "）", "、", "“", "”",
    "《", "》", "【", "】", "＋", "－", "＝", "／", "％", "＆", "＊", "＃", "＠"};
constexpr const char *kHomeItems[] = {"COMPOSE", "HISTORY", "SETTINGS", "PREVIEW"};
constexpr const char *kHomeItemsZh[] = {"编辑", "历史", "设置", "预览"};
constexpr const char *kSettingNames[] = {
    "MODEL", "REASONING", "CONTEXT", "AUTO PRINT",
    "FONT SIZE", "LINE SPACE", "PAPER FEED", "WIFI", "LANGUAGE"};
constexpr const char *kSettingNamesZh[] = {
    "模型", "推理", "上下文", "自动打印", "字号", "行距", "走纸", "网络", "语言"};

const char *input_mode_name(InputMode mode, bool chinese) {
  if (chinese) {
    switch (mode) {
    case InputMode::kUpper: return "大写";
    case InputMode::kLower: return "小写";
    case InputMode::kNumeric: return "数字";
    case InputMode::kStroke: return "笔画";
    case InputMode::kDictionary: return "词典";
    }
  }
  switch (mode) {
  case InputMode::kUpper: return "UPPER";
  case InputMode::kLower: return "lower";
  case InputMode::kNumeric: return "123";
  case InputMode::kStroke: return "STROKE";
  case InputMode::kDictionary: return "WORD";
  }
  return "";
}

std::size_t symbol_count(InputMode mode) {
  return mode == InputMode::kStroke
             ? sizeof(kFullWidthSymbols) / sizeof(kFullWidthSymbols[0])
             : sizeof(kHalfWidthSymbols) - 1;
}

const char *symbol_at(InputMode mode, std::size_t index) {
  if (mode == InputMode::kStroke)
    return kFullWidthSymbols[index % symbol_count(mode)];
  static char value[2];
  value[0] = kHalfWidthSymbols[index % symbol_count(mode)];
  value[1] = '\0';
  return value;
}

const char *stroke_glyph(char digit) {
  switch (digit) {
  case '1': return "一";
  case '2': return "丨";
  case '3': return "丿";
  case '4': return "丶";
  case '5': return "乙";
  default: return "?";
  }
}

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

std::size_t previous_utf8_boundary(const std::string &text, std::size_t position) {
  if (position == 0)
    return 0;
  --position;
  while (position > 0 && is_utf8_continuation(static_cast<unsigned char>(text[position])))
    --position;
  return position;
}

std::size_t next_utf8_boundary(const std::string &text, std::size_t position) {
  if (position >= text.size())
    return text.size();
  ++position;
  while (position < text.size() &&
         is_utf8_continuation(static_cast<unsigned char>(text[position])))
    ++position;
  return position;
}

std::size_t digit_index(LogicalKey key) {
  return static_cast<std::size_t>(key) -
         static_cast<std::size_t>(LogicalKey::kDigit0);
}

bool is_digit(LogicalKey key) {
  return key >= LogicalKey::kDigit0 && key <= LogicalKey::kDigit9;
}

void row(BitmapWindow &canvas, int y, const char *label, bool selected,
         bool large = false) {
  canvas.fill_rect(0, y, 128, large ? 16 : 10, selected);
  if (large)
    canvas.draw_utf8_text(3, y, label, !selected, 122);
  else
    canvas.draw_text(4, y + 1, label, !selected, 120);
}

void header(BitmapWindow &canvas, const char *title, bool large = false) {
  const bool has_chinese = static_cast<unsigned char>(title[0]) >= 0x80;
  const bool use_large = large || has_chinese;
  if (has_chinese)
    canvas.draw_utf8_text(3, 0, title);
  else
    canvas.draw_text(3, 1, title);
  canvas.fill_rect(0, use_large ? 16 : 9, 128, 1, true);
}

int utf8_cell_width(unsigned char first_byte) {
  return first_byte < 0x80 ? 8 : 16;
}

std::string utf8_prefix_cells(const std::string &text, std::size_t cells) {
  std::size_t end = 0;
  while (end < text.size() && cells-- > 0)
    end = next_utf8_boundary(text, end);
  return text.substr(0, end);
}

std::size_t utf8_offset_cells(const std::string &text, std::size_t cells) {
  std::size_t offset = 0;
  while (offset < text.size() && cells-- > 0)
    offset = next_utf8_boundary(text, offset);
  return offset;
}

std::size_t wrapped_line_end(const std::string &text, std::size_t start,
                             int max_width) {
  std::size_t end = start;
  int width = 0;
  while (end < text.size()) {
    const int cell_width = utf8_cell_width(static_cast<unsigned char>(text[end]));
    const auto next = next_utf8_boundary(text, end);
    if (width + cell_width > max_width && end > start)
      break;
    width += cell_width;
    end = next;
  }
  return end == start ? next_utf8_boundary(text, start) : end;
}

std::string compact(const std::string &value, std::size_t limit) {
  std::string result;
  result.reserve(limit);
  for (std::size_t i = 0; i < value.size() && result.size() < limit;) {
    const auto ch = static_cast<unsigned char>(value[i]);
    if (ch >= 32 && ch <= 126) {
      result.push_back(static_cast<char>(ch));
      ++i;
      continue;
    }
    std::size_t length = 1;
    if ((ch & 0xE0) == 0xC0) length = 2;
    else if ((ch & 0xF0) == 0xE0) length = 3;
    else if ((ch & 0xF8) == 0xF0) length = 4;
    if (length > 1 && i + length <= value.size() && result.size() + length <= limit) {
      result.append(value, i, length);
      i += length;
    } else {
      result.push_back('?');
      ++i;
    }
  }
  return result;
}

std::string ascii_marquee(const std::string &value, std::size_t offset,
                          std::size_t cells) {
  std::string text;
  for (std::size_t i = 0; i < value.size();) {
    const auto ch = static_cast<unsigned char>(value[i]);
    if (ch < 0x80) {
      text.push_back(ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?');
      ++i;
    } else {
      text.push_back('?');
      i = next_utf8_boundary(value, i);
    }
  }
  if (text.size() <= cells)
    return text;
  constexpr std::size_t kGap = 3;
  const auto period = text.size() + kGap;
  std::string result;
  result.reserve(cells);
  const auto start = offset % period;
  for (std::size_t i = 0; i < cells; ++i) {
    const auto position = (start + i) % period;
    result.push_back(position < text.size() ? text[position] : ' ');
  }
  return result;
}

std::string utf8_marquee(const std::string &value, std::size_t offset,
                         int max_width) {
  struct Cell {
    std::size_t begin;
    std::size_t end;
    int width;
  };
  std::vector<Cell> cells;
  int total_width = 0;
  for (std::size_t i = 0; i < value.size();) {
    const auto end = next_utf8_boundary(value, i);
    const int width = utf8_cell_width(static_cast<unsigned char>(value[i]));
    cells.push_back({i, end, width});
    total_width += width;
    i = end;
  }
  if (total_width <= max_width)
    return value;

  constexpr int kGapCells = 2;
  const auto period = cells.size() + kGapCells;
  const auto start = offset % period;
  std::string result;
  int used_width = 0;
  for (std::size_t i = 0; used_width < max_width; ++i) {
    const auto position = (start + i) % period;
    if (position < cells.size()) {
      const auto &cell = cells[position];
      if (used_width + cell.width > max_width)
        break;
      result.append(value, cell.begin, cell.end - cell.begin);
      used_width += cell.width;
    } else {
      result.push_back(' ');
      used_width += 8;
    }
  }
  return result;
}

std::string reasoning_label(const ModelInfo &model, std::size_t index,
                            bool chinese) {
  if (index >= model.reasoning_levels.size())
    return chinese ? "默认" : "DEFAULT";
  const auto &level = model.reasoning_levels[index];
  if (chinese)
    return level.label;

  std::string id = level.id;
  std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (id == "minimum" || id == "min" || id == "lowest") return "MINIMUM";
  if (id == "low") return "LOW";
  if (id == "medium" || id == "mid") return "MEDIUM";
  if (id == "maximum" || id == "max" || id == "xhigh") return "MAXIMUM";
  if (id == "high") return "HIGH";
  switch (index) {
  case 0: return "MINIMUM";
  case 1: return "LOW";
  case 2: return "MEDIUM";
  case 3: return "HIGH";
  default: return "LEVEL " + level.id;
  }
}

} // namespace

void TerminalUi::commit_pending() { pending_key_ = LogicalKey::kBack; }

void TerminalUi::tick(std::uint64_t now_ms) {
  if (pending_key_ != LogicalKey::kBack && now_ms >= pending_at_ + 850) {
    commit_pending();
    dirty_ = true;
  }
  if (is_text_entry() && now_ms >= last_cursor_blink_ + 450) {
    cursor_visible_ = !cursor_visible_;
    last_cursor_blink_ = now_ms;
    dirty_ = true;
  }
  if (((screen_ == UiScreen::kSettings && settings_selected_ == 0) ||
       screen_ == UiScreen::kWifiNetworks ||
       screen_ == UiScreen::kWifiPassword) &&
      now_ms >= settings_name_scroll_at_ + 500) {
    ++settings_name_scroll_;
    settings_name_scroll_at_ = now_ms;
    dirty_ = true;
  }
  if (screen_ == UiScreen::kCompose &&
      input_mode_ == InputMode::kDictionary &&
      !dictionary_candidates_.empty() &&
      now_ms >= dictionary_scroll_at_ + 450) {
    ++dictionary_scroll_;
    dictionary_scroll_at_ = now_ms;
    dirty_ = true;
  }
}

void TerminalUi::handle_digit(LogicalKey key, std::uint64_t now_ms) {
  if (screen_ == UiScreen::kCompose && input_mode_ == InputMode::kStroke) {
    handle_stroke_key(key);
    return;
  }
  if (screen_ == UiScreen::kCompose && input_mode_ == InputMode::kDictionary) {
    if (key == LogicalKey::kDigit0) {
      insert_text(' ');
      return;
    }
    if (key == LogicalKey::kDigit1) {
      const char *choices = kUpperMultiTap[1];
      auto &text = active_text();
      auto &cursor = active_cursor();
      const std::size_t count = std::char_traits<char>::length(choices);
      if (key == pending_key_ && now_ms - pending_at_ < 850 &&
          pending_position_ < text.size()) {
        pending_index_ = static_cast<std::uint8_t>((pending_index_ + 1) % count);
        text[pending_position_] = choices[pending_index_];
      } else if (text.size() < board_config::kMaxDraftLength) {
        pending_index_ = 0;
        pending_position_ = cursor;
        text.insert(cursor++, 1, choices[0]);
      }
      pending_key_ = key;
      pending_at_ = now_ms;
      cursor_visible_ = true;
      last_cursor_blink_ = now_ms;
      dirty_ = true;
      return;
    }
    handle_dictionary_key(key, now_ms);
    return;
  }
  auto &text = active_text();
  auto &cursor = active_cursor();
  const auto digit = digit_index(key);
  const char *choices = kUpperMultiTap[digit];
  if (input_mode_ == InputMode::kLower)
    choices = kLowerMultiTap[digit];
  else if (input_mode_ == InputMode::kNumeric)
    choices = kNumericMultiTap[digit];
  const std::size_t count = std::char_traits<char>::length(choices);
  if (key == pending_key_ && now_ms - pending_at_ < 850 &&
      pending_position_ < text.size() && input_mode_ != InputMode::kNumeric) {
    pending_index_ = static_cast<std::uint8_t>((pending_index_ + 1) % count);
    text[pending_position_] = choices[pending_index_];
  } else if (text.size() < (screen_ == UiScreen::kWifiPassword ? 63
                                                                : board_config::kMaxDraftLength)) {
    pending_index_ = 0;
    pending_position_ = cursor;
    text.insert(cursor++, 1, choices[0]);
  } else {
    return;
  }
  pending_key_ = input_mode_ == InputMode::kNumeric ? LogicalKey::kBack : key;
  pending_at_ = now_ms;
  cursor_visible_ = true;
  last_cursor_blink_ = now_ms;
  dirty_ = true;
}

void TerminalUi::move_cursor(std::ptrdiff_t delta) {
  commit_pending();
  auto &text = active_text();
  auto &cursor = active_cursor();
  if (delta < 0) {
    for (std::ptrdiff_t i = 0; i < -delta && cursor > 0; ++i)
      cursor = previous_utf8_boundary(text, cursor);
  } else {
    for (std::ptrdiff_t i = 0; i < delta && cursor < text.size(); ++i)
      cursor = next_utf8_boundary(text, cursor);
  }
  cursor_visible_ = true;
  dirty_ = true;
}

void TerminalUi::backspace() {
  commit_pending();
  auto &text = active_text();
  auto &cursor = active_cursor();
  if (cursor == 0 || text.empty())
    return;
  const auto start = previous_utf8_boundary(text, cursor);
  text.erase(start, cursor - start);
  cursor = start;
  cursor_visible_ = true;
  dirty_ = true;
}

void TerminalUi::insert_text(char ch) {
  char text[2] = {ch, '\0'};
  insert_text(text);
}

void TerminalUi::insert_text(const char *text) {
  commit_pending();
  if (text == nullptr)
    return;
  const std::string value(text);
  auto &target = active_text();
  auto &cursor = active_cursor();
  const auto max_length = screen_ == UiScreen::kWifiPassword
                              ? 63U
                              : board_config::kMaxDraftLength;
  if (value.empty() || target.size() + value.size() > max_length)
    return;
  target.insert(cursor, value);
  cursor += value.size();
  cursor_visible_ = true;
  dirty_ = true;
}

void TerminalUi::switch_input_mode() {
  commit_pending();
  symbol_panel_ = false;
  const unsigned mode_count = screen_ == UiScreen::kWifiPassword ? 3U : 5U;
  input_mode_ = static_cast<InputMode>(
      (static_cast<unsigned>(input_mode_) + 1U) % mode_count);
  clear_stroke_composition();
  clear_dictionary_composition();
  dirty_ = true;
}

void TerminalUi::open_symbol_panel() {
  commit_pending();
  if (screen_ == UiScreen::kCompose)
    clear_stroke_composition();
  if (screen_ == UiScreen::kCompose)
    clear_dictionary_composition();
  symbol_panel_ = true;
  symbol_selected_ = 0;
  dirty_ = true;
}

bool TerminalUi::is_text_entry() const {
  return screen_ == UiScreen::kCompose || screen_ == UiScreen::kWifiPassword;
}

std::string &TerminalUi::active_text() {
  return screen_ == UiScreen::kWifiPassword ? wifi_password_ : draft_;
}

std::size_t &TerminalUi::active_cursor() {
  return screen_ == UiScreen::kWifiPassword ? wifi_password_cursor_ : cursor_;
}

void TerminalUi::handle_symbol_key(LogicalKey key) {
  const std::size_t count = symbol_count(input_mode_);
  const std::size_t columns = screen_ == UiScreen::kWifiPassword ? 16 : 8;
  if (key == LogicalKey::kLeft)
    symbol_selected_ = symbol_selected_ == 0 ? count - 1 : symbol_selected_ - 1;
  else if (key == LogicalKey::kRight)
    symbol_selected_ = (symbol_selected_ + 1) % count;
  else if (key == LogicalKey::kUp)
    symbol_selected_ = symbol_selected_ < columns
                           ? std::min(count - 1,
                                      ((count - 1) / columns) * columns + symbol_selected_)
                           : symbol_selected_ - columns;
  else if (key == LogicalKey::kDown)
    symbol_selected_ = symbol_selected_ + columns < count
                           ? symbol_selected_ + columns
                           : symbol_selected_ % columns;
  else if (key == LogicalKey::kConfirm) {
    insert_text(symbol_at(input_mode_, symbol_selected_));
    symbol_panel_ = false;
  }
  else if (key == LogicalKey::kClear)
    symbol_panel_ = false;
  else if (key == LogicalKey::kSend)
    symbol_panel_ = false;
  dirty_ = true;
}

void TerminalUi::update_stroke_candidates() {
  stroke_candidates_.clear();
  stroke_candidate_exact_count_ = 0;
  std::string code;
  code.reserve(stroke_sequence_.size());
  for (const char digit : stroke_sequence_) {
    switch (digit) {
    case '1': code.push_back('h'); break;
    case '2': code.push_back('s'); break;
    case '3': code.push_back('p'); break;
    case '4': code.push_back('n'); break;
    case '5': code.push_back('z'); break;
    default: break;
    }
  }
  if (code.empty())
    return;

  auto matches = [&](const stroke_data::Character &character, bool exact) {
    for (std::size_t i = 0; i < character.stroke_count; ++i) {
      const char *candidate =
          stroke_data::kStrokeCodes[character.stroke_offset + i];
      if ((exact && std::strcmp(candidate, code.c_str()) == 0) ||
          (!exact && std::strncmp(candidate, code.c_str(), code.size()) == 0))
        return true;
    }
    return false;
  };

  for (std::size_t i = 0; i < stroke_data::kCharacterCount; ++i) {
    if (matches(stroke_data::kCharacters[i], true))
      stroke_candidates_.push_back(static_cast<std::uint16_t>(i));
  }
  stroke_candidate_exact_count_ = stroke_candidates_.size();
  for (std::size_t i = 0; i < stroke_data::kCharacterCount; ++i) {
    const auto index = static_cast<std::uint16_t>(i);
    if (std::binary_search(stroke_candidates_.begin(),
                           stroke_candidates_.begin() + stroke_candidate_exact_count_,
                           index))
      continue;
    if (matches(stroke_data::kCharacters[i], false))
      stroke_candidates_.push_back(index);
  }
  stroke_candidate_page_ = 0;
  stroke_candidate_selection_ = 0;
}

void TerminalUi::clear_stroke_composition() {
  stroke_sequence_.clear();
  stroke_candidates_.clear();
  stroke_candidate_exact_count_ = 0;
  stroke_candidate_page_ = 0;
  stroke_candidate_selection_ = 0;
}

void TerminalUi::update_dictionary_candidates(std::uint64_t now_ms) {
  dictionary_candidates_.clear();
  dictionary_candidate_page_ = 0;
  dictionary_candidate_selection_ = 0;
  dictionary_scroll_ = 0;
  dictionary_scroll_at_ = now_ms;
  if (dictionary_ == nullptr || dictionary_sequence_.empty())
    return;
  dictionary_->query(dictionary_sequence_, dictionary_candidates_, 24);
}

void TerminalUi::clear_dictionary_composition() {
  dictionary_sequence_.clear();
  dictionary_candidates_.clear();
  dictionary_candidate_page_ = 0;
  dictionary_candidate_selection_ = 0;
  dictionary_scroll_ = 0;
}

void TerminalUi::handle_dictionary_key(LogicalKey key, std::uint64_t now_ms) {
  if (key >= LogicalKey::kDigit0 && key <= LogicalKey::kDigit9) {
    if (dictionary_sequence_.size() < 31) {
      dictionary_sequence_.push_back(
          static_cast<char>('0' + digit_index(key)));
      update_dictionary_candidates(now_ms);
    }
  } else if (key == LogicalKey::kUp || key == LogicalKey::kDown) {
    constexpr std::size_t kPageSize = 4;
    const auto page_start = dictionary_candidate_page_ * kPageSize;
    const auto page_count = std::min(
        kPageSize, dictionary_candidates_.size() > page_start
                       ? dictionary_candidates_.size() - page_start
                       : 0U);
    if (page_count != 0) {
      if (key == LogicalKey::kUp)
        dictionary_candidate_selection_ =
            dictionary_candidate_selection_ == 0
                ? page_count - 1
                : dictionary_candidate_selection_ - 1;
      else
        dictionary_candidate_selection_ =
            (dictionary_candidate_selection_ + 1) % page_count;
    }
  } else if (key == LogicalKey::kLeft || key == LogicalKey::kRight) {
    constexpr std::size_t kPageSize = 4;
    const auto page_count =
        (dictionary_candidates_.size() + kPageSize - 1) / kPageSize;
    if (page_count != 0) {
      if (key == LogicalKey::kLeft)
        dictionary_candidate_page_ = dictionary_candidate_page_ == 0
                                         ? page_count - 1
                                         : dictionary_candidate_page_ - 1;
      else
        dictionary_candidate_page_ =
            (dictionary_candidate_page_ + 1) % page_count;
      dictionary_candidate_selection_ = 0;
    }
  } else if (key == LogicalKey::kConfirm) {
    constexpr std::size_t kPageSize = 4;
    const auto index = dictionary_candidate_page_ * kPageSize +
                       dictionary_candidate_selection_;
    if (index < dictionary_candidates_.size()) {
      insert_text(dictionary_candidates_[index].c_str());
      clear_dictionary_composition();
    }
  }
  if (key == LogicalKey::kUp || key == LogicalKey::kDown ||
      key == LogicalKey::kLeft || key == LogicalKey::kRight) {
    dictionary_scroll_ = 0;
    dictionary_scroll_at_ = now_ms;
  }
  dirty_ = true;
}

const std::string &TerminalUi::dictionary_candidate(std::size_t index) const {
  static const std::string empty;
  return index < dictionary_candidates_.size() ? dictionary_candidates_[index]
                                                : empty;
}

void TerminalUi::handle_stroke_key(LogicalKey key) {
  if (key >= LogicalKey::kDigit1 && key <= LogicalKey::kDigit5) {
    if (stroke_sequence_.size() < 64) {
      stroke_sequence_.push_back(static_cast<char>('1' + digit_index(key) - 1));
      update_stroke_candidates();
    }
  } else if (key == LogicalKey::kUp || key == LogicalKey::kDown) {
    constexpr std::size_t kPageSize = 6;
    const auto page_start = stroke_candidate_page_ * kPageSize;
    const auto page_count = std::min(kPageSize,
                                     stroke_candidates_.size() > page_start
                                         ? stroke_candidates_.size() - page_start
                                         : 0U);
    if (page_count != 0) {
      if (key == LogicalKey::kUp)
        stroke_candidate_selection_ = stroke_candidate_selection_ == 0
                                          ? page_count - 1
                                          : stroke_candidate_selection_ - 1;
      else
        stroke_candidate_selection_ =
            (stroke_candidate_selection_ + 1) % page_count;
    }
  } else if (key == LogicalKey::kLeft || key == LogicalKey::kRight) {
    constexpr std::size_t kPageSize = 6;
    const auto page_count = (stroke_candidates_.size() + kPageSize - 1) / kPageSize;
    if (page_count != 0) {
      if (key == LogicalKey::kLeft)
        stroke_candidate_page_ = stroke_candidate_page_ == 0
                                     ? page_count - 1
                                     : stroke_candidate_page_ - 1;
      else
        stroke_candidate_page_ = (stroke_candidate_page_ + 1) % page_count;
      stroke_candidate_selection_ = 0;
    }
  } else if (key == LogicalKey::kConfirm) {
    accept_stroke_candidate();
  }
  dirty_ = true;
}

void TerminalUi::accept_stroke_candidate() {
  constexpr std::size_t kPageSize = 6;
  const auto index = stroke_candidate_page_ * kPageSize + stroke_candidate_selection_;
  if (index >= stroke_candidates_.size())
    return;
  insert_text(stroke_data::kCharacters[stroke_candidates_[index]].utf8);
  clear_stroke_composition();
}

const char *TerminalUi::stroke_candidate_text(std::size_t index) const {
  if (index >= stroke_candidates_.size())
    return nullptr;
  return stroke_data::kCharacters[stroke_candidates_[index]].utf8;
}

void TerminalUi::pan(LogicalKey key, std::uint32_t bitmap_height) {
  switch (key) {
  case LogicalKey::kLeft:
    preview_x_ = preview_x_ > 32 ? preview_x_ - 32 : 0;
    break;
  case LogicalKey::kRight:
    preview_x_ = std::min<std::uint16_t>(256, preview_x_ + 32);
    break;
  case LogicalKey::kUp:
    preview_y_ = preview_y_ > 32 ? preview_y_ - 32 : 0;
    break;
  case LogicalKey::kDown:
    preview_y_ = std::min<std::uint32_t>(
        bitmap_height > 64 ? bitmap_height - 64 : 0, preview_y_ + 32);
    break;
  default:
    break;
  }
  dirty_ = true;
}

UiAction TerminalUi::on_key(const KeyEvent &event, std::uint64_t now_ms,
                             UserSettings &settings,
                             const std::vector<ModelInfo> &models,
                             bool bitmap_ready,
                             std::uint32_t bitmap_height) {
  const auto key = event.key;
  if (key == LogicalKey::kSend && !event.repeat) {
    send_pressed_ = true;
    send_pressed_at_ = now_ms;
    retry_latched_ = false;
  }
  if (!event.repeat)
    retry_latched_ = false;
  if (key == LogicalKey::kSend && event.long_press && send_pressed_ &&
      !retry_latched_ &&
      now_ms >= send_pressed_at_ + board_config::kRetryLongPressMs) {
    retry_latched_ = true;
    commit_pending();
    symbol_panel_ = false;
    dirty_ = true;
    return UiAction::kRetry;
  }
  if (is_text_entry() && !symbol_panel_ &&
      event.command == board_config::kEditBackspaceCommand) {
    if (screen_ == UiScreen::kCompose && input_mode_ == InputMode::kStroke &&
        !stroke_sequence_.empty()) {
      stroke_sequence_.pop_back();
      update_stroke_candidates();
      dirty_ = true;
    } else if (screen_ == UiScreen::kCompose &&
               input_mode_ == InputMode::kDictionary &&
               !dictionary_sequence_.empty()) {
      dictionary_sequence_.pop_back();
      update_dictionary_candidates(now_ms);
      dirty_ = true;
    } else {
      backspace();
    }
    return UiAction::kNone;
  }
  if (is_text_entry() && !symbol_panel_ && event.repeat &&
      event.command == board_config::kEditSpaceCommand)
    return UiAction::kNone;
  if (event.repeat && key != LogicalKey::kUp && key != LogicalKey::kDown &&
      key != LogicalKey::kLeft && key != LogicalKey::kRight)
    return UiAction::kNone;
  if (is_text_entry() && symbol_panel_) {
    if (event.command == board_config::kEditSymbolsCommand ||
        key == LogicalKey::kClear || key == LogicalKey::kMenu ||
        key == LogicalKey::kBack) {
      symbol_panel_ = false;
      dirty_ = true;
      return UiAction::kNone;
    }
    if (event.command == board_config::kEditInputModeCommand ||
        key == LogicalKey::kPrint) {
      switch_input_mode();
      return UiAction::kNone;
    }
    handle_symbol_key(key);
    return UiAction::kNone;
  }
  if (is_text_entry() && !event.repeat) {
    if (event.command == board_config::kEditSpaceCommand) {
      if (screen_ == UiScreen::kCompose)
        clear_stroke_composition();
      if (screen_ == UiScreen::kCompose)
        clear_dictionary_composition();
      insert_text(' ');
      return UiAction::kNone;
    }
  }
  if (key == LogicalKey::kMenu || key == LogicalKey::kBack) {
    commit_pending();
    symbol_panel_ = false;
    if (screen_ == UiScreen::kWifiPassword)
      screen_ = UiScreen::kWifiNetworks;
    else if (screen_ == UiScreen::kWifiNetworks)
      screen_ = UiScreen::kSettings;
    else
      screen_ = UiScreen::kHome;
    dirty_ = true;
    return UiAction::kNone;
  }
  if (is_text_entry() &&
      (event.command == board_config::kEditInputModeCommand ||
       key == LogicalKey::kPrint)) {
    switch_input_mode();
    return UiAction::kNone;
  }
  if (is_text_entry() &&
      (event.command == board_config::kEditSymbolsCommand ||
       key == LogicalKey::kClear)) {
    open_symbol_panel();
    return UiAction::kNone;
  }
  if (screen_ == UiScreen::kWifiNetworks && key == LogicalKey::kClear)
    return UiAction::kScanWifi;
  if (key == LogicalKey::kClear && !is_text_entry()) {
    notice_return_ = screen_;
    screen_ = UiScreen::kConfirmClear;
    clear_yes_ = false;
    dirty_ = true;
    return UiAction::kNone;
  }
  if (key == LogicalKey::kPrint && bitmap_ready &&
      screen_ != UiScreen::kWifiNetworks &&
      screen_ != UiScreen::kWifiPassword)
    return UiAction::kPrint;

  switch (screen_) {
  case UiScreen::kHome:
    if (settings.ui_language == UiLanguage::kChinese &&
        (key == LogicalKey::kUp || key == LogicalKey::kDown ||
         key == LogicalKey::kLeft || key == LogicalKey::kRight)) {
      if (key == LogicalKey::kUp || key == LogicalKey::kDown)
        selected_ ^= 1U;
      else if (key == LogicalKey::kLeft || key == LogicalKey::kRight)
        selected_ ^= 2U;
    } else if (key == LogicalKey::kUp)
      selected_ = selected_ == 0 ? 3 : selected_ - 1;
    else if (key == LogicalKey::kDown)
      selected_ = (selected_ + 1) % 4;
    else if (key == LogicalKey::kConfirm) {
      switch (selected_) {
      case 0: screen_ = UiScreen::kCompose; break;
      case 1:
        if (settings.ui_language == UiLanguage::kChinese) {
          screen_ = UiScreen::kSettings;
          settings_name_scroll_ = 0;
          settings_name_scroll_at_ = now_ms;
          dirty_ = true;
          return UiAction::kLoadModels;
        }
        screen_ = UiScreen::kHistory;
        dirty_ = true;
        return UiAction::kLoadHistory;
      case 2:
        if (settings.ui_language == UiLanguage::kChinese) {
          screen_ = UiScreen::kHistory;
          dirty_ = true;
          return UiAction::kLoadHistory;
        }
        screen_ = UiScreen::kSettings;
        settings_name_scroll_ = 0;
        settings_name_scroll_at_ = now_ms;
        dirty_ = true;
        return UiAction::kLoadModels;
      case 3:
        if (bitmap_ready) screen_ = UiScreen::kPreview;
        else show_notice("NO REPLY LOADED");
        break;
      }
    } else if (key == LogicalKey::kSend) {
      screen_ = UiScreen::kCompose;
    }
    break;
  case UiScreen::kCompose:
    if (symbol_panel_) {
      handle_symbol_key(key);
      break;
    }
    if (is_digit(key)) handle_digit(key, now_ms);
    else if (input_mode_ == InputMode::kStroke && !stroke_sequence_.empty() &&
             (key == LogicalKey::kUp || key == LogicalKey::kDown ||
              key == LogicalKey::kLeft || key == LogicalKey::kRight)) {
      handle_stroke_key(key);
    } else if (input_mode_ == InputMode::kDictionary &&
               !dictionary_sequence_.empty() &&
               (key == LogicalKey::kUp || key == LogicalKey::kDown ||
                key == LogicalKey::kLeft || key == LogicalKey::kRight)) {
      handle_dictionary_key(key, now_ms);
    } else if (key == LogicalKey::kLeft) {
      move_cursor(-1);
    } else if (key == LogicalKey::kRight) {
      move_cursor(1);
    } else if (key == LogicalKey::kUp) {
      move_cursor(-20);
    } else if (key == LogicalKey::kDown) {
      move_cursor(20);
    } else if (key == LogicalKey::kSend) {
      commit_pending();
      if (!draft_.empty() && stroke_sequence_.empty() &&
          dictionary_sequence_.empty()) return UiAction::kSend;
    } else if (key == LogicalKey::kConfirm) {
      if (input_mode_ == InputMode::kStroke)
        accept_stroke_candidate();
      else if (input_mode_ == InputMode::kDictionary)
        handle_dictionary_key(key, now_ms);
      else
        commit_pending();
    }
    break;
  case UiScreen::kWifiNetworks:
    if (key == LogicalKey::kUp && wifi_network_selected_ > 0) {
      --wifi_network_selected_;
      settings_name_scroll_ = 0;
      settings_name_scroll_at_ = now_ms;
    } else if (key == LogicalKey::kDown &&
               wifi_network_selected_ + 1 < wifi_networks_.size()) {
      ++wifi_network_selected_;
      settings_name_scroll_ = 0;
      settings_name_scroll_at_ = now_ms;
    } else if (key == LogicalKey::kSend)
      return UiAction::kScanWifi;
    else if (key == LogicalKey::kConfirm &&
             wifi_network_selected_ < wifi_networks_.size()) {
      const auto &network = wifi_networks_[wifi_network_selected_];
      wifi_ssid_ = network.ssid;
      wifi_password_.clear();
      wifi_password_cursor_ = 0;
      settings_name_scroll_ = 0;
      settings_name_scroll_at_ = now_ms;
      commit_pending();
      if (network.secured) {
        screen_ = UiScreen::kWifiPassword;
        input_mode_ = InputMode::kUpper;
        cursor_visible_ = true;
        last_cursor_blink_ = now_ms;
        dirty_ = true;
        break;
      }
      return UiAction::kConnectWifi;
    }
    break;
  case UiScreen::kWifiPassword:
    if (is_digit(key))
      handle_digit(key, now_ms);
    else if (key == LogicalKey::kLeft)
      move_cursor(-1);
    else if (key == LogicalKey::kRight)
      move_cursor(1);
    else if (key == LogicalKey::kUp)
      move_cursor(-20);
    else if (key == LogicalKey::kDown)
      move_cursor(20);
    else if (key == LogicalKey::kConfirm || key == LogicalKey::kSend) {
      commit_pending();
      if (wifi_password_.size() < 8) {
        show_notice("PASSWORD TOO SHORT");
      } else {
        return UiAction::kConnectWifi;
      }
    }
    break;
  case UiScreen::kHistory:
    if (key == LogicalKey::kUp && history_selected_ > 0)
      --history_selected_;
    else if (key == LogicalKey::kDown && history_selected_ + 1 < history_.size())
      ++history_selected_;
    else if (key == LogicalKey::kRight && !next_cursor_.empty())
      return UiAction::kLoadOlderHistory;
    else if (key == LogicalKey::kConfirm && selected_message() != nullptr) {
      if (selected_message()->assistant)
        return UiAction::kRenderHistory;
      history_text_scroll_ = 0;
      screen_ = UiScreen::kHistoryText;
    }
    else if (key == LogicalKey::kLeft)
      screen_ = UiScreen::kHome;
    break;
  case UiScreen::kHistoryText:
    if (key == LogicalKey::kUp && history_text_scroll_ > 0)
      --history_text_scroll_;
    else if (key == LogicalKey::kDown && selected_message() != nullptr &&
             (history_text_scroll_ + 1) * 20 < selected_message()->content.size())
      ++history_text_scroll_;
    else if (key == LogicalKey::kLeft)
      screen_ = UiScreen::kHistory;
    break;
  case UiScreen::kSettings: {
    const auto old_selection = settings_selected_;
    if (key == LogicalKey::kUp)
      settings_selected_ = settings_selected_ == 0 ? 8 : settings_selected_ - 1;
    else if (key == LogicalKey::kDown)
      settings_selected_ = (settings_selected_ + 1) % 9;
    if (old_selection != settings_selected_) {
      settings_name_scroll_ = 0;
      settings_name_scroll_at_ = now_ms;
    }
    else if (key == LogicalKey::kLeft || key == LogicalKey::kRight ||
             key == LogicalKey::kConfirm) {
      const int direction = key == LogicalKey::kLeft ? -1 : 1;
      switch (settings_selected_) {
      case 0:
        if (!models.empty()) {
          const auto current = std::find_if(
              models.begin(), models.end(), [&](const ModelInfo &model) {
                return model.id == settings.model_id;
              });
          std::size_t next_index = 0;
          if (current == models.end()) {
            next_index = direction > 0 ? 0 : models.size() - 1;
          } else {
            const auto current_index =
                static_cast<std::size_t>(current - models.begin());
            next_index = direction > 0
                             ? (current_index + 1) % models.size()
                             : (current_index + models.size() - 1) % models.size();
          }

          std::string previous_level_id;
          if (current != models.end() &&
              settings.reasoning_level < current->reasoning_levels.size())
            previous_level_id =
                current->reasoning_levels[settings.reasoning_level].id;

          const auto &next_model = models[next_index];
          std::size_t next_level = settings.reasoning_level;
          if (next_model.reasoning_levels.empty()) {
            next_level = 0;
          } else {
            const auto matching_level = std::find_if(
                next_model.reasoning_levels.begin(),
                next_model.reasoning_levels.end(),
                [&](const ReasoningLevel &level) {
                  return !previous_level_id.empty() &&
                         level.id == previous_level_id;
                });
            if (matching_level != next_model.reasoning_levels.end()) {
              next_level = static_cast<std::size_t>(
                  matching_level - next_model.reasoning_levels.begin());
            } else {
              next_level = std::min(next_level,
                                    next_model.reasoning_levels.size() - 1);
            }
          }
          settings.model_id = next_model.id;
          settings.reasoning_level = static_cast<std::uint8_t>(next_level);
          settings_name_scroll_ = 0;
          settings_name_scroll_at_ = now_ms;
        }
        break;
      case 1:
        for (const auto &model : models) {
          if (model.id == settings.model_id && !model.reasoning_levels.empty()) {
            const int count = static_cast<int>(model.reasoning_levels.size());
            settings.reasoning_level = (settings.reasoning_level + count + direction) % count;
            break;
          }
        }
        break;
      case 2: settings.use_context = !settings.use_context; break;
      case 3: settings.printer.auto_print = !settings.printer.auto_print; break;
      case 4: settings.render.font_size = std::clamp<int>(settings.render.font_size + direction, 14, 30); break;
      case 5: settings.render.line_height = std::clamp(settings.render.line_height + direction * 0.05F, 1.1F, 1.9F); break;
      case 6: settings.render.bottom_feed = std::clamp<int>(settings.render.bottom_feed + direction * 4, 0, 96); break;
      case 7:
        screen_ = UiScreen::kWifiNetworks;
        wifi_network_selected_ = 0;
        dirty_ = true;
        return UiAction::kScanWifi;
      case 8:
        settings.ui_language = settings.ui_language == UiLanguage::kChinese
                                   ? UiLanguage::kEnglish
                                   : UiLanguage::kChinese;
        break;
      }
      dirty_ = true;
      return UiAction::kSaveSettings;
    }
    break;
  }
  case UiScreen::kPreview:
    pan(key, bitmap_height);
    break;
  case UiScreen::kConfirmClear:
    if (key == LogicalKey::kLeft || key == LogicalKey::kRight)
      clear_yes_ = !clear_yes_;
    else if (key == LogicalKey::kConfirm) {
      screen_ = notice_return_;
      dirty_ = true;
      return clear_yes_ ? UiAction::kClearHistory : UiAction::kNone;
    }
    break;
  case UiScreen::kNotice:
    if (key == LogicalKey::kConfirm) screen_ = notice_return_;
    break;
  }
  dirty_ = true;
  return UiAction::kNone;
}

void TerminalUi::set_history(std::vector<HistoryMessage> messages,
                              std::string next_cursor, bool append) {
  if (!append) {
    history_ = std::move(messages);
    history_selected_ = 0;
  } else {
    history_.insert(history_.end(),
                    std::make_move_iterator(messages.begin()),
                    std::make_move_iterator(messages.end()));
  }
  next_cursor_ = std::move(next_cursor);
  dirty_ = true;
}

void TerminalUi::set_wifi_networks(std::vector<WifiNetworkOption> networks) {
  wifi_networks_ = std::move(networks);
  wifi_network_selected_ = 0;
  settings_name_scroll_ = 0;
  screen_ = UiScreen::kWifiNetworks;
  dirty_ = true;
}

void TerminalUi::finish_wifi_setup(bool connected) {
  symbol_panel_ = false;
  clear_stroke_composition();
  clear_dictionary_composition();
  if (connected)
    screen_ = UiScreen::kSettings;
  show_notice(connected ? "WIFI CONNECTED" : "WIFI FAILED");
}

const HistoryMessage *TerminalUi::selected_message() const {
  return history_selected_ < history_.size() ? &history_[history_selected_] : nullptr;
}

void TerminalUi::show_preview() {
  screen_ = UiScreen::kPreview;
  preview_x_ = 0;
  preview_y_ = 0;
  commit_pending();
  dirty_ = true;
}

void TerminalUi::clear_draft() {
  draft_.clear();
  cursor_ = 0;
  symbol_panel_ = false;
  clear_stroke_composition();
  clear_dictionary_composition();
  commit_pending();
  dirty_ = true;
}

void TerminalUi::go_home() {
  screen_ = UiScreen::kHome;
  selected_ = 0;
  dirty_ = true;
}

void TerminalUi::show_notice(const char *message) {
  if (screen_ != UiScreen::kNotice) notice_return_ = screen_;
  screen_ = UiScreen::kNotice;
  notice_ = message == nullptr ? "ERROR" : message;
  dirty_ = true;
}

void TerminalUi::return_from_notice() {
  screen_ = notice_return_;
  dirty_ = true;
}

void TerminalUi::render(BitmapWindow &canvas, bool wifi_connected,
                         const UserSettings &settings,
                         const std::vector<ModelInfo> &models,
                         bool bitmap_ready) const {
  canvas.clear();
  const bool chinese = settings.ui_language == UiLanguage::kChinese;
  char line[48];
  switch (screen_) {
  case UiScreen::kHome:
    header(canvas, chinese ? "聊天终端" : "THERMAL TERMINAL");
    if (chinese) {
      constexpr int kPositions[][2] = {{3, 18}, {3, 38}, {68, 18}, {68, 38}};
      for (int i = 0; i < 4; ++i) {
        const int x = kPositions[i][0];
        const int y = kPositions[i][1];
        canvas.fill_rect(x - 3, y, 62, 16, selected_ == i);
        canvas.draw_utf8_text(x, y,
                              i == 0 ? "编辑" : i == 1 ? "设置" :
                              i == 2 ? "历史" : "预览",
                              selected_ != i, 58);
      }
      canvas.draw_utf8_text(3, 48, wifi_connected ? "网络开" : "网络关");
      canvas.draw_utf8_text(72, 48, bitmap_ready ? "有回复" : "空");
    } else {
      for (int i = 0; i < 4; ++i)
        row(canvas, 12 + i * 10, kHomeItems[i], selected_ == i);
      canvas.draw_text(3, 55, wifi_connected ? "WIFI ON" : "WIFI OFF");
      canvas.draw_text(80, 55, bitmap_ready ? "REPLY" : "EMPTY");
    }
    break;
  case UiScreen::kCompose: {
    const bool has_hanzi = std::any_of(draft_.begin(), draft_.end(),
                                      [](unsigned char ch) { return ch >= 0x80; });
    const bool large_layout = chinese || input_mode_ == InputMode::kStroke ||
                              input_mode_ == InputMode::kDictionary || has_hanzi;
    header(canvas, chinese ? "编辑" : "EDIT", large_layout);
    const int mode_x = chinese ? 38 : large_layout ? 44 : 78;
    if (chinese) {
      canvas.draw_utf8_text(mode_x, 0, input_mode_name(input_mode_, true));
    } else if (large_layout) {
      const char *large_mode = input_mode_ == InputMode::kUpper ? "UP"
                               : input_mode_ == InputMode::kLower ? "LO"
                               : input_mode_ == InputMode::kNumeric ? "123"
                               : input_mode_ == InputMode::kDictionary ? "WORD"
                                                                    : "STK";
      canvas.draw_utf8_text(mode_x, 0, large_mode);
    } else {
      canvas.draw_text(mode_x, 1, input_mode_name(input_mode_, false));
    }
    if (input_mode_ == InputMode::kStroke && !stroke_sequence_.empty()) {
      if (large_layout) {
        const std::size_t visible_strokes = chinese ? 3 : 2;
        const int preview_x = chinese ? 70 : 94;
        const auto first = stroke_sequence_.size() > visible_strokes
                               ? stroke_sequence_.size() - visible_strokes
                               : 0;
        for (std::size_t i = first; i < stroke_sequence_.size(); ++i)
          canvas.draw_utf8_text(preview_x + static_cast<int>(i - first) * 16, 0,
                                stroke_glyph(stroke_sequence_[i]), true, 16);
      } else {
        canvas.draw_text(82, 4, stroke_sequence_.c_str(), true, 44);
      }
    }
    if (input_mode_ == InputMode::kDictionary && !dictionary_sequence_.empty())
      canvas.draw_text(chinese ? 70 : 82, 4, dictionary_sequence_.c_str(), true,
                      44);

    if (large_layout) {
      const auto visible_cursor = std::min(cursor_, draft_.size());
      std::size_t previous_line_start = 0;
      std::size_t cursor_line_start = 0;
      std::size_t cursor_line_end = 0;
      std::size_t line_start = 0;
      std::size_t line_index = 0;
      while (line_start < draft_.size()) {
        const auto line_end = wrapped_line_end(draft_, line_start, 120);
        if (visible_cursor < line_end ||
            (visible_cursor == line_end && line_end == draft_.size())) {
          cursor_line_start = line_start;
          cursor_line_end = line_end;
          break;
        }
        previous_line_start = line_start;
        line_start = line_end;
        ++line_index;
      }
      const std::size_t first_line_start =
          line_index == 0 ? cursor_line_start : previous_line_start;
      const auto first_line_end =
          first_line_start < draft_.size()
              ? wrapped_line_end(draft_, first_line_start, 120)
              : first_line_start;
      canvas.draw_utf8_text(3, 16, draft_.c_str() + first_line_start, true, 120);
      if (first_line_end < draft_.size()) {
        canvas.draw_utf8_text(3, 32, draft_.c_str() + first_line_end, true, 120);
      }
      const int cursor_row = line_index == 0 ? 0 : 1;
      int cursor_x = 3;
      for (auto i = cursor_line_start; i < visible_cursor;
           i = next_utf8_boundary(draft_, i))
        cursor_x += utf8_cell_width(static_cast<unsigned char>(draft_[i]));
      if (cursor_visible_ && visible_cursor >= cursor_line_start &&
          visible_cursor <= cursor_line_end)
        canvas.fill_rect(cursor_x, 30 + cursor_row * 16, 5, 1, true);

      if (symbol_panel_) {
        canvas.fill_rect(0, 48, 128, 16, true);
        const auto count = symbol_count(input_mode_);
        constexpr std::size_t kVisibleSymbols = 8;
        const auto first = (symbol_selected_ / kVisibleSymbols) * kVisibleSymbols;
        for (std::size_t i = first;
             i < std::min(count, first + kVisibleSymbols); ++i) {
          const int x = static_cast<int>(i - first) * 16;
          canvas.draw_utf8_text(x, 48, symbol_at(input_mode_, i), false, 16);
          if (i == symbol_selected_)
            canvas.fill_rect(x, 63, 16, 1, false);
        }
      } else if (input_mode_ == InputMode::kDictionary &&
                 !dictionary_sequence_.empty()) {
        constexpr std::size_t kPageSize = 4;
        const auto index = dictionary_candidate_page_ * kPageSize +
                           dictionary_candidate_selection_;
        if (index >= dictionary_candidates_.size()) {
          canvas.draw_text(3, 48, "NONE", true, 120);
        } else {
          canvas.fill_rect(0, 48, 128, 16, true);
          const auto word = ascii_marquee(dictionary_candidates_[index],
                                          dictionary_scroll_, 12);
          canvas.draw_utf8_text(0, 48, word.c_str(), false, 96);
          std::snprintf(line, sizeof(line), "%u/%u",
                        static_cast<unsigned>(index + 1),
                        static_cast<unsigned>(dictionary_candidates_.size()));
          canvas.draw_text(100, 55, line, false, 28);
        }
      } else if (input_mode_ == InputMode::kStroke && !stroke_sequence_.empty()) {
        constexpr std::size_t kPageSize = 6;
        const auto first = stroke_candidate_page_ * kPageSize;
        const auto last = std::min(stroke_candidates_.size(), first + kPageSize);
        if (first == last) {
          canvas.draw_utf8_text(3, 48, chinese ? "无匹配" : "NONE", true, 120);
        } else {
          for (std::size_t i = first; i < last; ++i) {
            const int x = 3 + static_cast<int>(i - first) * 20;
            const bool selected = i - first == stroke_candidate_selection_;
            if (selected)
              canvas.fill_rect(x, 48, 16, 16, true);
            canvas.draw_utf8_text(x, 48, stroke_candidate_text(i), !selected, 16);
          }
        }
      } else {
        canvas.draw_utf8_text(3, 48,
                              multi_tap_pending() ? (chinese ? "等待" : "WAIT")
                                                  : (chinese ? "就绪" : "READY"),
                              true, 120);
      }
    } else {
      const auto visible_cursor = std::min(cursor_, draft_.size());
      const auto start = visible_cursor >= 80 ? (visible_cursor / 20 - 3) * 20 : 0;
      for (int i = 0; i < 4; ++i) {
        const auto offset = start + i * 20;
        if (offset < draft_.size())
          canvas.draw_text(3, 13 + i * 10,
                           draft_.substr(offset, 20).c_str(), true, 120);
      }
      const auto cursor_row = static_cast<int>((visible_cursor - start) / 20);
      const auto cursor_col = static_cast<int>((visible_cursor - start) % 20);
      if (cursor_visible_ && cursor_row < 4)
        canvas.fill_rect(3 + cursor_col * 6, 20 + cursor_row * 10, 5, 1, true);
      std::snprintf(line, sizeof(line), "%u/%u %s",
                    static_cast<unsigned>(draft_.size()),
                    static_cast<unsigned>(board_config::kMaxDraftLength),
                    multi_tap_pending() ? "WAIT" : "READY");
      canvas.draw_text(3, 55, line, true, 126);
      if (symbol_panel_) {
        canvas.fill_rect(0, 43, 128, 12, true);
        const auto count = symbol_count(input_mode_);
        const auto first = symbol_selected_ > 9 ? symbol_selected_ - 9 : 0;
        for (std::size_t i = first; i < std::min(count, first + 20); ++i) {
          const int x = 4 + static_cast<int>(i - first) * 6;
          canvas.draw_text(x, 45, symbol_at(input_mode_, i), false, 6);
          if (i == symbol_selected_)
            canvas.fill_rect(x, 53, 5, 1, false);
        }
      }
    }
    break;
  }
  case UiScreen::kWifiNetworks: {
    header(canvas, chinese ? "选择网络" : "SELECT WIFI", true);
    if (wifi_networks_.empty()) {
      if (chinese)
        canvas.draw_utf8_text(3, 25, "无网络");
      else
        canvas.draw_utf8_text(3, 24, "NO NETWORKS");
      break;
    }
    constexpr std::size_t kPageSize = 3;
    const auto first = (wifi_network_selected_ / kPageSize) * kPageSize;
    const auto last = std::min(wifi_networks_.size(), first + kPageSize);
    for (std::size_t i = first; i < last; ++i) {
      const auto &network = wifi_networks_[i];
      const int y = 16 + static_cast<int>(i - first) * 16;
      const bool selected = i == wifi_network_selected_;
      canvas.fill_rect(0, y, 128, 16, selected);
      canvas.draw_utf8_text(
          3, y,
          utf8_marquee(network.ssid,
                       selected ? settings_name_scroll_ : 0, 84).c_str(),
          !selected, 88);
      canvas.draw_utf8_text(94, y, network.secured ? "LOCK" : "OPEN",
                            !selected, 32);
    }
    break;
  }
  case UiScreen::kWifiPassword: {
    header(canvas, chinese ? "输入密码" : "WIFI PASSWORD", true);
    canvas.draw_utf8_text(
        3, 16, utf8_marquee(wifi_ssid_, settings_name_scroll_, 120).c_str(),
        true, 120);
    const auto visible_cursor =
        std::min(wifi_password_cursor_, wifi_password_.size());
    const auto start = visible_cursor > 15 ? visible_cursor - 15 : 0;
    const auto end = std::min(wifi_password_.size(), start + 15);
    const std::string masked(end - start, '*');
    canvas.draw_utf8_text(3, 32, masked.c_str(), true, 120);
    if (cursor_visible_)
      canvas.fill_rect(3 + static_cast<int>(visible_cursor - start) * 8,
                       47, 6, 1, true);
    if (symbol_panel_) {
      canvas.fill_rect(0, 48, 128, 16, true);
      constexpr std::size_t kVisibleSymbols = 16;
      const auto first = (symbol_selected_ / kVisibleSymbols) * kVisibleSymbols;
      const auto count = symbol_count(input_mode_);
      for (std::size_t i = first;
           i < std::min(count, first + kVisibleSymbols); ++i) {
        const int x = static_cast<int>(i - first) * 8;
        canvas.draw_utf8_text(x, 48, symbol_at(input_mode_, i), false, 8);
        if (i == symbol_selected_)
          canvas.fill_rect(x, 63, 8, 1, false);
      }
    } else {
      canvas.draw_utf8_text(3, 48, input_mode_name(input_mode_, chinese),
                            true, 70);
      if (multi_tap_pending())
        canvas.draw_utf8_text(78, 48, chinese ? "等待" : "WAIT", true, 48);
      std::snprintf(line, sizeof(line), "%u/63",
                    static_cast<unsigned>(wifi_password_.size()));
      canvas.draw_text(82, 3, line, true, 44);
    }
    break;
  }
  case UiScreen::kHistory: {
    const bool large_history = chinese || std::any_of(
        history_.begin(), history_.end(), [](const HistoryMessage &message) {
          return std::any_of(message.content.begin(), message.content.end(),
                             [](unsigned char ch) { return ch >= 0x80; });
        });
    header(canvas, chinese ? "历史" : "HISTORY", large_history);
    if (history_.empty()) {
      if (chinese)
        canvas.draw_utf8_text(3, 24, "无消息");
      else
        canvas.draw_text(3, 24, "NO MESSAGES");
    }
    const std::size_t page_size = large_history ? 3 : 4;
    const std::size_t start = history_selected_ / page_size * page_size;
    for (std::size_t i = 0; i < page_size && start + i < history_.size(); ++i) {
      const auto &message = history_[start + i];
        std::snprintf(line, sizeof(line), "%c%lu %s",
                      message.assistant ? 'A' : 'U',
                      static_cast<unsigned long>(message.sequence),
                      compact(message.content, chinese ? 18 : 13).c_str());
      const std::string row_text = large_history
                                       ? utf8_prefix_cells(line, 7)
                                       : std::string(line);
      row(canvas, large_history ? 16 + static_cast<int>(i) * 16
                                : 12 + static_cast<int>(i) * 10,
          row_text.c_str(), start + i == history_selected_, large_history);
    }
    if (!large_history) {
      std::snprintf(line, sizeof(line), "%u MSG%s",
                    static_cast<unsigned>(history_.size()),
                    next_cursor_.empty() ? "" : " +");
      canvas.draw_text(3, 55, line);
    }
    break;
  }
  case UiScreen::kHistoryText: {
    const auto *message = selected_message();
    const bool large_text = chinese ||
        (message != nullptr && std::any_of(
            message->content.begin(), message->content.end(),
            [](unsigned char ch) { return ch >= 0x80; }));
    header(canvas, chinese ? "用户消息" : "USER MESSAGE", large_text);
    if (message != nullptr) {
      const auto text = compact(message->content, message->content.size());
      if (large_text) {
        std::size_t offset = utf8_offset_cells(text, history_text_scroll_ * 7);
        for (int i = 0; i < 3 && offset < text.size(); ++i) {
          const auto line = text.substr(offset);
          const auto end = offset + utf8_prefix_cells(line, 7).size();
          canvas.draw_utf8_text(3, 16 + i * 16,
                                text.substr(offset, end - offset).c_str(), true, 120);
          offset = end;
        }
      } else {
        for (int i = 0; i < 4; ++i) {
          const auto offset = (history_text_scroll_ + i) * 20;
          if (offset < text.size())
            canvas.draw_text(3, 13 + i * 10,
                             text.substr(offset, 20).c_str());
        }
        std::snprintf(line, sizeof(line), "MSG %lu",
                      static_cast<unsigned long>(message->sequence));
        canvas.draw_text(3, 55, line);
      }
    }
    break;
  }
  case UiScreen::kSettings: {
    header(canvas, chinese ? "设置" : "SETTINGS");
    const std::size_t page_size = chinese ? 3 : 4;
    const auto start = settings_selected_ / page_size * page_size;
    for (std::size_t i = 0; i < page_size; ++i) {
      const auto index = start + i;
      const int y = chinese ? 16 + static_cast<int>(i) * 16
                            : 12 + static_cast<int>(i) * 10;
      if (index == 0) {
        const auto model = std::find_if(
            models.begin(), models.end(), [&](const ModelInfo &candidate) {
              return candidate.id == settings.model_id;
            });
        const std::string &name = model == models.end()
                                      ? settings.model_id
                                      : model->name;
        const bool selected = index == settings_selected_;
        canvas.fill_rect(0, y, 128, chinese ? 16 : 10, selected);
        if (chinese) {
          canvas.draw_utf8_text(3, y, "模型", !selected, 32);
          canvas.draw_utf8_text(40, y,
                                utf8_marquee(name,
                                             selected ? settings_name_scroll_ : 0,
                                             88).c_str(),
                                !selected, 88);
        } else {
          canvas.draw_text(4, y + 1, "MODEL", !selected, 34);
          canvas.draw_text(42, y + 1,
                           ascii_marquee(name,
                                         selected ? settings_name_scroll_ : 0,
                                         14).c_str(),
                           !selected, 84);
        }
        continue;
      }
      std::string value;
      switch (index) {
      case 1:
        value = chinese ? "默认" : "DEFAULT";
        for (const auto &model : models)
          if (model.id == settings.model_id) {
            value = reasoning_label(model, settings.reasoning_level, chinese);
            break;
          }
        break;
      case 2: value = settings.use_context ? (chinese ? "开" : "ON")
                                           : (chinese ? "关" : "OFF"); break;
      case 3: value = settings.printer.auto_print ? (chinese ? "开" : "ON")
                                                  : (chinese ? "关" : "OFF"); break;
      case 4: value = std::to_string(settings.render.font_size); break;
      case 5:
        std::snprintf(line, sizeof(line), "%.2f", settings.render.line_height);
        value = line;
        break;
      case 6: value = std::to_string(settings.render.bottom_feed); break;
      case 7: value = wifi_connected ? (chinese ? "开" : "ON")
                                     : (chinese ? "关" : "OFF"); break;
      case 8:
        value = settings.ui_language == UiLanguage::kChinese ? "中文" : "EN";
        break;
      }
      std::snprintf(line, sizeof(line), "%s %s",
                    chinese ? kSettingNamesZh[index] : kSettingNames[index],
                    value.c_str());
      row(canvas, y, line, index == settings_selected_, chinese);
    }
    if (!chinese) {
      const auto page = settings_selected_ / page_size + 1;
      std::snprintf(line, sizeof(line), "%u/3", static_cast<unsigned>(page));
      canvas.draw_text(3, 55, line);
    } else {
      const auto page = settings_selected_ / page_size + 1;
      std::snprintf(line, sizeof(line), "%u/3", static_cast<unsigned>(page));
      canvas.draw_text(100, 4, line);
    }
    break;
  }
  case UiScreen::kPreview:
    break;
  case UiScreen::kConfirmClear:
    header(canvas, chinese ? "清空历史？" : "CLEAR HISTORY?");
    row(canvas, chinese ? 24 : 22, chinese ? "否" : "NO", !clear_yes_, chinese);
    row(canvas, chinese ? 44 : 34, chinese ? "是" : "YES", clear_yes_, chinese);
    break;
  case UiScreen::kNotice:
    header(canvas, chinese ? "提示" : "NOTICE");
    if (chinese) {
      const char *translated = notice_.c_str();
      if (notice_ == "NO REPLY LOADED") translated = "无回复";
      else if (notice_ == "SAVE FAILED") translated = "保存失败";
      else if (notice_ == "PRINTER NOT READY") translated = "打印机未就绪";
      else if (notice_ == "PRINT FAILED") translated = "打印失败";
      else if (notice_ == "WIFI OFFLINE") translated = "网络离线";
      else if (notice_ == "WIFI CONNECTED") translated = "连接成功";
      else if (notice_ == "WIFI CONNECTING") translated = "连接中";
      else if (notice_ == "WIFI FAILED") translated = "连接失败";
      else if (notice_ == "WIFI SCANNING") translated = "扫描中";
      else if (notice_ == "WIFI SCAN FAILED") translated = "扫描失败";
      else if (notice_ == "PASSWORD TOO SHORT") translated = "密码过短";
      else if (notice_ == "RETRYING...") translated = "重试中";
      else if (notice_ == "RETRY FAILED") translated = "重试失败";
      else if (notice_ == "NO MESSAGE TO RETRY") translated = "无可重试消息";
      else if (notice_ == "SENDING...") translated = "发送中";
      canvas.draw_utf8_text(3, 25, translated);
    } else {
      canvas.draw_text(3, 25, compact(notice_, 20).c_str());
    }
    break;
  }
}

} // namespace thermal_terminal
