#include "nec_input/nec_receiver.hpp"

#include <array>

#include "esp_log.h"
#include "esp_timer.h"

namespace thermal_terminal {
namespace {
constexpr char kTag[] = "nec-rx";
}

NecReceiver::NecReceiver(int gpio) : gpio_(gpio) {}

NecReceiver::~NecReceiver() { release(); }

void NecReceiver::release() {
  if (channel_ != nullptr) {
    rmt_disable(channel_);
    rmt_del_channel(channel_);
    channel_ = nullptr;
  }
  if (queue_ != nullptr) {
    vQueueDelete(queue_);
    queue_ = nullptr;
  }
  receiving_ = false;
}

bool NecReceiver::on_receive(rmt_channel_handle_t,
                             const rmt_rx_done_event_data_t *data,
                             void *context) {
  auto *queue = static_cast<QueueHandle_t>(context);
  BaseType_t wake = pdFALSE;
  xQueueSendFromISR(queue, data, &wake);
  return wake == pdTRUE;
}

bool NecReceiver::initialize() {
  if (channel_ != nullptr)
    return true;
  queue_ = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
  if (queue_ == nullptr)
    return false;

  rmt_rx_channel_config_t config{};
  config.gpio_num = static_cast<gpio_num_t>(gpio_);
  config.clk_src = RMT_CLK_SRC_DEFAULT;
  config.resolution_hz = 1000000;
  config.mem_block_symbols = symbols_.size();
  if (rmt_new_rx_channel(&config, &channel_) != ESP_OK) {
    release();
    return false;
  }

  rmt_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_done = &NecReceiver::on_receive;
  if (rmt_rx_register_event_callbacks(channel_, &callbacks, queue_) != ESP_OK ||
      rmt_enable(channel_) != ESP_OK) {
    release();
    return false;
  }
  ESP_LOGI(kTag, "1838B NEC receiver ready on GPIO%d", gpio_);
  return true;
}

bool NecReceiver::read(NecFrame &frame, std::uint32_t timeout_ms) {
  if (channel_ == nullptr)
    return false;
  if (!receiving_) {
    rmt_receive_config_t config{};
    // NEC 的有效脉冲约为 560 us；1.25 us 足以过滤毛刺，且符合
    // ESP-IDF RMT 在 1 MHz 分辨率下对 signal_range_min_ns 的限制。
    config.signal_range_min_ns = 1250;
    config.signal_range_max_ns = 12000000;
    if (rmt_receive(channel_, symbols_.data(), sizeof(symbols_), &config) !=
        ESP_OK) {
      return false;
    }
    receiving_ = true;
  }

  rmt_rx_done_event_data_t received{};
  if (xQueueReceive(queue_, &received, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
    return false;
  }
  receiving_ = false;
  if (received.num_symbols > symbols_.size())
    return false;

  std::array<NecPulse, 64> pulses{};
  for (std::size_t index = 0; index < received.num_symbols; ++index) {
    const auto &symbol = received.received_symbols[index];
    if (symbol.level0 != 0 || (symbol.duration1 != 0 && symbol.level1 != 1)) {
      return false;
    }
    pulses[index] = {symbol.duration0, symbol.duration1};
  }
  return decoder_.decode(
      pulses.data(), received.num_symbols,
      static_cast<std::uint64_t>(esp_timer_get_time() / 1000), frame);
}

} // namespace thermal_terminal
