#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config/board_config.hpp"
#include "interfaces/input.hpp"
#include "nec_input/nec_decoder.hpp"

namespace thermal_terminal {

using RemoteKeyBinding = board_config::RemoteButtonBinding;

const RemoteKeyBinding *remote_key_bindings(std::size_t &count);
bool map_nec_key(const NecFrame &frame, KeyEvent &event);
const char *logical_key_name(LogicalKey key);

} // namespace thermal_terminal
