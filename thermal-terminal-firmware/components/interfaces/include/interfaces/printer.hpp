#pragma once

#include <cstddef>
#include <cstdint>

namespace thermal_terminal {

class Printer {
public:
  virtual ~Printer() = default;
  virtual bool initialize() = 0;
  virtual bool ready() const = 0;
  virtual bool print_rows(const std::uint8_t *bitmap, std::size_t size,
                          std::uint16_t rows) = 0;
  virtual bool feed(std::uint16_t dots) = 0;
  virtual void cancel() = 0;
};

} // namespace thermal_terminal
