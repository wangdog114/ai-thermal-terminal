#pragma once

#include "settings/user_settings.hpp"

namespace thermal_terminal {

class SettingsStore {
public:
  bool initialize();
  bool load(UserSettings &user, NetworkSettings &network,
            SessionSettings &session);
  bool save_user(const UserSettings &user);
  bool save_network(const NetworkSettings &network);
  bool save_session(const SessionSettings &session);
};

} // namespace thermal_terminal
