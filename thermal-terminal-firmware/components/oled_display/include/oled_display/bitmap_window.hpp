#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace thermal_terminal {

// SSD1306 pages: one byte per column, bit 0 at the top of each 8-pixel page.
class BitmapWindow {
public:
  static constexpr std::uint16_t kWidth = 128;
  static constexpr std::uint16_t kHeight = 64;
  static constexpr std::size_t kBufferSize = kWidth * kHeight / 8;

  void clear();
  void set_pixel(std::uint16_t x, std::uint16_t y, bool on);
  void fill_rect(int x, int y, int width, int height, bool on);
  void draw_text(int x, int y, const char *text, bool on = true,
                 int max_width = kWidth);
  void draw_utf8_text(int x, int y, const char *text, bool on = true,
                      int max_width = kWidth);
  bool draw_tpb_window(const std::uint8_t *bitmap, std::size_t bitmap_size,
                       std::uint16_t source_width, std::uint32_t source_height,
                       std::uint16_t x, std::uint32_t y);
  [[nodiscard]] const std::array<std::uint8_t, kBufferSize> &data() const;

private:
  std::array<std::uint8_t, kBufferSize> pixels_{};
};

} // namespace thermal_terminal
