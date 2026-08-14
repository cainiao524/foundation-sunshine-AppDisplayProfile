#include <gtest/gtest.h>

#include "src/display_device/current_physical_display.h"

namespace {
  display_device::device_info_t
  device(
    std::string display_name,
    std::string friendly_name,
    display_device::device_state_e state) {
    return {
      std::move(display_name),
      std::move(friendly_name),
      state,
      display_device::hdr_state_e::unknown
    };
  }
}

TEST(CurrentPhysicalDisplay, PrefersAnActivePhysicalPrimaryOverVirtualPrimary) {
  const display_device::device_info_map_t devices {
    {"physical-secondary", device(R"(\\.\DISPLAY2)", "Laptop Panel", display_device::device_state_e::active)},
    {"physical-primary", device(R"(\\.\DISPLAY1)", "Desktop Monitor", display_device::device_state_e::primary)},
    {"virtual-primary", device(R"(\\.\DISPLAY3)", "Zako HDR", display_device::device_state_e::primary)},
  };

  const auto selected = display_device::select_current_physical_display_id(devices, {}, "Zako HDR");

  ASSERT_TRUE(selected);
  EXPECT_EQ(*selected, "physical-primary");
}

TEST(CurrentPhysicalDisplay, AcceptsDisplayAndFriendlyNameSelectors) {
  const display_device::device_info_map_t devices {
    {"physical", device(R"(\\.\DISPLAY1)", "Laptop Panel", display_device::device_state_e::primary)},
  };

  const auto by_display_name = display_device::select_current_physical_display_id(devices, R"(\\.\DISPLAY1)", "Zako HDR");
  const auto by_friendly_name = display_device::select_current_physical_display_id(devices, "Laptop Panel", "Zako HDR");
  ASSERT_TRUE(by_display_name);
  ASSERT_TRUE(by_friendly_name);
  EXPECT_EQ(*by_display_name, "physical");
  EXPECT_EQ(*by_friendly_name, "physical");
}

TEST(CurrentPhysicalDisplay, ExplicitUnavailableOrVirtualSelectorNeverFallsBack) {
  const display_device::device_info_map_t devices {
    {"physical", device(R"(\\.\DISPLAY1)", "Laptop Panel", display_device::device_state_e::primary)},
    {"inactive", device({}, "Disconnected Monitor", display_device::device_state_e::inactive)},
    {"virtual", device(R"(\\.\DISPLAY3)", "Zako HDR", display_device::device_state_e::active)},
  };

  EXPECT_FALSE(display_device::select_current_physical_display_id(devices, "inactive", "Zako HDR"));
  EXPECT_FALSE(display_device::select_current_physical_display_id(devices, "virtual", "Zako HDR"));
  EXPECT_FALSE(display_device::select_current_physical_display_id(devices, "missing", "Zako HDR"));
}

TEST(CurrentPhysicalDisplay, ReturnsNoneWhenOnlyVirtualOrInactiveDisplaysExist) {
  const display_device::device_info_map_t devices {
    {"inactive", device({}, "Disconnected Monitor", display_device::device_state_e::inactive)},
    {"virtual", device(R"(\\.\DISPLAY3)", "Zako HDR", display_device::device_state_e::primary)},
  };

  EXPECT_FALSE(display_device::select_current_physical_display_id(devices, {}, "Zako HDR"));
}
