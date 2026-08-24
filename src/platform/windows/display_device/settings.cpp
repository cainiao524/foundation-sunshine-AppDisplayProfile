// standard includes
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <thread>

// local includes
#include "settings_topology.h"
#include "src/audio.h"
#include "src/display_device/color_profile.h"
#include "src/display_device/session.h"
#include "src/display_device/to_string.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/config.h"
#include "src/rtsp.h"
#include "color_profile.h"
#include "windows_utils.h"

namespace display_device {

  struct settings_t::persistent_data_t {
    topology_pair_t topology; /**< Contains topology before the modification and the one we modified. */
    device_identity_map_t device_identities; /**< Original route-independent identities for physical displays. */
    std::set<std::string> vdd_device_ids; /**< Device ids confirmed to belong to VDD monitors. */
    std::string original_primary_display; /**< Original primary display in the topology we modified. Empty value if we didn't modify it. */
    device_display_mode_map_t original_modes; /**< Original display modes in the topology we modified. Empty value if we didn't modify it. */
    hdr_state_map_t original_hdr_states; /**< Original display HDR states in the topology we modified. Empty value if we didn't modify it. */
    std::optional<win_color_profile::state_t> color_profile; /**< Temporary physical-display ICC override and its restore snapshot. */

    /**
     * @brief Check if the persistent data contains any meaningful modifications that need to be reverted.
     * @returns True if the data contains something that needs to be reverted, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * settings_t::persistent_data_t data;
     * if (data.contains_modifications()) {
     *   // save persistent data
     * }
     * ```
     */
    [[nodiscard]] bool
    contains_modifications() const {
      return !is_topology_the_same(topology.initial, topology.modified) ||
             !original_primary_display.empty() ||
             !original_modes.empty() ||
             !original_hdr_states.empty() ||
             color_profile.has_value();
    }

    friend void
    to_json(nlohmann::json &json, const persistent_data_t &data) {
      json = {
        { "topology", data.topology },
        { "device_identities", data.device_identities },
        { "vdd_device_ids", data.vdd_device_ids },
        { "original_primary_display", data.original_primary_display },
        { "original_modes", data.original_modes },
        { "original_hdr_states", data.original_hdr_states },
      };
      if (data.color_profile) {
        json["color_profile"] = *data.color_profile;
      }
    }

    friend void
    from_json(const nlohmann::json &json, persistent_data_t &data) {
      json.at("topology").get_to(data.topology);
      data.device_identities = json.value("device_identities", device_identity_map_t {});
      data.vdd_device_ids = json.value("vdd_device_ids", std::set<std::string> {});
      json.at("original_primary_display").get_to(data.original_primary_display);
      json.at("original_modes").get_to(data.original_modes);
      json.at("original_hdr_states").get_to(data.original_hdr_states);
      if (const auto profile = json.find("color_profile"); profile != json.end() && !profile->is_null()) {
        data.color_profile = profile->get<win_color_profile::state_t>();
      }
      else {
        data.color_profile.reset();
      }
    }
  };

  struct settings_t::audio_data_t {
    /**
     * @brief A reference to the audio context that will automatically extend the audio session.
     * @note It is auto-initialized here for convenience.
     */
    decltype(audio::get_audio_ctx_ref()) audio_ctx_ref { audio::get_audio_ctx_ref() };
  };

  namespace {

    /**
     * @brief Get one of the primary display ids found in the topology metadata.
     * @param metadata Topology metadata that also includes current active topology.
     * @return Device id for the primary device, or empty string if primary device not found somehow.
     *
     * EXAMPLES:
     * ```cpp
     * topology_metadata_t metadata;
     * const std::string primary_device_id = get_current_primary_display(metadata);
     * ```
     */
    std::string
    get_current_primary_display(const topology_metadata_t &metadata) {
      for (const auto &group : metadata.current_topology) {
        for (const auto &device_id : group) {
          if (is_primary_device(device_id)) {
            return device_id;
          }
        }
      }

      return std::string {};
    }

    /**
     * @brief Compute the new primary display id based on the information we have.
     * @param original_primary_display Original device id (the one before our first modification or from current topology).
     * @param metadata The current metadata that we are evaluating.
     * @return Primary display id that matches the requirements.
     *
     * EXAMPLES:
     * ```cpp
     * topology_metadata_t metadata;
     * const std::string primary_device_id = determine_new_primary_display("MY_DEVICE_ID", metadata);
     * ```
     */
    std::string
    determine_new_primary_display(const std::string &original_primary_display, const topology_metadata_t &metadata) {
      if (metadata.primary_device_requested) {
        // Primary device was requested - no device was specified by user.
        // This means we are keeping whatever display we have.
        return original_primary_display;
      }

      // For primary devices it is enough to set 1 as a primary display, as the whole duplicated group
      // will become primary displays.
      const auto new_primary_device { metadata.duplicated_devices.front() };
      return new_primary_device;
    }

    bool
    is_physical_primary_candidate(const std::string &device_id, const std::string &vdd_device_id) {
      return !device_id.empty() &&
             device_id != vdd_device_id &&
             get_display_friendly_name(device_id) != ZAKO_NAME;
    }

    std::string
    find_physical_primary_candidate(const active_topology_t &topology, const std::string &vdd_device_id) {
      for (const auto &group : topology) {
        for (const auto &device_id : group) {
          if (is_physical_primary_candidate(device_id, vdd_device_id)) {
            return device_id;
          }
        }
      }

      return std::string {};
    }

    bool
    should_restore_physical_primary_for_vdd_secondary(const parsed_config_t &config) {
      return config.use_vdd &&
             *config.use_vdd &&
             config.vdd_prep == parsed_config_t::vdd_prep_e::vdd_as_secondary;
    }

    /**
     * @brief Change the primary display based on the configuration and previously configured primary display.
     *
     * The function performs the necessary steps for changing the primary display if needed.
     * It also evaluates for possible changes in the configuration and undoes the changes
     * we have made before.
     *
     * @param device_prep Device preparation value from the configuration.
     * @param previous_primary_display Device id of the original primary display we have initially changed (can be empty).
     * @param metadata Additional data with info about the current topology.
     * @return Device id to be used when reverting all settings (can be empty string), or an empty optional if the function fails.
     */
    boost::optional<std::string>
    handle_primary_display_configuration(const parsed_config_t &config, const std::string &previous_primary_display, const topology_metadata_t &metadata, const active_topology_t &initial_topology) {
      if (config.use_vdd &&
          *config.use_vdd &&
          config.vdd_prep == parsed_config_t::vdd_prep_e::vdd_as_secondary) {
        // session_t::apply_vdd_display_stage() already applies the physical-primary
        // topology before settings_t handles modes and HDR. Calling SetDisplayConfig
        // again here races the VDD topology transition and can return ERROR_GEN_FAILURE.
        BOOST_LOG(debug) << "VDD secondary topology already selected physical primary; skipping duplicate primary-display configuration";
        return std::string {};
      }

      if (config.device_prep == parsed_config_t::device_prep_e::ensure_primary) {
        std::string original_primary_display = previous_primary_display;
        if (original_primary_display.empty() && config.use_vdd && *config.use_vdd) {
          // VDD creation can make the new virtual display primary before this
          // function runs. The pre-VDD topology is the reliable source for the
          // physical display that must be restored after the session.
          original_primary_display = find_physical_primary_candidate(initial_topology, config.device_id);
        }
        if (original_primary_display.empty()) {
          original_primary_display = get_current_primary_display(metadata);
        }
        const auto new_primary_display { determine_new_primary_display(original_primary_display, metadata) };

        BOOST_LOG(info) << "Changing primary display to: " << new_primary_display;
        if (!set_as_primary_device(new_primary_display)) {
          // Error already logged
          return boost::none;
        }

        // Here we preserve the data from persistence (unless there's none) as in the end that is what we want to go back to.
        return original_primary_display;
      }

      if (should_restore_physical_primary_for_vdd_secondary(config)) {
        const auto physical_primary_display =
          is_physical_primary_candidate(previous_primary_display, config.device_id) ?
            previous_primary_display :
            find_physical_primary_candidate(initial_topology, config.device_id);

        if (physical_primary_display.empty()) {
          BOOST_LOG(error) << "Failed to find a physical display to use as primary for VDD secondary mode.";
          return boost::none;
        }

        BOOST_LOG(info) << "Changing primary display to physical display for VDD secondary mode: " << physical_primary_display;
        if (!set_as_primary_device(physical_primary_display)) {
          // Error already logged
          return boost::none;
        }

        return std::string {};
      }

      if (!previous_primary_display.empty()) {
        BOOST_LOG(info) << "Changing primary display back to: " << previous_primary_display;
        if (!set_as_primary_device(previous_primary_display)) {
          // Error already logged
          return boost::none;
        }
      }

      return std::string {};
    }

    /**
     * @brief Remove entries from a device_id-keyed map whose keys are not in the valid set.
     */
    template<typename MapT>
    void
    filter_stale_devices(MapT &map, const std::unordered_set<std::string> &valid_ids, const char *label) {
      for (auto it = map.begin(); it != map.end();) {
        if (valid_ids.find(it->first) == valid_ids.end()) {
          BOOST_LOG(warning) << "Removing stale device from " << label << ": " << it->first;
          it = map.erase(it);
        }
        else {
          ++it;
        }
      }
    }

    /**
     * @brief Remove VDD device entries from a device_id-keyed map.
     *
     * VDD device IDs are unstable (change on destroy/recreate), so they should not be
     * persisted. This function removes them before saving to persistent_data.
     */
    template<typename MapT>
    void
    filter_vdd_devices(MapT &map) {
      for (auto it = map.begin(); it != map.end();) {
        if (get_display_friendly_name(it->first) == ZAKO_NAME) {
          BOOST_LOG(debug) << "Excluding VDD device from persistence: " << it->first;
          it = map.erase(it);
        }
        else {
          ++it;
        }
      }
    }

    device_identity_map_t
    collect_device_identities(const device_info_map_t &devices) {
      device_identity_map_t identities;
      for (const auto &[device_id, info] : devices) {
        identities.emplace(device_id, info.physical_identity);
      }
      return identities;
    }

    std::unordered_set<std::string>
    collect_persisted_device_ids(
      const settings_t::persistent_data_t &data,
      const std::unordered_set<std::string> &vdd_device_ids) {
      auto device_ids { get_device_ids_from_topology(data.topology.initial) };
      const auto modified_ids { get_device_ids_from_topology(data.topology.modified) };
      device_ids.insert(modified_ids.begin(), modified_ids.end());

      // Topology is authoritative for physical display cardinality. Mode/HDR maps may contain
      // a second route alias after a failed dual-GPU restore; treating that alias as another
      // physical display prevents the real topology id from being remapped. Legacy data with no
      // topology still falls back to the keyed snapshots so it remains recoverable.
      if (device_ids.empty()) {
        for (const auto &[device_id, _] : data.original_modes) {
          device_ids.insert(device_id);
        }
        for (const auto &[device_id, _] : data.original_hdr_states) {
          device_ids.insert(device_id);
        }
        if (!data.original_primary_display.empty()) {
          device_ids.insert(data.original_primary_display);
        }
        if (data.color_profile && !data.color_profile->device_id.empty()) {
          device_ids.insert(data.color_profile->device_id);
        }
      }

      for (const auto &vdd_id : vdd_device_ids) {
        device_ids.erase(vdd_id);
      }
      return device_ids;
    }

    void
    apply_device_id_replacements(
      settings_t::persistent_data_t &data,
      const std::map<std::string, std::string> &replacements) {
      if (replacements.empty()) {
        return;
      }

      remap_topology_device_ids(data.topology.initial, replacements);
      remap_topology_device_ids(data.topology.modified, replacements);

      const auto remap_keyed_values = [&replacements](auto &values) {
        for (const auto &[old_id, new_id] : replacements) {
          if (auto old_value = values.find(old_id); old_value != values.end()) {
            const auto value = old_value->second;
            values.erase(old_value);
            values[new_id] = value;
          }
        }
      };

      remap_keyed_values(data.original_modes);
      remap_keyed_values(data.original_hdr_states);
      remap_keyed_values(data.device_identities);

      if (const auto replacement = replacements.find(data.original_primary_display); replacement != replacements.end()) {
        data.original_primary_display = replacement->second;
      }
      if (data.color_profile) {
        if (const auto replacement = replacements.find(data.color_profile->device_id); replacement != replacements.end()) {
          data.color_profile->device_id = replacement->second;
        }
      }
    }

    bool
    is_valid_refresh_rate(const refresh_rate_t &refresh_rate) {
      return refresh_rate.numerator > 0 && refresh_rate.denominator > 0;
    }

    boost::optional<refresh_rate_t>
    find_fallback_refresh_rate(
      const device_display_mode_map_t &display_modes,
      const active_topology_t &preferred_topology) {
      const auto find_for_device = [&display_modes](const std::string &device_id) -> boost::optional<refresh_rate_t> {
        const auto mode_it = display_modes.find(device_id);
        if (mode_it != display_modes.end() && is_valid_refresh_rate(mode_it->second.refresh_rate)) {
          return mode_it->second.refresh_rate;
        }
        return boost::none;
      };

      // The first device in the first topology group is the original primary
      // display on Windows. Prefer its saved refresh rate for a new VDD.
      for (const auto &group : preferred_topology) {
        for (const auto &device_id : group) {
          if (const auto refresh_rate = find_for_device(device_id)) {
            return refresh_rate;
          }
        }
      }

      // Fall back to any valid saved physical-display mode if the topology
      // changed while the session was being prepared.
      for (const auto &[_, mode] : display_modes) {
        if (is_valid_refresh_rate(mode.refresh_rate)) {
          return mode.refresh_rate;
        }
      }

      return boost::none;
    }

    /**
     * @brief Compute the new display modes based on the information we have.
     * @param resolution Resolution value from the configuration.
     * @param refresh_rate Refresh rate value from the configuration.
     * @param original_display_modes Original display modes (the ones before our first modification or from current topology)
     *                               that we use as a base we will apply changes to.
     * @param metadata The current metadata that we are evaluating.
     * @param preferred_topology The topology used to select a refresh rate fallback.
     * @return New display modes for the topology.
     */
    device_display_mode_map_t
    determine_new_display_modes(
      const boost::optional<resolution_t> &resolution,
      const boost::optional<refresh_rate_t> &refresh_rate,
      const device_display_mode_map_t &original_display_modes,
      const topology_metadata_t &metadata,
      const active_topology_t &preferred_topology) {
      device_display_mode_map_t new_modes { original_display_modes };
      const auto fallback_refresh_rate = resolution && !refresh_rate
        ? find_fallback_refresh_rate(original_display_modes, preferred_topology)
        : boost::none;

      if (resolution) {
        // For duplicate devices the resolution must match no matter what, otherwise
        // they cannot be duplicated, which breaks Windows' rules.
        for (const auto &device_id : metadata.duplicated_devices) {
          auto &mode = new_modes[device_id];
          mode.resolution = *resolution;

          // A newly created VDD can report a 0/0 target refresh rate while
          // Windows is still publishing its mode list. Never pass that value
          // to SetDisplayConfig when only the resolution was requested.
          if (!refresh_rate && !is_valid_refresh_rate(mode.refresh_rate)) {
            if (fallback_refresh_rate) {
              mode.refresh_rate = *fallback_refresh_rate;
              BOOST_LOG(info) << "Display refresh rate unchanged by configuration; using preserved refresh rate "
                              << to_string(*fallback_refresh_rate) << " for the new display mode.";
            }
            else {
              mode.refresh_rate = { 60, 1 };
              BOOST_LOG(warning) << "No valid preserved refresh rate was available for the new display mode; using 60Hz fallback.";
            }
          }
        }
      }

      if (refresh_rate) {
        if (metadata.primary_device_requested) {
          // No device has been specified, so if they're all are primary devices
          // we need to apply the refresh rate change to all duplicates
          for (const auto &device_id : metadata.duplicated_devices) {
            new_modes[device_id].refresh_rate = *refresh_rate;
          }
        }
        else {
          // Even if we have duplicate devices, their refresh rate may differ
          // and since the device was specified, let's apply the refresh
          // rate only to the specified device.
          new_modes[metadata.duplicated_devices.front()].refresh_rate = *refresh_rate;
        }
      }

      return new_modes;
    }

    /**
     * @brief Modify the display modes based on the configuration and previously configured display modes.
     *
     * The function performs the necessary steps for changing the display modes if needed.
     * It also evaluates for possible changes in the configuration and undoes the changes
     * we have made before.
     *
     * @param resolution Resolution value from the configuration.
     * @param refresh_rate Refresh rate value from the configuration.
     * @param previous_display_modes Original display modes that we have initially changed (can be empty).
     * @param metadata Additional data with info about the current topology.
     * @param preferred_topology The topology used to select a refresh rate fallback.
     * @return Display modes to be used when reverting all settings (can be empty map), or an empty optional if the function fails.
     */
    boost::optional<device_display_mode_map_t>
    handle_display_mode_configuration(
      const boost::optional<resolution_t> &resolution,
      const boost::optional<refresh_rate_t> &refresh_rate,
      const device_display_mode_map_t &previous_display_modes,
      const topology_metadata_t &metadata,
      const active_topology_t &preferred_topology) {
      // Build a set of device IDs present in current topology to filter out stale entries
      // (e.g. old VDD device IDs lingering in persistent_data after a client switch).
      const auto valid_device_ids { get_device_ids_from_topology(metadata.current_topology) };
      const std::unordered_set<std::string> valid_ids_set(valid_device_ids.begin(), valid_device_ids.end());

      if (resolution || refresh_rate) {
        const auto original_display_modes { previous_display_modes.empty() ? get_current_display_modes(valid_device_ids) : previous_display_modes };
        auto new_display_modes { determine_new_display_modes(
          resolution,
          refresh_rate,
          original_display_modes,
          metadata,
          preferred_topology) };

        filter_stale_devices(new_display_modes, valid_ids_set, "display modes");

        BOOST_LOG(info) << "Changing display modes to: " << to_string(new_display_modes);
        if (!set_display_modes(new_display_modes)) {
          // Error already logged
          return boost::none;
        }

        // Here we preserve the data from persistence (unless there's none) as in the end that is what we want to go back to.
        return original_display_modes;
      }

      if (!previous_display_modes.empty()) {
        device_display_mode_map_t filtered_modes { previous_display_modes };
        filter_stale_devices(filtered_modes, valid_ids_set, "rollback display modes");

        if (!filtered_modes.empty()) {
          BOOST_LOG(info) << "Changing display modes back to: " << to_string(filtered_modes);
          if (!set_display_modes(filtered_modes)) {
            // Error already logged
            return boost::none;
          }
        }
      }

      return device_display_mode_map_t {};
    }

    /**
     * @brief Reverse ("blank") HDR states for newly enabled devices.
     *
     * Some newly enabled displays do not handle HDR state correctly (IDD HDR display for example).
     * The colors can become very blown out/high contrast. A simple workaround is to toggle the HDR state
     * once the display has "settled down" or something.
     *
     * This is what this function does, it changes the HDR state to the opposite states that we will have in the
     * end, sleeps for a little and then allows us to continue changing HDR states to the final ones.
     *
     * "blank" comes as an inspiration from "vblank" as this function is meant to be used before changing the HDR
     * states to clean up something.
     *
     * @param states Final states for the devices that we want to blank.
     * @param newly_enabled_devices Devices to perform blanking for.
     * @return False if the function has failed to set HDR states, true otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * hdr_state_map_t new_states;
     * const bool success = blank_hdr_states(new_states, { "DEVICE_ID" });
     * ```
     */
    bool
    blank_hdr_states(const hdr_state_map_t &states, const std::unordered_set<std::string> &newly_enabled_devices) {
      const std::chrono::milliseconds delay { 2333 };
      if (delay > std::chrono::milliseconds::zero()) {
        bool state_changed { false };
        auto toggled_states { states };
        for (const auto &device_id : newly_enabled_devices) {
          auto state_it { toggled_states.find(device_id) };
          if (state_it == std::end(toggled_states)) {
            continue;
          }

          if (state_it->second == hdr_state_e::enabled) {
            state_it->second = hdr_state_e::disabled;
            state_changed = true;
          }
          else if (state_it->second == hdr_state_e::disabled) {
            state_it->second = hdr_state_e::enabled;
            state_changed = true;
          }
        }

        if (state_changed) {
          BOOST_LOG(debug) << "Toggling HDR states for newly enabled devices and waiting for " << delay.count() << "ms before actually applying the correct states.";
          if (!set_hdr_states(toggled_states)) {
            // Error already logged
            return false;
          }

          std::this_thread::sleep_for(delay);
        }
      }

      return true;
    }

    /**
     * @brief Wait for display operations to stabilize before HDR changes.
     *
     * This function ensures that other display operations (topology changes,
     * mode changes, primary display changes) have stabilized before applying
     * HDR state changes. This prevents conflicts and ensures proper HDR handling.
     *
     * @param metadata Topology metadata containing information about current state.
     * @return True if operations have stabilized, false if timeout occurred.
     *
     * EXAMPLES:
     * ```cpp
     * topology_metadata_t metadata;
     * if (wait_for_display_stability(metadata)) {
     *   // Safe to apply HDR changes
     * }
     * ```
     */
    bool
    wait_for_display_stability(const topology_metadata_t &metadata) {
      constexpr int max_attempts = 10;
      constexpr auto stability_check_interval = std::chrono::milliseconds(500);
      constexpr auto max_wait_time = std::chrono::milliseconds(5000);

      BOOST_LOG(debug) << "等待显示器操作稳定，准备进行HDR切换...";

      auto start_time = std::chrono::steady_clock::now();

      for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 检查是否超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed > max_wait_time) {
          BOOST_LOG(warning) << "等待显示器稳定超时，继续执行HDR切换";
          return false;
        }

        // 检查当前拓扑是否稳定
        auto current_topology = get_current_topology();
        if (is_topology_the_same(current_topology, metadata.current_topology)) {
          // 检查显示模式是否稳定
          auto current_modes = get_current_display_modes(get_device_ids_from_topology(current_topology));
          bool modes_stable = true;

          for (const auto &device_id : metadata.duplicated_devices) {
            auto current_mode_it = current_modes.find(device_id);
            if (current_mode_it == current_modes.end()) {
              modes_stable = false;
              break;
            }
          }

          if (modes_stable) {
            BOOST_LOG(debug) << "显示器操作已稳定，可以进行HDR切换";
            return true;
          }
        }

        std::this_thread::sleep_for(stability_check_interval);
      }

      BOOST_LOG(warning) << "显示器稳定检查达到最大尝试次数，继续执行HDR切换";
      return false;
    }

    /**
     * @brief Compute the new HDR states based on the information we have.
     * @param change_hdr_state HDR state value from the configuration.
     * @param original_hdr_states Original HDR states (the ones before our first modification or from current topology)
     *                            that we use as a base we will apply changes to.
     * @param metadata The current metadata that we are evaluating.
     * @return New HDR states for the topology.
     */
    hdr_state_map_t
    determine_new_hdr_states(const boost::optional<bool> &change_hdr_state, const hdr_state_map_t &original_hdr_states, const topology_metadata_t &metadata) {
      hdr_state_map_t new_states { original_hdr_states };

      if (change_hdr_state) {
        const hdr_state_e final_state { *change_hdr_state ? hdr_state_e::enabled : hdr_state_e::disabled };
        const auto try_update_new_state = [&new_states, final_state](const std::string &device_id) {
          const auto current_state { new_states[device_id] };
          if (current_state == hdr_state_e::unknown) {
            return;
          }

          new_states[device_id] = final_state;
        };

        if (metadata.primary_device_requested) {
          // No device has been specified, so if they're all are primary devices
          // we need to apply the HDR state change to all duplicates
          for (const auto &device_id : metadata.duplicated_devices) {
            try_update_new_state(device_id);
          }
        }
        else {
          // Even if we have duplicate devices, their HDR states may differ
          // and since the device was specified, let's apply the HDR state
          // only to the specified device.
          try_update_new_state(metadata.duplicated_devices.front());
        }
      }

      return new_states;
    }

    /**
     * @brief Modify the display HDR states based on the configuration and previously configured display HDR states.
     *
     * The function performs the necessary steps for changing the display HDR states if needed.
     * It also evaluates for possible changes in the configuration and undoes the changes
     * we have made before.
     *
     * @param change_hdr_state HDR state value from the configuration.
     * @param previous_hdr_states Original display HDR states have initially changed (can be empty).
     * @param metadata Additional data with info about the current topology.
     * @return Display HDR states to be used when reverting all settings (can be empty map), or an empty optional if the function fails.
     */
    boost::optional<hdr_state_map_t>
    handle_hdr_state_configuration(const boost::optional<bool> &change_hdr_state, const hdr_state_map_t &previous_hdr_states, const topology_metadata_t &metadata) {
      // Build valid device ID set from current topology to filter stale entries
      const auto valid_device_ids { get_device_ids_from_topology(metadata.current_topology) };
      const std::unordered_set<std::string> valid_ids_set(valid_device_ids.begin(), valid_device_ids.end());

      if (change_hdr_state) {
        const auto current_hdr_states { get_current_hdr_states(valid_device_ids) };
        if (current_hdr_states.empty()) {
          return boost::none;
        }

        auto original_hdr_states { previous_hdr_states };
        for (const auto &[device_id, state] : current_hdr_states) {
          original_hdr_states.try_emplace(device_id, state);
        }

        auto new_hdr_states { determine_new_hdr_states(change_hdr_state, original_hdr_states, metadata) };
        filter_stale_devices(new_hdr_states, valid_ids_set, "HDR states");

        BOOST_LOG(info) << "Changing hdr states to: " << to_string(new_hdr_states);
        if (!blank_hdr_states(new_hdr_states, metadata.newly_enabled_devices) || !set_hdr_states(new_hdr_states)) {
          // Error already logged
          return boost::none;
        }

        // Here we preserve the data from persistence (unless there's none) as in the end that is what we want to go back to.
        return original_hdr_states;
      }

      if (!previous_hdr_states.empty()) {
        hdr_state_map_t filtered_hdr { previous_hdr_states };
        filter_stale_devices(filtered_hdr, valid_ids_set, "rollback HDR states");

        if (!filtered_hdr.empty()) {
          BOOST_LOG(info) << "Changing hdr states back to: " << to_string(filtered_hdr);
          if (!blank_hdr_states(filtered_hdr, metadata.newly_enabled_devices) || !set_hdr_states(filtered_hdr)) {
            // Error already logged
            return boost::none;
          }
        }
      }

      return hdr_state_map_t {};
    }

    /**
     * @brief Revert settings to the ones found in the persistent data.
     * @param data Reference to persistent data containing original settings.
     * @param data_modified Reference to a boolean that is set to true if changes are made to the persistent data reference.
     * @return True if all settings within persistent data have been reverted, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * bool data_modified { false };
     * settings_t::persistent_data_t data;
     *
     * if (!try_revert_settings(data, data_modified)) {
     *   if (data_modified) {
     *     // Update the persistent file
     *   }
     * }
     * ```
     */
    bool
    try_revert_settings(settings_t::persistent_data_t &data, bool &data_modified, bool skip_vdd_destroy = false) {
      try {
        nlohmann::json json_data = data;
        BOOST_LOG(debug) << "Reverting persistent display settings from:\n"
                         << json_data.dump(4);
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to dump persistent display settings: " << err.what();
      }

      if (!data.contains_modifications()) {
        return true;
      }

      const auto available_devices = enum_available_devices_checked();
      if (!available_devices) {
        BOOST_LOG(warning) << "Display enumeration failed while preparing persistent restoration; keeping restore data for retry";
        return false;
      }

      std::unordered_set<std::string> vdd_device_ids {
        data.vdd_device_ids.begin(),
        data.vdd_device_ids.end()
      };
      std::unordered_set<std::string> vdd_in_initial;

      for (const auto &[device_id, info] : *available_devices) {
        if (info.friendly_name == ZAKO_NAME) {
          vdd_device_ids.insert(device_id);
        }
      }

      for (const auto &group : data.topology.initial) {
        for (const auto &device_id : group) {
          if (vdd_device_ids.contains(device_id)) {
            vdd_in_initial.insert(device_id);
          }
        }
      }

      bool should_destroy_vdd = false;
      if (!config::video.vdd_keep_enabled) {
        for (const auto &vdd_id : vdd_device_ids) {
          if (vdd_in_initial.count(vdd_id) == 0) {
            should_destroy_vdd = true;
            break;
          }
        }
      }

      const auto vdd_ids_from_initial = remove_vdd_from_topology(data.topology.initial, vdd_device_ids);
      const auto vdd_ids_from_modified = remove_vdd_from_topology(data.topology.modified, vdd_device_ids);
      std::unordered_set<std::string> all_removed_vdd_ids = vdd_ids_from_initial;
      all_removed_vdd_ids.insert(vdd_ids_from_modified.begin(), vdd_ids_from_modified.end());

      if (!all_removed_vdd_ids.empty()) {
        BOOST_LOG(info) << "Removed confirmed VDD devices from persistent topology";
        data_modified = true;
      }

      for (const auto &vdd_id : vdd_device_ids) {
        data_modified = data.original_hdr_states.erase(vdd_id) > 0 || data_modified;
        data_modified = data.original_modes.erase(vdd_id) > 0 || data_modified;
        data_modified = data.device_identities.erase(vdd_id) > 0 || data_modified;
        if (data.original_primary_display == vdd_id) {
          data.original_primary_display.clear();
          data_modified = true;
        }
        if (data.color_profile && data.color_profile->device_id == vdd_id) {
          data.color_profile.reset();
          data_modified = true;
        }
      }

      const auto expected_device_ids = collect_persisted_device_ids(data, vdd_device_ids);
      const auto current_identities = collect_device_identities(*available_devices);
      auto saved_identities = data.device_identities;
      auto remap_result = resolve_device_id_remaps(
        expected_device_ids,
        saved_identities,
        current_identities);

      if (!remap_result.unresolved_device_ids.empty()) {
        const auto historical_identities = w_utils::get_historical_physical_device_identities();
        bool migrated_identity = false;
        for (const auto &device_id : remap_result.unresolved_device_ids) {
          if (const auto historical = historical_identities.find(device_id);
              historical != historical_identities.end() && !historical->second.empty()) {
            BOOST_LOG(info) << "Migrating persisted physical display identity from historical PnP data: " << device_id;
            saved_identities[device_id] = historical->second;
            data.device_identities[device_id] = historical->second;
            migrated_identity = true;
            data_modified = true;
          }
        }

        if (migrated_identity) {
          remap_result = resolve_device_id_remaps(
            expected_device_ids,
            saved_identities,
            current_identities);
        }
      }

      if (!remap_result.unresolved_device_ids.empty()) {
        BOOST_LOG(warning) << "Cannot uniquely resolve persisted physical display ids after a GPU path change; keeping restore data for retry";
        for (const auto &device_id : remap_result.unresolved_device_ids) {
          BOOST_LOG(warning) << "Unresolved persisted physical display id: " << device_id;
        }
        return false;
      }

      for (const auto &[old_id, new_id] : remap_result.replacements) {
        BOOST_LOG(info) << "Remapping physical display id after GPU path change: " << old_id << " -> " << new_id;
      }
      if (!remap_result.replacements.empty()) {
        apply_device_id_replacements(data, remap_result.replacements);
        data_modified = true;
      }

      bool partially_failed = false;
      if (data.color_profile) {
        if (data.color_profile->version != win_color_profile::state_t::current_version) {
          BOOST_LOG(warning) << "Discarding an incompatible persisted color-profile snapshot";
          data.color_profile.reset();
          data_modified = true;
        }
        else if (win_color_profile::restore(*data.color_profile)) {
          data.color_profile.reset();
          data_modified = true;
        }
        else {
          partially_failed = true;
        }
      }

      const bool modified_topology_valid = is_topology_valid(data.topology.modified);
      const bool initial_topology_valid = is_topology_valid(data.topology.initial);
      const bool have_changes_for_modified_topology = !data.original_primary_display.empty() ||
                                                      !data.original_modes.empty() ||
                                                      !data.original_hdr_states.empty();

      std::unordered_set<std::string> newly_enabled_devices;
      auto current_topology = get_current_topology();

      // 保存用于"初始拓扑恢复后最终模式校验"的原始模式快照。
      // 早期的模式恢复成功后会清空 data.original_modes，因此必须先保存副本。
      const auto original_modes_snapshot = data.original_modes;

      // Handle modified topology changes
      if (have_changes_for_modified_topology) {
        if (modified_topology_valid && set_topology(data.topology.modified)) {
          newly_enabled_devices = get_newly_enabled_devices_from_topology(current_topology, data.topology.modified);
          current_topology = data.topology.modified;

          // Revert HDR states
          if (!data.original_hdr_states.empty()) {
            BOOST_LOG(info) << "Changing back the HDR states to: " << to_string(data.original_hdr_states);
            if (set_hdr_states(data.original_hdr_states)) {
              data.original_hdr_states.clear();
              data_modified = true;
            }
            else {
              partially_failed = true;
            }
          }

          // Revert display modes
          if (!data.original_modes.empty()) {
            BOOST_LOG(info) << "Changing back the display modes to: " << to_string(data.original_modes);
            if (set_display_modes(data.original_modes)) {
              data.original_modes.clear();
              data_modified = true;
            }
            else {
              partially_failed = true;
            }
          }

          // Revert primary display
          if (!data.original_primary_display.empty()) {
            BOOST_LOG(info) << "Changing back the primary device to: " << data.original_primary_display;
            if (set_as_primary_device(data.original_primary_display)) {
              data.original_primary_display.clear();
              data_modified = true;
            }
            else {
              partially_failed = true;
            }
          }
        }
        else if (!modified_topology_valid) {
          BOOST_LOG(warning) << "Modified topology invalid; keeping dependent restore data for retry";
          partially_failed = true;
        }
        else {
          BOOST_LOG(error) << "Cannot switch to the topology to undo changes!";
          partially_failed = true;
        }
      }

      // Revert to initial topology
      BOOST_LOG(info) << "Changing display topology back to: " << to_string(data.topology.initial);
      if (initial_topology_valid) {
        if (set_topology(data.topology.initial)) {
          newly_enabled_devices.merge(get_newly_enabled_devices_from_topology(current_topology, data.topology.initial));
          current_topology = data.topology.initial;
          data_modified = true;
        }
        else {
          BOOST_LOG(error) << "Failed to switch back to the initial topology!";
          partially_failed = true;
        }
      }
      else {
        BOOST_LOG(warning) << "Initial topology invalid; keeping restore data for retry";
        partially_failed = true;
      }

      // 最终模式校验与修复：双显卡笔记本的面板通常存在两条 GPU 路径（核显/独显），
      // 会话期间可能发生路径切换或短暂丢失（"GPU path change"）。恢复初始拓扑后
      // Windows 未必把活动主屏恢复到串流前的模式（例如停留在 VDD 的客户端分辨率），
      // 这里做最终校验，不一致时按保存的原始模式重新应用并重试。
      if (!original_modes_snapshot.empty()) {
        const auto refresh_rates_approximately_equal = [](const refresh_rate_t &a, const refresh_rate_t &b) {
          if (a.denominator == 0 || b.denominator == 0) {
            return a.numerator == b.numerator && a.denominator == b.denominator;
          }
          const double hz_a = static_cast<double>(a.numerator) / static_cast<double>(a.denominator);
          const double hz_b = static_cast<double>(b.numerator) / static_cast<double>(b.denominator);
          return std::abs(hz_a - hz_b) < 0.5;
        };

        constexpr int max_mode_verify_attempts = 3;
        bool modes_verified = false;
        for (int attempt = 0; attempt < max_mode_verify_attempts && !modes_verified; ++attempt) {
          const auto available_devices = enum_available_devices();

          // 期望的原始模式按"当前活动设备"重新定位：设备 ID 直接命中优先，
          // 否则按物理身份匹配当前活动设备（面板可能在恢复期间再次切换 GPU 路径）。
          device_display_mode_map_t expected_modes_to_check;
          bool all_expected_resolved = true;
          for (const auto &[expected_id, expected_mode] : original_modes_snapshot) {
            const auto direct_it = available_devices.find(expected_id);
            if (direct_it != available_devices.end() &&
                (direct_it->second.device_state == device_state_e::active ||
                 direct_it->second.device_state == device_state_e::primary)) {
              expected_modes_to_check[expected_id] = expected_mode;
              continue;
            }

            const auto identity_it = data.device_identities.find(expected_id);
            const auto expected_identity = identity_it == data.device_identities.end() ? std::string {} : identity_it->second;
            bool resolved = false;
            if (!expected_identity.empty()) {
              for (const auto &[candidate_id, candidate_info] : available_devices) {
                if ((candidate_info.device_state == device_state_e::active ||
                     candidate_info.device_state == device_state_e::primary) &&
                    candidate_info.physical_identity == expected_identity) {
                  BOOST_LOG(info) << "Resolving restored display mode through physical identity after GPU path change: "
                                  << expected_id << " -> " << candidate_id;
                  expected_modes_to_check[candidate_id] = expected_mode;
                  resolved = true;
                  break;
                }
              }
            }
            if (!resolved) {
              all_expected_resolved = false;
            }
          }

          if (all_expected_resolved) {
            std::unordered_set<std::string> check_ids;
            for (const auto &[device_id, _mode] : expected_modes_to_check) {
              check_ids.insert(device_id);
            }
            const auto current_modes = get_current_display_modes(check_ids);
            modes_verified = current_modes.size() == expected_modes_to_check.size();
            if (modes_verified) {
              for (const auto &[device_id, expected_mode] : expected_modes_to_check) {
                const auto mode_it = current_modes.find(device_id);
                if (mode_it == current_modes.end() ||
                    mode_it->second.resolution.width != expected_mode.resolution.width ||
                    mode_it->second.resolution.height != expected_mode.resolution.height ||
                    !refresh_rates_approximately_equal(mode_it->second.refresh_rate, expected_mode.refresh_rate)) {
                  modes_verified = false;
                  break;
                }
              }
            }
          }

          if (modes_verified) {
            break;
          }

          BOOST_LOG(warning) << "Restored display mode verification failed (attempt " << (attempt + 1)
                             << "/" << max_mode_verify_attempts << "); re-applying the original display modes";
          if (!set_display_modes(expected_modes_to_check.empty() ? original_modes_snapshot : expected_modes_to_check)) {
            BOOST_LOG(error) << "Failed to re-apply the original display modes during verification";
          }
          if (attempt + 1 < max_mode_verify_attempts) {
            std::this_thread::sleep_for(300ms);
          }
        }

        if (!modes_verified) {
          BOOST_LOG(error) << "Restored display mode verification failed after all attempts; the primary display may keep an unexpected resolution";
        }
      }

      // Fix HDR states for newly enabled devices
      if (!newly_enabled_devices.empty()) {
        const auto current_hdr_states = get_current_hdr_states(get_device_ids_from_topology(current_topology));
        BOOST_LOG(debug) << "Trying to fix HDR states (if needed).";
        blank_hdr_states(current_hdr_states, newly_enabled_devices);
        set_hdr_states(current_hdr_states);
      }

      if (!partially_failed) {
        if (skip_vdd_destroy) {
          BOOST_LOG(debug) << "VDD lifecycle is managed by the caller; skipping internal destruction";
        }
        else if (config::video.vdd_keep_enabled) {
          BOOST_LOG(debug) << "VDD keep-enabled mode is active; preserving VDD";
        }
        else if (should_destroy_vdd) {
          BOOST_LOG(info) << "Display restoration completed; destroying Sunshine-created VDD";
          display_device::session_t::get().destroy_vdd_monitor();
        }
      }

      return !partially_failed;
    }

    /**
     * @brief Save settings to the JSON file.
     * @param filepath Filepath for the persistent data.
     * @param data Persistent data to save.
     * @return True if the filepath is empty or the data was saved to the file, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * settings_t::persistent_data_t data;
     *
     * if (save_settings("/foo/bar.json", data)) {
     *   // Do stuff...
     * }
     * ```
     */
    bool
    save_settings(const std::filesystem::path &filepath, const settings_t::persistent_data_t &data) {
      if (filepath.empty()) {
        BOOST_LOG(warning) << "No filename was specified for persistent display device configuration.";
        return true;
      }

      try {
        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
          BOOST_LOG(error) << "Failed to open persistent display settings for writing: " << filepath;
          return false;
        }
        nlohmann::json json_data = data;

        // Write json with indentation
        file << std::setw(4) << json_data << std::endl;
        if (!file) {
          BOOST_LOG(error) << "Failed to write persistent display settings: " << filepath;
          return false;
        }
        BOOST_LOG(debug) << "Saved persistent display settings:\n"
                         << json_data.dump(4);
        return true;
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to save display settings: " << err.what();
      }

      return false;
    }

    /**
     * @brief Load persistent data from the JSON file.
     * @param filepath Filepath to load data from.
     * @return Unique pointer to the persistent data if it was loaded successfully, nullptr otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * auto data = load_settings("/foo/bar.json");
     * ```
     */
    std::unique_ptr<settings_t::persistent_data_t>
    load_settings(const std::filesystem::path &filepath) {
      try {
        if (!filepath.empty() && std::filesystem::exists(filepath)) {
          std::ifstream file(filepath);
          return std::make_unique<settings_t::persistent_data_t>(nlohmann::json::parse(file));
        }
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to load saved display settings: " << err.what();
      }

      return nullptr;
    }

    /**
     * @brief Remove the file.
     * @param filepath Filepath to remove.
     *
     * EXAMPLES:
     * ```cpp
     * remove_file("/foo/bar.json");
     * ```
     */
    void
    remove_file(const std::filesystem::path &filepath) {
      try {
        if (!filepath.empty()) {
          std::filesystem::remove(filepath);
        }
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "Failed to remove " << filepath << ". Error: " << err.what();
      }
    }

  }  // namespace

  settings_t::settings_t() = default;

  settings_t::~settings_t() = default;

  void
  settings_t::capture_audio_sink() {
    if (audio_data) {
      return;
    }

    BOOST_LOG(debug) << "Capturing audio sink before changing display";
    audio_data = std::make_unique<audio_data_t>();
  }

  void
  settings_t::release_audio_sink() {
    if (!audio_data) {
      return;
    }

    BOOST_LOG(debug) << "Releasing captured audio sink";
    audio_data = nullptr;
  }

  bool
  settings_t::is_changing_settings_going_to_fail() const {
    const bool session_locked = w_utils::is_user_session_locked();
    
    // 如果会话已锁定，直接返回true，跳过CCD API测试
    // 这避免了在锁屏状态下频繁调用显示API导致ERROR_ACCESS_DENIED和WATCHDOG事件
    if (session_locked) {
      BOOST_LOG(info) << "Changing settings will fail - session_locked: true";
      return true;
    }
    
    const bool no_ccd_access = w_utils::test_no_access_to_ccd_api();
    if (no_ccd_access) {
      BOOST_LOG(info) << "Changing settings will fail - no_ccd_access: true";
    }
    
    return no_ccd_access;
  }

  settings_t::apply_result_t
  settings_t::apply_config(
    const parsed_config_t &config,
    const rtsp_stream::launch_session_t &session,
    const boost::optional<active_topology_t> &pre_saved_initial_topology,
    const boost::optional<device_display_mode_map_t> &pre_saved_initial_modes,
    const boost::optional<device_info_map_t> &pre_saved_devices) {
    auto profile_setting = color_profile::resolve_client_hdr_profile(
      config::get_clients_config(), session.client_cert_uuid, session.client_name);
    if (!profile_setting) {
      BOOST_LOG(warning) << "Ignoring invalid client color-profile setting: " << profile_setting.error;
      profile_setting = {};
    }
    else if (profile_setting.used_legacy_name) {
      BOOST_LOG(warning) << "Using legacy client-name matching for an Advanced Color profile; pair the client again to bind by UUID";
    }

    const bool client_hdr_enabled = session.enable_hdr;
    const auto do_apply_config { [this, &pre_saved_initial_topology, &pre_saved_initial_modes, &pre_saved_devices, &profile_setting, client_hdr_enabled](const parsed_config_t &config) -> settings_t::apply_result_t {
      // 检测是否为VDD模式
      const bool is_vdd_mode = config.use_vdd && *config.use_vdd;

      // 根据模式选择不同的拓扑处理方式
      boost::optional<handled_topology_result_t> topology_result;
      bool failed_while_reverting_settings { false };

      if (is_vdd_mode) {
        // VDD模式：拓扑由 session_t 的 VDD 显示阶段控制，这里只获取 metadata
        // 这里不修改拓扑，分辨率、刷新率、HDR 等设置仍然会应用
        BOOST_LOG(info) << "VDD mode: topology controlled by the session VDD display stage, only getting current topology metadata";
        topology_result = get_current_topology_metadata(config.device_id);

        // 如果有预保存的初始拓扑（在 VDD 创建前保存的物理显示器拓扑），
        // 用它替换 get_current_topology_metadata 返回的初始拓扑。
        // 否则恢复时 remove_vdd_from_topology 会将初始拓扑清空，
        // 导致物理显示器无法被重新启用。
        if (topology_result && pre_saved_initial_topology && !pre_saved_initial_topology->empty()) {
          BOOST_LOG(info) << "VDD mode: using pre-saved initial topology (physical displays) instead of current VDD-only topology";
          topology_result->pair.initial = *pre_saved_initial_topology;
        }
      }
      else {
        // 普通模式：device_prep 控制拓扑
        if (config.device_prep == parsed_config_t::device_prep_e::no_operation) {
          BOOST_LOG(info) << "Display device preparation mode is set to no_operation, topology will not be changed";
        }

        const boost::optional<topology_pair_t> previously_configured_topology { 
          persistent_data ? boost::make_optional(persistent_data->topology) : boost::none 
        };

        // On Windows the display settings are kept per an active topology list - each topology
        // has separate configuration saved in the database. Therefore, we must always switch
        // to the topology we want to modify before we actually start applying settings.
        topology_result = handle_device_topology_configuration(config, previously_configured_topology, [&]() {
          const bool audio_sink_was_captured { audio_data != nullptr };
          if (!revert_settings(revert_reason_e::topology_switch)) {
            failed_while_reverting_settings = true;
            return false;
          }

          if (audio_sink_was_captured && !audio_data) {
            capture_audio_sink();
          }
          return true;
        }, pre_saved_initial_topology);
      }

      if (!topology_result) {
        // Error already logged
        return { failed_while_reverting_settings ? apply_result_t::result_e::revert_fail : apply_result_t::result_e::topology_fail };
      }

      // Once we have switched to the correct topology, we need to select where we want to
      // save persistent data.
      //
      // If we already have cached persistent data, we want to use that, however we must NOT
      // take over the topology "pair" from the result as the initial topology doest not
      // reflect the actual initial topology before we made our first changes.
      //
      // There is no better way to somehow always guess the initial topology we want to revert to.
      // The user could have switched topology when the stream was paused, then technically we could
      // try to switch back to that topology. However, the display could have also turned off and the
      // topology was automatically changed by Windows. In this case we don't want to switch back to
      // that topology since it was not the user's decision.
      //
      // Therefore, we are always sticking with the first initial topology before the first configuration
      // was applied.
      persistent_data_t new_settings { topology_result->pair };
      if (pre_saved_devices) {
        for (const auto &[device_id, info] : *pre_saved_devices) {
          if (info.friendly_name != ZAKO_NAME && !info.physical_identity.empty()) {
            new_settings.device_identities.emplace(device_id, info.physical_identity);
          }
        }
      }
      if (is_vdd_mode && !config.device_id.empty()) {
        new_settings.vdd_device_ids.insert(config.device_id);
      }

      persistent_data_t &current_settings { persistent_data ? *persistent_data : new_settings };
      if (persistent_data && pre_saved_devices) {
        for (const auto &[device_id, info] : *pre_saved_devices) {
          if (info.friendly_name != ZAKO_NAME && !info.physical_identity.empty()) {
            current_settings.device_identities.emplace(device_id, info.physical_identity);
          }
        }
      }
      if (is_vdd_mode && !config.device_id.empty()) {
        current_settings.vdd_device_ids.insert(config.device_id);
      }
      const bool should_skip_new_vdd_only_persistence =
        is_vdd_mode &&
        !persistent_data &&
        !pre_saved_initial_topology &&
        is_vdd_only_topology(new_settings.topology.initial, config.device_id);

      const auto persist_settings = [&]() -> apply_result_t {
        if (current_settings.contains_modifications()) {
          if (!persistent_data) {
            if (should_skip_new_vdd_only_persistence) {
              BOOST_LOG(warning) << "VDD mode: refusing to persist current VDD-only topology as the initial restore baseline; continuing without new display restore data.";
              return { apply_result_t::result_e::success };
            }

            persistent_data = std::make_unique<persistent_data_t>(new_settings);
          }

          if (!save_settings(filepath, *persistent_data)) {
            return { apply_result_t::result_e::file_save_fail };
          }
        }
        else if (persistent_data) {
          if (!revert_settings(revert_reason_e::config_cleanup)) {
            // Sanity check, as the revert_settings should always pass
            // at this point since our settings contain no modifications.
            return { apply_result_t::result_e::revert_fail };
          }
        }

        return { apply_result_t::result_e::success };
      };

      // Since we will be modifying system state in multiple steps, we
      // have no choice, but to save any changes we have made so
      // that we can undo them if anything fails.
      auto save_guard = util::fail_guard([&]() {
        persist_settings();  // Ignoring the return value
      });

      // Here each of the handler returns full set of their specific settings for
      // all the displays in the topology.
      //
      // We have the same train of though here as with the topology - if we are
      // controlling some parts of the display settings, we are taking what
      // we have before any modification by us are sticking with it until we
      // release the control.
      //
      // Also, since we keep settings for all the displays (not only the ones that
      // we modify), we can use these settings as a base that will revert whatever
      // we did before if we are re-applying settings with different configuration.
      //
      // User modified the resolution manually? Well, he shouldn't have. If we
      // are responsible for the resolution, then hands off! Initial settings
      // will be re-applied when the paused session is resumed.

      const auto original_primary_display { handle_primary_display_configuration(config, current_settings.original_primary_display, topology_result->metadata, topology_result->pair.initial) };
      if (!original_primary_display) {
        // Error already logged
        return { apply_result_t::result_e::primary_display_fail };
      }
      current_settings.original_primary_display = *original_primary_display;

      const auto previous_display_modes = current_settings.original_modes.empty() && pre_saved_initial_modes
        ? *pre_saved_initial_modes
        : current_settings.original_modes;
      const auto original_modes { handle_display_mode_configuration(
        config.resolution,
        config.refresh_rate,
        previous_display_modes,
        topology_result->metadata,
        topology_result->pair.initial) };
      if (!original_modes) {
        // Error already logged
        return { apply_result_t::result_e::modes_fail };
      }
      current_settings.original_modes = *original_modes;
      filter_vdd_devices(current_settings.original_modes);

      // 如果有HDR切换操作，等待其他操作稳定后再进行HDR切换
      if (config.change_hdr_state) {
        BOOST_LOG(info) << "检测到HDR切换操作，等待其他显示器操作稳定...";
        if (!wait_for_display_stability(topology_result->metadata)) {
          BOOST_LOG(warning) << "显示器稳定检查未完全通过，但继续执行HDR切换";
        }
      }

      const auto original_hdr_states { handle_hdr_state_configuration(config.change_hdr_state, current_settings.original_hdr_states, topology_result->metadata) };
      if (!original_hdr_states) {
        // Error already logged
        return { apply_result_t::result_e::hdr_states_fail };
      }
      current_settings.original_hdr_states = *original_hdr_states;
      filter_vdd_devices(current_settings.original_hdr_states);

      const bool wants_physical_profile = client_hdr_enabled &&
                                          !is_vdd_mode &&
                                          profile_setting.policy == color_profile::profile_policy_e::apply;
      const bool existing_profile_matches = current_settings.color_profile &&
                                            wants_physical_profile &&
                                            current_settings.color_profile->device_id == config.device_id &&
                                            current_settings.color_profile->applied_profile == *profile_setting.profile;

      if (current_settings.color_profile && !existing_profile_matches) {
        if (!win_color_profile::restore(*current_settings.color_profile)) {
          return { apply_result_t::result_e::color_profile_fail };
        }
        current_settings.color_profile.reset();
      }

      if (profile_setting.policy == color_profile::profile_policy_e::apply && is_vdd_mode) {
        BOOST_LOG(info) << "Ignoring physical-display ICC override for VDD; client luminance capabilities are programmed directly into the virtual display";
      }

      if (wants_physical_profile) {
        if (!current_settings.color_profile) {
          current_settings.color_profile = win_color_profile::snapshot(config.device_id, *profile_setting.profile);
          if (!current_settings.color_profile) {
            return { apply_result_t::result_e::color_profile_fail };
          }

          // Persist the restore snapshot before changing Windows color state.
          if (const auto persist_result = persist_settings(); !persist_result) {
            return persist_result;
          }
        }

        if (!win_color_profile::apply(*current_settings.color_profile)) {
          return { apply_result_t::result_e::color_profile_fail };
        }
      }

      save_guard.disable();
      return persist_settings();
    } };

    BOOST_LOG(info) << "Applying configuration to the display device.";
    const bool display_may_change { config.device_prep == parsed_config_t::device_prep_e::ensure_only_display };
    if (display_may_change && !audio_data) {
      // It is very likely that in this situation our "current" audio device will be gone, so we
      // want to capture the audio sink immediately and extend the audio session until we revert our changes.
      capture_audio_sink();
    }

    const auto result { do_apply_config(config) };
    if (result) {
      if (!display_may_change && audio_data) {
        // Just to be safe in the future when the video config can be reloaded
        // without Sunshine restarting, we should clean up, because in this situation
        // we have had to revert the changes that turned off other displays. Thus, extending
        // the session for a display that again exist is pointless.
        release_audio_sink();
      }

    }

    if (!result) {
      BOOST_LOG(error) << "Failed to configure display:\n"
                       << result.get_error_message();
    }
    else {
      BOOST_LOG(info) << "Display device configuration applied.";
    }
    return result;
  }

  bool
  settings_t::revert_settings(revert_reason_e reason, bool skip_vdd_destroy) {
    static const char *reason_strs[] = { "串流结束", "拓扑切换", "配置清理", "重置持久化" };
    const char *reason_str = reason_strs[static_cast<int>(reason)];
    BOOST_LOG(info) << "正在恢复显示设备设置 (原因: " << reason_str << ")";

    // 加载持久化设置数据
    if (!persistent_data) {
      BOOST_LOG(info) << "加载显示设备持久化设置";
      persistent_data = load_settings(filepath);
    }

    // 如果存在持久化数据，尝试恢复设置
    if (persistent_data) {
      // 尝试恢复设置
      bool data_updated { false };
      bool success = try_revert_settings(*persistent_data, data_updated, skip_vdd_destroy);
      if (!success) {
        if (data_updated) {
          save_settings(filepath, *persistent_data);  // Best effort; retain remaining restore state for retry.
        }
        BOOST_LOG(error) << "恢复显示设备设置失败！如有异常请尝试关闭基地显示器，或手动修改系统显示设置~";
        return false;
      }

      // 清理持久化数据
      remove_file(filepath);
      persistent_data = nullptr;

      // 释放音频数据
      if (reason != revert_reason_e::topology_switch) {
        release_audio_sink();
      }

      BOOST_LOG(info) << "显示设备配置已恢复";
    }
    return true;
  }

  void
  settings_t::reset_persistence() {
    BOOST_LOG(info) << "Purging persistent display device data (trying to reset settings one last time).";
    if (persistent_data && !revert_settings(revert_reason_e::persistence_reset)) {
      BOOST_LOG(info) << "Failed to revert settings - proceeding to reset persistence.";
    }

    remove_file(filepath);
    persistent_data = nullptr;

    release_audio_sink();
  }

  bool
  settings_t::has_persistent_data() const {
    return persistent_data != nullptr;
  }

  bool
  settings_t::is_vdd_in_initial_topology() const {
    if (!persistent_data) {
      return false;
    }
    
    for (const auto &group : persistent_data->topology.initial) {
      for (const auto &device_id : group) {
        const auto friendly_name = get_display_friendly_name(device_id);
        if (friendly_name == ZAKO_NAME) {
          return true;
        }
      }
    }
    return false;
  }

  void
  settings_t::remove_vdd_from_initial_topology(const std::string& vdd_id) {
    if (!persistent_data) {
      return;
    }
    
    // Remove from initial topology
    for (auto& group : persistent_data->topology.initial) {
      group.erase(
        std::remove(group.begin(), group.end(), vdd_id),
        group.end()
      );
    }
    
    // Remove from modified topology
    for (auto& group : persistent_data->topology.modified) {
      group.erase(
        std::remove(group.begin(), group.end(), vdd_id),
        group.end()
      );
    }
    
    // Remove VDD from HDR states (avoid trying to restore HDR for destroyed VDD)
    if (persistent_data->original_hdr_states.erase(vdd_id) > 0) {
      BOOST_LOG(debug) << "Removed VDD from original_hdr_states: " << vdd_id;
    }
    
    // Remove VDD from display modes (avoid trying to restore mode for destroyed VDD)
    if (persistent_data->original_modes.erase(vdd_id) > 0) {
      BOOST_LOG(debug) << "Removed VDD from original_modes: " << vdd_id;
    }
    persistent_data->device_identities.erase(vdd_id);
    persistent_data->vdd_device_ids.erase(vdd_id);
    if (persistent_data->original_primary_display == vdd_id) {
      persistent_data->original_primary_display.clear();
    }
    
    // Save updated persistent data
    save_settings(filepath, *persistent_data);
  }

  void
  settings_t::replace_vdd_id(const std::string& old_id, const std::string& new_id) {
    if (!persistent_data) {
      return;
    }
    
    // Replace in initial topology
    for (auto& group : persistent_data->topology.initial) {
      std::replace(group.begin(), group.end(), old_id, new_id);
    }
    
    // Replace in modified topology
    for (auto& group : persistent_data->topology.modified) {
      std::replace(group.begin(), group.end(), old_id, new_id);
    }
    
    // Replace VDD ID in HDR states map
    if (auto it = persistent_data->original_hdr_states.find(old_id); it != persistent_data->original_hdr_states.end()) {
      auto hdr_value = it->second;
      persistent_data->original_hdr_states.erase(it);
      persistent_data->original_hdr_states[new_id] = hdr_value;
      BOOST_LOG(debug) << "Replaced VDD ID in original_hdr_states: " << old_id << " -> " << new_id;
    }
    
    // Replace VDD ID in display modes map
    if (auto it = persistent_data->original_modes.find(old_id); it != persistent_data->original_modes.end()) {
      auto mode_value = it->second;
      persistent_data->original_modes.erase(it);
      persistent_data->original_modes[new_id] = mode_value;
      BOOST_LOG(debug) << "Replaced VDD ID in original_modes: " << old_id << " -> " << new_id;
    }

    if (persistent_data->vdd_device_ids.erase(old_id) > 0) {
      persistent_data->vdd_device_ids.insert(new_id);
    }
    if (persistent_data->original_primary_display == old_id) {
      persistent_data->original_primary_display = new_id;
    }
    
    // Save updated persistent data
    save_settings(filepath, *persistent_data);
  }

}  // namespace display_device
