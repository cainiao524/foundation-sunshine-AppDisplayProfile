#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include <boost/optional.hpp>

#include "display_device.h"

namespace display_device {

  /**
   * @brief Select a display id using strict-selector and primary-first rules.
   * @param devices Enumerated devices.
   * @param preferred_selector Optional id, OS display name, or friendly name.
   * @param excluded_friendly_name Optional virtual-display friendly name to exclude.
   */
  inline boost::optional<std::string>
  select_current_physical_display_id(
    const device_info_map_t &devices,
    const std::string &preferred_selector,
    std::string_view excluded_friendly_name = {}) {
    const auto is_active_physical = [&](const auto &entry) {
      const auto &info = entry.second;
      const bool active = info.device_state == device_state_e::active ||
                          info.device_state == device_state_e::primary;
      return active && (excluded_friendly_name.empty() || info.friendly_name != excluded_friendly_name);
    };

    auto selected = devices.end();
    if (!preferred_selector.empty()) {
      selected = std::find_if(devices.begin(), devices.end(), [&](const auto &entry) {
        return is_active_physical(entry) &&
               (entry.first == preferred_selector ||
                entry.second.display_name == preferred_selector ||
                entry.second.friendly_name == preferred_selector);
      });
    }
    else {
      selected = std::find_if(devices.begin(), devices.end(), [&](const auto &entry) {
        return is_active_physical(entry) && entry.second.device_state == device_state_e::primary;
      });
      if (selected == devices.end()) {
        selected = std::find_if(devices.begin(), devices.end(), is_active_physical);
      }
    }

    if (selected == devices.end()) {
      return boost::none;
    }
    return selected->first;
  }

}  // namespace display_device
