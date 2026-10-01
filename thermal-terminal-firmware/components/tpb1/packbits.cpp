#include "tpb1/packbits.hpp"

#include <algorithm>

namespace thermal_terminal::tpb1 {

PackBitsError decode_packbits(const std::uint8_t *input, std::size_t input_size,
                              std::size_t expected_output_size,
                              std::vector<std::uint8_t> &output) {
  output.clear();
  output.reserve(expected_output_size);
  std::size_t position = 0;

  while (position < input_size) {
    const std::uint8_t control = input[position++];
    if (control <= 127U) {
      const std::size_t count = static_cast<std::size_t>(control) + 1U;
      if (count > input_size - position)
        return PackBitsError::kTruncatedInput;
      if (count > expected_output_size - output.size()) {
        return PackBitsError::kOutputOverflow;
      }
      output.insert(output.end(), input + position, input + position + count);
      position += count;
      continue;
    }

    if (control == 128U)
      return PackBitsError::kReservedControl;
    if (position >= input_size)
      return PackBitsError::kTruncatedInput;

    const std::size_t count = 257U - static_cast<std::size_t>(control);
    if (count > expected_output_size - output.size()) {
      return PackBitsError::kOutputOverflow;
    }
    output.insert(output.end(), count, input[position++]);
  }

  return output.size() == expected_output_size ? PackBitsError::kNone
                                               : PackBitsError::kLengthMismatch;
}

} // namespace thermal_terminal::tpb1
