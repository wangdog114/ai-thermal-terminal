#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace thermal_terminal::tpb1 {

enum class PackBitsError : std::uint8_t {
  kNone,
  kTruncatedInput,
  kReservedControl,
  kOutputOverflow,
  kLengthMismatch
};

PackBitsError decode_packbits(const std::uint8_t *input, std::size_t input_size,
                              std::size_t expected_output_size,
                              std::vector<std::uint8_t> &output);

} // namespace thermal_terminal::tpb1
