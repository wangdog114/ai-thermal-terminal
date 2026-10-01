#include "oled_display/ssd1306_display.hpp"

#include <array>
#include <cstring>

#include "esp_log.h"

namespace thermal_terminal {
namespace {
constexpr char kTag[] = "ssd1306";
constexpr int kI2cTimeoutMs = 500;
} // namespace

Ssd1306Display::Ssd1306Display(int sda_gpio, int scl_gpio, std::uint8_t address)
    : sda_gpio_(sda_gpio), scl_gpio_(scl_gpio), address_(address) {}

Ssd1306Display::~Ssd1306Display() { release(); }

void Ssd1306Display::release() {
  if (device_ != nullptr) {
    i2c_master_bus_rm_device(device_);
    device_ = nullptr;
  }
  if (bus_ != nullptr) {
    i2c_del_master_bus(bus_);
    bus_ = nullptr;
  }
}

bool Ssd1306Display::send_command(std::uint8_t command) {
  const std::uint8_t packet[] = {0x00, command};
  return device_ != nullptr &&
         i2c_master_transmit(device_, packet, sizeof(packet), kI2cTimeoutMs) ==
             ESP_OK;
}

bool Ssd1306Display::initialize() {
  if (ready())
    return true;
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = static_cast<gpio_num_t>(sda_gpio_);
  bus_config.scl_io_num = static_cast<gpio_num_t>(scl_gpio_);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK)
    return false;
  const std::uint8_t preferred_address = address_;
  if (i2c_master_probe(bus_, address_, kI2cTimeoutMs) != ESP_OK) {
    const std::uint8_t alternate_address = address_ == 0x3D   ? 0x3C
                                           : address_ == 0x3C ? 0x3D
                                                              : 0;
    if (alternate_address == 0 ||
        i2c_master_probe(bus_, alternate_address, kI2cTimeoutMs) != ESP_OK) {
      ESP_LOGW(kTag, "no display at I2C addresses 0x%02X/0x%02X",
               preferred_address, alternate_address);
      release();
      return false;
    }
    address_ = alternate_address;
    ESP_LOGW(kTag, "0x%02X did not respond; using 0x%02X", preferred_address,
             address_);
  }
  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = address_;
  device_config.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus_, &device_config, &device_) != ESP_OK) {
    release();
    return false;
  }

  constexpr std::uint8_t kInit[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00,
                                    0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
                                    0xDA, 0x12, 0x81, 0x7F, 0xD9, 0xF1, 0xDB,
                                    0x40, 0xA4, 0xA6, 0xAF};
  for (const auto command : kInit) {
    if (!send_command(command)) {
      release();
      return false;
    }
  }
  clear();
  present();
  ESP_LOGI(kTag, "128x64 I2C display ready at 0x%02X on GPIO%d/GPIO%d",
           address_, sda_gpio_, scl_gpio_);
  return true;
}

void Ssd1306Display::clear() { framebuffer_.clear(); }

void Ssd1306Display::present() {
  if (!ready())
    return;
  constexpr std::uint8_t kAddressCommands[] = {0x21, 0x00, 0x7F,
                                               0x22, 0x00, 0x07};
  for (const auto command : kAddressCommands) {
    if (!send_command(command)) {
      ESP_LOGW(kTag, "I2C command failed");
      return;
    }
  }
  std::array<std::uint8_t, 129> packet{};
  packet[0] = 0x40;
  for (std::size_t page = 0; page < 8; ++page) {
    std::memcpy(packet.data() + 1,
                framebuffer_.data().data() + page * BitmapWindow::kWidth,
                BitmapWindow::kWidth);
    if (i2c_master_transmit(device_, packet.data(), packet.size(),
                            kI2cTimeoutMs) != ESP_OK) {
      ESP_LOGW(kTag, "I2C frame transfer failed");
      return;
    }
  }
}

void Ssd1306Display::draw_bitmap_window(const std::uint8_t *bitmap,
                                        std::size_t bitmap_size,
                                        std::uint16_t source_width,
                                        std::uint32_t source_height,
                                        std::uint16_t x, std::uint32_t y) {
  if (!framebuffer_.draw_tpb_window(bitmap, bitmap_size, source_width,
                                    source_height, x, y)) {
    ESP_LOGW(kTag, "invalid preview window");
  }
}

void Ssd1306Display::show_test_pattern() {
  framebuffer_.clear();
  for (std::uint16_t x = 0; x < BitmapWindow::kWidth; ++x) {
    framebuffer_.set_pixel(x, 0, true);
    framebuffer_.set_pixel(x, BitmapWindow::kHeight - 1, true);
  }
  for (std::uint16_t y = 0; y < BitmapWindow::kHeight; ++y) {
    framebuffer_.set_pixel(0, y, true);
    framebuffer_.set_pixel(BitmapWindow::kWidth - 1, y, true);
    framebuffer_.set_pixel(y * 2, y, true);
  }
  present();
}

std::vector<std::uint8_t> Ssd1306Display::scan_addresses() {
  bool temporary_bus = bus_ == nullptr;
  if (temporary_bus) {
    i2c_master_bus_config_t config{};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = static_cast<gpio_num_t>(sda_gpio_);
    config.scl_io_num = static_cast<gpio_num_t>(scl_gpio_);
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&config, &bus_) != ESP_OK)
      return {};
  }
  std::vector<std::uint8_t> found;
  for (std::uint16_t address = 0x08; address <= 0x7F; ++address) {
    if (i2c_master_probe(bus_, address, 20) == ESP_OK) {
      found.push_back(static_cast<std::uint8_t>(address));
    }
  }
  if (temporary_bus)
    release();
  return found;
}

bool Ssd1306Display::ready() const { return device_ != nullptr; }

BitmapWindow &Ssd1306Display::canvas() { return framebuffer_; }

} // namespace thermal_terminal
