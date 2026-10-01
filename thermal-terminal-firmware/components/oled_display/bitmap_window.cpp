#include "oled_display/bitmap_window.hpp"

#include <algorithm>

#include "oled_display/unifont_data.hpp"

namespace thermal_terminal {
namespace {

// Five columns per glyph, least significant bit at the top. Unsupported
// bytes are rendered as '?' rather than half a UTF-8 sequence.
constexpr std::uint8_t kLetters[][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x41, 0x41, 0x7F, 0x41, 0x41}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x26, 0x49, 0x49, 0x49, 0x32}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43}};
constexpr std::uint8_t kDigits[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}};
constexpr std::uint8_t kLowerLetters[][5] = {
    {0x20, 0x54, 0x54, 0x54, 0x78}, {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20}, {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x08, 0x14, 0x54, 0x54, 0x3C}, {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0, 0x44, 0x7D, 0x40, 0}, {0x20, 0x40, 0x44, 0x3D, 0},
    {0x7F, 0x10, 0x28, 0x44, 0}, {0, 0x41, 0x7F, 0x40, 0},
    {0x7C, 0x04, 0x18, 0x04, 0x78}, {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C}, {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20}, {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, {0x44, 0x64, 0x54, 0x4C, 0x44}};
constexpr std::uint8_t kQuestion[5] = {0x02, 0x01, 0x51, 0x09, 0x06};

const std::uint8_t *glyph(unsigned char ch) {
  if (ch >= 'a' && ch <= 'z')
    return kLowerLetters[ch - 'a'];
  if (ch >= 'A' && ch <= 'Z')
    return kLetters[ch - 'A'];
  if (ch >= '0' && ch <= '9')
    return kDigits[ch - '0'];
  switch (ch) {
  case ' ': { static constexpr std::uint8_t data[5]{}; return data; }
  case '-': { static constexpr std::uint8_t data[5]{0, 8, 8, 8, 0}; return data; }
  case '.': { static constexpr std::uint8_t data[5]{0, 0x60, 0x60, 0, 0}; return data; }
  case ',': { static constexpr std::uint8_t data[5]{0, 0x40, 0x20, 0, 0}; return data; }
  case ':': { static constexpr std::uint8_t data[5]{0, 0x36, 0x36, 0, 0}; return data; }
  case ';': { static constexpr std::uint8_t data[5]{0, 0x56, 0x36, 0, 0}; return data; }
  case '"': { static constexpr std::uint8_t data[5]{0, 0x07, 0, 0x07, 0}; return data; }
  case '\'': { static constexpr std::uint8_t data[5]{0, 0, 0x07, 0, 0}; return data; }
  case '(': { static constexpr std::uint8_t data[5]{0, 0x1C, 0x22, 0x41, 0}; return data; }
  case ')': { static constexpr std::uint8_t data[5]{0, 0x41, 0x22, 0x1C, 0}; return data; }
  case '/': { static constexpr std::uint8_t data[5]{0x20, 0x10, 8, 4, 2}; return data; }
  case '\\': { static constexpr std::uint8_t data[5]{2, 4, 8, 0x10, 0x20}; return data; }
  case '_': { static constexpr std::uint8_t data[5]{0x40, 0x40, 0x40, 0x40, 0x40}; return data; }
  case '+': { static constexpr std::uint8_t data[5]{8, 8, 0x3E, 8, 8}; return data; }
  case '=': { static constexpr std::uint8_t data[5]{0x14, 0x14, 0x14, 0x14, 0x14}; return data; }
  case '>': { static constexpr std::uint8_t data[5]{0, 0x41, 0x22, 0x14, 8}; return data; }
  case '<': { static constexpr std::uint8_t data[5]{8, 0x14, 0x22, 0x41, 0}; return data; }
  case '!': { static constexpr std::uint8_t data[5]{0, 0, 0x5F, 0, 0}; return data; }
  case '@': { static constexpr std::uint8_t data[5]{0x3E, 0x41, 0x5D, 0x55, 0x1E}; return data; }
  case '#': { static constexpr std::uint8_t data[5]{0x14, 0x7F, 0x14, 0x7F, 0x14}; return data; }
  case '%': { static constexpr std::uint8_t data[5]{0x63, 0x13, 8, 0x64, 0x63}; return data; }
  case '&': { static constexpr std::uint8_t data[5]{0x36, 0x49, 0x55, 0x22, 0x50}; return data; }
  case '*': { static constexpr std::uint8_t data[5]{0x14, 8, 0x3E, 8, 0x14}; return data; }
  case '[': { static constexpr std::uint8_t data[5]{0, 0x7F, 0x41, 0x41, 0}; return data; }
  case ']': { static constexpr std::uint8_t data[5]{0, 0x41, 0x41, 0x7F, 0}; return data; }
  case '{': { static constexpr std::uint8_t data[5]{8, 0x36, 0x41, 0x41, 0}; return data; }
  case '}': { static constexpr std::uint8_t data[5]{0, 0x41, 0x41, 0x36, 8}; return data; }
  case '|': { static constexpr std::uint8_t data[5]{0, 0, 0x7F, 0, 0}; return data; }
  case '~': { static constexpr std::uint8_t data[5]{8, 4, 8, 0x10, 8}; return data; }
  case '`': { static constexpr std::uint8_t data[5]{0, 1, 2, 4, 0}; return data; }
  case '^': { static constexpr std::uint8_t data[5]{4, 2, 1, 2, 4}; return data; }
  case '?': return kQuestion;
  default: return kQuestion;
  }
}

const unifont_data::Glyph *unicode_glyph(std::uint32_t codepoint) {
  std::size_t first = 0;
  std::size_t last = unifont_data::kGlyphCount;
  while (first < last) {
    const auto middle = first + (last - first) / 2;
    if (unifont_data::kGlyphs[middle].codepoint < codepoint)
      first = middle + 1;
    else
      last = middle;
  }
  if (first < unifont_data::kGlyphCount &&
      unifont_data::kGlyphs[first].codepoint == codepoint)
    return &unifont_data::kGlyphs[first];
  return nullptr;
}

bool decode_utf8(const char *text, std::uint32_t &codepoint, std::size_t &length) {
  const auto first = static_cast<std::uint8_t>(text[0]);
  if (first < 0x80) {
    codepoint = first;
    length = 1;
    return true;
  }
  if ((first & 0xE0) == 0xC0 && text[1] != '\0' &&
      (static_cast<std::uint8_t>(text[1]) & 0xC0) == 0x80) {
    codepoint = ((first & 0x1F) << 6) | (static_cast<std::uint8_t>(text[1]) & 0x3F);
    length = 2;
    return codepoint >= 0x80;
  }
  if ((first & 0xF0) == 0xE0 &&
      text[1] != '\0' && text[2] != '\0' &&
      (static_cast<std::uint8_t>(text[1]) & 0xC0) == 0x80 &&
      (static_cast<std::uint8_t>(text[2]) & 0xC0) == 0x80) {
    codepoint = ((first & 0x0F) << 12) |
                ((static_cast<std::uint8_t>(text[1]) & 0x3F) << 6) |
                (static_cast<std::uint8_t>(text[2]) & 0x3F);
    length = 3;
    return codepoint >= 0x800;
  }
  if ((first & 0xF8) == 0xF0 &&
      text[1] != '\0' && text[2] != '\0' && text[3] != '\0' &&
      (static_cast<std::uint8_t>(text[1]) & 0xC0) == 0x80 &&
      (static_cast<std::uint8_t>(text[2]) & 0xC0) == 0x80 &&
      (static_cast<std::uint8_t>(text[3]) & 0xC0) == 0x80) {
    codepoint = ((first & 0x07) << 18) |
                ((static_cast<std::uint8_t>(text[1]) & 0x3F) << 12) |
                ((static_cast<std::uint8_t>(text[2]) & 0x3F) << 6) |
                (static_cast<std::uint8_t>(text[3]) & 0x3F);
    length = 4;
    return codepoint >= 0x10000 && codepoint <= 0x10FFFF;
  }
  codepoint = '?';
  length = 1;
  return false;
}

} // namespace

void BitmapWindow::clear() { pixels_.fill(0); }

void BitmapWindow::set_pixel(std::uint16_t x, std::uint16_t y, bool on) {
  if (x >= kWidth || y >= kHeight)
    return;
  const std::size_t offset = static_cast<std::size_t>(y / 8) * kWidth + x;
  const std::uint8_t mask = static_cast<std::uint8_t>(1U << (y & 7));
  if (on) {
    pixels_[offset] |= mask;
  } else {
    pixels_[offset] &= static_cast<std::uint8_t>(~mask);
  }
}

void BitmapWindow::fill_rect(int x, int y, int width, int height, bool on) {
  for (int row = std::max(0, y); row < std::min<int>(kHeight, y + height); ++row) {
    for (int col = std::max(0, x); col < std::min<int>(kWidth, x + width); ++col)
      set_pixel(col, row, on);
  }
}

void BitmapWindow::draw_text(int x, int y, const char *text, bool on,
                             int max_width) {
  if (text == nullptr || max_width <= 0)
    return;
  const int end_x = std::min<int>(kWidth, x + max_width);
  while (*text != '\0' && x + 5 <= end_x) {
    const auto *columns = glyph(static_cast<unsigned char>(*text++));
    for (int col = 0; col < 5; ++col) {
      for (int row = 0; row < 7; ++row) {
        if ((columns[col] & (1U << row)) && x + col >= 0 && y + row >= 0 &&
            x + col < kWidth && y + row < kHeight)
          set_pixel(x + col, y + row, on);
      }
    }
    x += 6;
  }
}

void BitmapWindow::draw_utf8_text(int x, int y, const char *text, bool on,
                                  int max_width) {
  if (text == nullptr || max_width <= 0)
    return;
  const int end_x = std::min<int>(kWidth, x + max_width);
  while (*text != '\0' && x < end_x) {
    std::uint32_t codepoint = 0;
    std::size_t length = 1;
    const bool valid = decode_utf8(text, codepoint, length);
    const int cell_width = codepoint < 0x80 ? 8 : 16;
    if (x + cell_width > end_x)
      break;
    const auto *font_glyph = valid ? unicode_glyph(codepoint) : nullptr;
    if (font_glyph == nullptr)
      font_glyph = unicode_glyph('?');
    if (font_glyph != nullptr) {
      for (int row = 0; row < 16; ++row) {
        const auto bits = font_glyph->rows[static_cast<std::size_t>(row)];
        for (int col = 0; col < cell_width; ++col) {
          const auto mask = codepoint < 0x80 ? (0x0080U >> col)
                                             : (0x8000U >> col);
          if ((bits & mask) != 0 && x + col >= 0 && y + row >= 0 &&
              x + col < kWidth && y + row < kHeight)
            set_pixel(x + col, y + row, on);
        }
      }
    }
    x += cell_width;
    text += length;
  }
}

bool BitmapWindow::draw_tpb_window(const std::uint8_t *bitmap,
                                   std::size_t bitmap_size,
                                   std::uint16_t source_width,
                                   std::uint32_t source_height, std::uint16_t x,
                                   std::uint32_t y) {
  clear();
  if (bitmap == nullptr || source_width < kWidth || source_width % 8 != 0 ||
      source_height == 0 || x > source_width - kWidth || y >= source_height) {
    return false;
  }
  const std::size_t row_bytes = source_width / 8;
  if (source_height > bitmap_size / row_bytes)
    return false;

  const std::uint32_t rows =
      std::min<std::uint32_t>(kHeight, source_height - y);
  for (std::uint32_t row = 0; row < rows; ++row) {
    const auto *source = bitmap + static_cast<std::size_t>(y + row) * row_bytes;
    for (std::uint16_t column = 0; column < kWidth; ++column) {
      const std::uint16_t source_x = x + column;
      const bool black =
          (source[source_x / 8] & (0x80U >> (source_x & 7))) != 0;
      set_pixel(column, static_cast<std::uint16_t>(row), black);
    }
  }
  return true;
}

const std::array<std::uint8_t, BitmapWindow::kBufferSize> &
BitmapWindow::data() const {
  return pixels_;
}

} // namespace thermal_terminal
