#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "interfaces/printer.hpp"

namespace thermal_terminal {

class CsnA2Printer final : public Printer {
public:
  CsnA2Printer(int uart_num, int tx_gpio, int rx_gpio, int baudrate,
               std::uint8_t density);
  ~CsnA2Printer() override;

  CsnA2Printer(const CsnA2Printer &) = delete;
  CsnA2Printer &operator=(const CsnA2Printer &) = delete;

  bool initialize() override;
  bool ready() const override;
  bool print_rows(const std::uint8_t *bitmap, std::size_t size,
                  std::uint16_t rows) override;
  bool feed(std::uint16_t dots) override;
  void cancel() override;

  // Diagnostics and configuration helpers.  CSN-A2 exposes a text path and
  // a built-in test-page command in addition to raster printing.
  bool print_text(const char *text);
  bool print_test_page();
  bool query_status(std::uint8_t &status);
  bool print_rows_gsv0(const std::uint8_t *bitmap, std::size_t size,
                       std::uint16_t rows);
  bool set_baudrate(int baudrate);
  [[nodiscard]] int baudrate() const;
  bool print_test_pattern();

private:
  bool write_bytes(const std::uint8_t *data, std::size_t size);
  bool send_command(std::initializer_list<std::uint8_t> command);
  bool wait_tx(std::size_t bytes, int extra_ms = 1000);

  int uart_num_;
  int tx_gpio_;
  int rx_gpio_;
  int baudrate_;
  std::uint8_t density_;
  bool ready_{};
};

} // namespace thermal_terminal
