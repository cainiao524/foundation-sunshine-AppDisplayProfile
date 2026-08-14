#include <gtest/gtest.h>

#include "src/display_device/parsed_config.h"

namespace {
  using parsed_config_t = display_device::parsed_config_t;

  TEST(FoundationMoonlightDisplayCompat, UsesDedicatedVddLayoutWhenValid) {
    EXPECT_EQ(
      parsed_config_t::resolve_vdd_prep(
        1,
        parsed_config_t::device_prep_e::ensure_only_display
      ),
      parsed_config_t::vdd_prep_e::vdd_as_primary
    );
    EXPECT_EQ(
      parsed_config_t::resolve_vdd_prep(
        2,
        parsed_config_t::device_prep_e::ensure_primary
      ),
      parsed_config_t::vdd_prep_e::vdd_as_secondary
    );
  }

  TEST(FoundationMoonlightDisplayCompat, FallsBackToUnifiedModeWhenDedicatedValueIsAbsentOrInvalid) {
    EXPECT_EQ(
      parsed_config_t::resolve_vdd_prep(
        -1,
        parsed_config_t::device_prep_e::ensure_only_display
      ),
      parsed_config_t::vdd_prep_e::display_off
    );
    EXPECT_EQ(
      parsed_config_t::resolve_vdd_prep(
        99,
        parsed_config_t::device_prep_e::ensure_secondary
      ),
      parsed_config_t::vdd_prep_e::vdd_as_secondary
    );
  }
}  // namespace
