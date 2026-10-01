#pragma once

#include <cstdint>
#include <vector>

#include "driver/i2c_master.h"
#include "interfaces/display.hpp"
#include "oled_display/bitmap_window.hpp"

namespace thermal_terminal {

class Ssd1306Display final : public Display {
public:
  Ssd1306Display(int sda_gpio, int scl_gpio, std::uint8_t address = 0x3C);
  ~Ssd1306Display() override;

  Ssd1306Display(const Ssd1306Display &) = delete;
  Ssd1306Display &operator=(const Ssd1306Display &) = delete;

  bool initialize() override;
  void clear() override;
  void present() override;
  void draw_bitmap_window(const std::uint8_t *bitmap, std::size_t bitmap_size,
                          std::uint16_t source_width,
                          std::uint32_t source_height, std::uint16_t x,
                          std::uint32_t y) override;
  void show_test_pattern();
  std::vector<std::uint8_t> scan_addresses();
  [[nodiscard]] bool ready() const;
  [[nodiscard]] BitmapWindow &canvas();

private:
  bool send_command(std::uint8_t command);
  void release();

  int sda_gpio_;
  int scl_gpio_;
  std::uint8_t address_;
  i2c_master_bus_handle_t bus_{};
  i2c_master_dev_handle_t device_{};
  BitmapWindow framebuffer_;
};

} // namespace thermal_terminal
