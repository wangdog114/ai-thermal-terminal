#pragma once

#include <array>
#include <cstdint>

#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nec_input/nec_decoder.hpp"

namespace thermal_terminal {

class NecReceiver {
public:
  explicit NecReceiver(int gpio);
  ~NecReceiver();

  NecReceiver(const NecReceiver &) = delete;
  NecReceiver &operator=(const NecReceiver &) = delete;

  bool initialize();
  bool read(NecFrame &frame, std::uint32_t timeout_ms);

private:
  void release();
  static bool on_receive(rmt_channel_handle_t channel,
                         const rmt_rx_done_event_data_t *data, void *context);

  int gpio_;
  rmt_channel_handle_t channel_{};
  QueueHandle_t queue_{};
  std::array<rmt_symbol_word_t, 64> symbols_{};
  NecDecoder decoder_;
  bool receiving_{};
};

} // namespace thermal_terminal
