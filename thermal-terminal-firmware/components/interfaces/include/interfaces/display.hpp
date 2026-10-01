#pragma once

#include <cstddef>
#include <cstdint>

namespace thermal_terminal {

class Display {
public:
  virtual ~Display() = default;
  virtual bool initialize() = 0;
  virtual void clear() = 0;
  virtual void present() = 0;
  virtual void draw_bitmap_window(const std::uint8_t *bitmap,
                                  std::size_t bitmap_size,
                                  std::uint16_t source_width,
                                  std::uint32_t source_height, std::uint16_t x,
                                  std::uint32_t y) = 0;
};

} // namespace thermal_terminal
