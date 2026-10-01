#pragma once

#include <cstddef>
#include <cstdint>

#include "tpb1/parser.hpp"

namespace thermal_terminal {

class BitmapStore final : public tpb1::Sink {
public:
  BitmapStore() = default;
  ~BitmapStore() override;

  BitmapStore(const BitmapStore &) = delete;
  BitmapStore &operator=(const BitmapStore &) = delete;

  bool begin_document(const tpb1::DocumentInfo &info) override;
  bool write_band(const tpb1::BandInfo &info, const std::uint8_t *data,
                  std::size_t size) override;
  bool end_document() override;

  void reset();
  [[nodiscard]] bool ready() const;
  [[nodiscard]] const std::uint8_t *data() const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] const tpb1::DocumentInfo &document() const;
  [[nodiscard]] const std::uint8_t *row(std::uint32_t y) const;

private:
  std::uint8_t *bytes_{};
  std::size_t size_{};
  std::size_t written_{};
  std::uint16_t next_band_{};
  bool ready_{};
  tpb1::DocumentInfo document_{};
};

} // namespace thermal_terminal
