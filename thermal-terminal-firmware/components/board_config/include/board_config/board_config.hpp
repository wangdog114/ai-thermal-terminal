#pragma once

#include <cstddef>
#include <cstdint>

#include "interfaces/input.hpp"

namespace thermal_terminal::board_config {

// Edit this file when moving wires or changing the NEC remote. Rebuild and
// flash the firmware afterward; these values are not stored in NVS.
constexpr int kOledSdaGpio = 1;
constexpr int kOledSclGpio = 2;
constexpr std::uint8_t kOledI2cAddress = 0x3D; // 7-bit address; driver also tries 0x3C.

constexpr int kIrReceiverGpio = 15;
constexpr std::uint16_t kRemoteAddress = 0x0000;
constexpr std::uint8_t kEditBackspaceCommand = 0x07; // VOL-
constexpr std::uint8_t kEditSpaceCommand = 0x15;     // VOL+
constexpr std::uint8_t kEditInputModeCommand = 0x19; // 100+
constexpr std::uint8_t kEditSymbolsCommand = 0x0D;   // 200+
// NEC remotes emit repeat frames shortly after a key press. A repeat must
// last this long before PLAY/PAUSE is treated as retry instead of a release.
constexpr std::uint32_t kRetryLongPressMs = 800;

constexpr int kPrinterUart = 1;
constexpr int kPrinterTxGpio = 17; // ESP32 TX -> printer TTL RXD.
constexpr int kPrinterRxGpio = 18; // ESP32 RX <- printer TTL TXD.
constexpr int kPrinterBaudrate = 9600;
constexpr std::uint8_t kPrinterDensity = 8; // Reserved; driver does not send density command yet.
constexpr std::size_t kMaxDraftLength = 2048;
// Keep false until the replacement printer is confirmed to have a TTL UART.
constexpr bool kPrinterInterfaceReady = false;

struct RemoteButtonBinding {
  std::uint8_t command;
  LogicalKey key;
  const char *name;
};

// Physical remote order: left to right, top to bottom. 100+/200+ are
// contextual in compose mode: input-mode switch and symbol panel.
constexpr RemoteButtonBinding kRemoteButtons[] = {
    {0x45, LogicalKey::kUp, "CH-"},
    {0x46, LogicalKey::kConfirm, "CH"},
    {0x47, LogicalKey::kDown, "CH+"},
    {0x44, LogicalKey::kLeft, "PREV"},
    {0x40, LogicalKey::kRight, "NEXT"},
    {0x43, LogicalKey::kSend, "PLAY/PAUSE"},
    {kEditBackspaceCommand, LogicalKey::kUp, "VOL-"},
    {kEditSpaceCommand, LogicalKey::kDown, "VOL+"},
    {0x09, LogicalKey::kMenu, "EQ"},
    {0x16, LogicalKey::kDigit0, "0"},
    {0x19, LogicalKey::kPrint, "100+"},
    {0x0D, LogicalKey::kClear, "200+"},
    {0x0C, LogicalKey::kDigit1, "1"},
    {0x18, LogicalKey::kDigit2, "2"},
    {0x5E, LogicalKey::kDigit3, "3"},
    {0x08, LogicalKey::kDigit4, "4"},
    {0x1C, LogicalKey::kDigit5, "5"},
    {0x5A, LogicalKey::kDigit6, "6"},
    {0x42, LogicalKey::kDigit7, "7"},
    {0x52, LogicalKey::kDigit8, "8"},
    {0x4A, LogicalKey::kDigit9, "9"},
};

constexpr std::size_t kRemoteButtonCount =
    sizeof(kRemoteButtons) / sizeof(kRemoteButtons[0]);

constexpr bool valid_pin(int gpio) { return gpio >= 0 && gpio <= 48; }

constexpr bool unique_signal_pins() {
  constexpr int pins[] = {kOledSdaGpio, kOledSclGpio, kIrReceiverGpio,
                          kPrinterTxGpio, kPrinterRxGpio};
  for (std::size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
    for (std::size_t j = i + 1; j < sizeof(pins) / sizeof(pins[0]); ++j)
      if (pins[i] == pins[j])
        return false;
  return true;
}

constexpr bool unique_remote_commands() {
  for (std::size_t i = 0; i < kRemoteButtonCount; ++i)
    for (std::size_t j = i + 1; j < kRemoteButtonCount; ++j)
      if (kRemoteButtons[i].command == kRemoteButtons[j].command)
        return false;
  return true;
}

static_assert(valid_pin(kOledSdaGpio) && valid_pin(kOledSclGpio) &&
                  valid_pin(kIrReceiverGpio) && valid_pin(kPrinterTxGpio) &&
                  valid_pin(kPrinterRxGpio),
              "Board GPIO must be 0..48");
static_assert(unique_signal_pins(), "Two signals cannot share one GPIO");
static_assert(kOledI2cAddress == 0x3C || kOledI2cAddress == 0x3D,
              "SSD1306 address must be a 7-bit 0x3C or 0x3D");
static_assert(kPrinterUart == 1 || kPrinterUart == 2,
              "Printer UART must be UART1 or UART2");
static_assert(kPrinterBaudrate >= 1200 && kPrinterBaudrate <= 921600,
              "Printer baudrate is outside the supported range");
static_assert(unique_remote_commands(), "Duplicate NEC command in remote map");

} // namespace thermal_terminal::board_config
