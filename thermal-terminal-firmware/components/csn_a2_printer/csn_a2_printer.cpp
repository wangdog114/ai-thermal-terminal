#include "csn_a2_printer/csn_a2_printer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>

#include "driver/uart.h"
#include "esp_log.h"

namespace thermal_terminal {
namespace {

constexpr char kTag[] = "csn-a2";
constexpr std::uint16_t kPrintWidth = 384;
constexpr std::size_t kRowBytes = kPrintWidth / 8;
constexpr int kMinimumTimeoutMs = 3000;

} // namespace

CsnA2Printer::CsnA2Printer(int uart_num, int tx_gpio, int rx_gpio, int baudrate,
                           std::uint8_t density)
    : uart_num_(uart_num), tx_gpio_(tx_gpio), rx_gpio_(rx_gpio),
      baudrate_(baudrate), density_(density) {}

CsnA2Printer::~CsnA2Printer() {
  if (ready_) {
    uart_wait_tx_done(static_cast<uart_port_t>(uart_num_), pdMS_TO_TICKS(1000));
    uart_driver_delete(static_cast<uart_port_t>(uart_num_));
  }
}

bool CsnA2Printer::initialize() {
  if (ready_)
    return true;
  const auto uart = static_cast<uart_port_t>(uart_num_);
  uart_config_t config{};
  config.baud_rate = baudrate_;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  if (uart_param_config(uart, &config) != ESP_OK ||
      uart_set_pin(uart, tx_gpio_, rx_gpio_, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK ||
      uart_driver_install(uart, 4096, 0, 0, nullptr, 0) != ESP_OK) {
    ESP_LOGE(kTag, "UART initialization failed");
    return false;
  }
  ready_ = true;
  if (!send_command({0x1B, 0x40})) {
    ready_ = false;
    uart_driver_delete(uart);
    return false;
  }
  ESP_LOGI(kTag, "CSN-A2 UART%d ready TX=%d RX=%d baud=%d", uart_num_, tx_gpio_,
           rx_gpio_, baudrate_);
  return true;
}

bool CsnA2Printer::write_bytes(const std::uint8_t *data, std::size_t size) {
  if (!ready_ || data == nullptr || size == 0)
    return false;
  const int written =
      uart_write_bytes(static_cast<uart_port_t>(uart_num_), data, size);
  return written == static_cast<int>(size);
}

bool CsnA2Printer::send_command(std::initializer_list<std::uint8_t> command) {
  return write_bytes(command.begin(), command.size());
}

bool CsnA2Printer::wait_tx(std::size_t bytes, int extra_ms) {
  // A UART frame is 10 bits in 8N1 mode.  uart_write_bytes() can return
  // before the last byte has physically left the UART, especially when the
  // driver has no TX ring buffer.  Give the printer enough time at low baud
  // rates instead of reporting a false print failure after three seconds.
  const std::uint64_t wire_ms =
      (static_cast<std::uint64_t>(bytes) * 10U * 1000U +
       static_cast<std::uint64_t>(baudrate_) - 1U) /
      static_cast<std::uint64_t>(baudrate_);
  const int timeout_ms = std::max<int>(
      kMinimumTimeoutMs,
      static_cast<int>(std::min<std::uint64_t>(wire_ms + extra_ms, 120000U)));
  return uart_wait_tx_done(static_cast<uart_port_t>(uart_num_),
                           pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}

bool CsnA2Printer::print_rows(const std::uint8_t *bitmap, std::size_t size,
                              std::uint16_t rows) {
  if (!ready_ || bitmap == nullptr || rows == 0 ||
      size != static_cast<std::size_t>(rows) * kRowBytes) {
    return false;
  }

  // CSN-A2 manual, section 8.2.4: DC2 V nL nH [d1...d48].  The width is
  // fixed to the mechanism width (48 bytes = 384 dots), and each command
  // carries one raster block.  This avoids the generic GS v 0 parser found
  // on some firmware revisions, which otherwise treats the payload as text.
  const std::array<std::uint8_t, 4> header = {
      0x12,
      0x56,
      static_cast<std::uint8_t>(rows & 0xFF),
      static_cast<std::uint8_t>((rows >> 8) & 0xFF)};
  if (!write_bytes(header.data(), header.size()) ||
      !write_bytes(bitmap, size)) {
    return false;
  }
  return wait_tx(header.size() + size);
}

bool CsnA2Printer::print_rows_gsv0(const std::uint8_t *bitmap,
                                   std::size_t size, std::uint16_t rows) {
  if (!ready_ || bitmap == nullptr || rows == 0 ||
      size != static_cast<std::size_t>(rows) * kRowBytes) {
    return false;
  }

  // Generic ESC/POS GS v 0 form, retained as a diagnostic fallback for
  // modules whose firmware was replaced with a standard ESC/POS image.
  const std::array<std::uint8_t, 8> header = {
      0x1D, 0x76, 0x30, 0x00,
      static_cast<std::uint8_t>(kRowBytes & 0xFF),
      static_cast<std::uint8_t>((kRowBytes >> 8) & 0xFF),
      static_cast<std::uint8_t>(rows & 0xFF),
      static_cast<std::uint8_t>((rows >> 8) & 0xFF)};
  if (!write_bytes(header.data(), header.size()) ||
      !write_bytes(bitmap, size)) {
    return false;
  }
  return wait_tx(header.size() + size);
}

bool CsnA2Printer::feed(std::uint16_t dots) {
  if (!ready_)
    return false;
  while (dots > 0) {
    const std::uint8_t amount =
        static_cast<std::uint8_t>(std::min<std::uint16_t>(dots, 255));
    if (!send_command({0x1B, 0x4A, amount}))
      return false;
    dots -= amount;
  }
  return wait_tx(3);
}

void CsnA2Printer::cancel() {
  if (ready_)
    uart_flush_input(static_cast<uart_port_t>(uart_num_));
}

bool CsnA2Printer::print_test_pattern() {
  std::array<std::uint8_t, kRowBytes * 64> bitmap{};
  for (std::size_t row = 0; row < 64; ++row) {
    for (std::size_t column = 0; column < kPrintWidth; ++column) {
      if (row == 0 || row == 63 || column == 0 || column == 383 ||
          column == row || column == 383 - row) {
        bitmap[row * kRowBytes + column / 8] |=
            static_cast<std::uint8_t>(0x80U >> (column & 7));
      }
    }
  }
  return print_rows(bitmap.data(), bitmap.size(), 64) && feed(24);
}

bool CsnA2Printer::print_text(const char *text) {
  if (!ready_ || text == nullptr)
    return false;
  const auto size = std::strlen(text);
  if (size == 0)
    return false;
  if (!write_bytes(reinterpret_cast<const std::uint8_t *>(text), size) ||
      !send_command({0x0A})) {
    return false;
  }
  return wait_tx(size + 1);
}

bool CsnA2Printer::print_test_page() {
  if (!ready_ || !send_command({0x12, 0x54}))
    return false;
  return wait_tx(2, 2000);
}

bool CsnA2Printer::query_status(std::uint8_t &status) {
  if (!ready_)
    return false;
  uart_flush_input(static_cast<uart_port_t>(uart_num_));
  if (!send_command({0x1B, 0x76, 0x00}) || !wait_tx(3))
    return false;
  std::uint8_t response = 0;
  const int received = uart_read_bytes(
      static_cast<uart_port_t>(uart_num_), &response, 1, pdMS_TO_TICKS(750));
  if (received != 1)
    return false;
  status = response;
  return true;
}

bool CsnA2Printer::set_baudrate(int baudrate) {
  if (baudrate < 1200 || baudrate > 921600)
    return false;
  baudrate_ = baudrate;
  if (!ready_)
    return true;
  return uart_set_baudrate(static_cast<uart_port_t>(uart_num_), baudrate) ==
         ESP_OK;
}

int CsnA2Printer::baudrate() const { return baudrate_; }

bool CsnA2Printer::ready() const { return ready_; }

} // namespace thermal_terminal
