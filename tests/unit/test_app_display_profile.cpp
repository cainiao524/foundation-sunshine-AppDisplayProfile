#include <gtest/gtest.h>

#include "src/display_device/parsed_config.h"
#include "src/process.h"

namespace {
  using device_prep_e = display_device::parsed_config_t::device_prep_e;
  using refresh_rate_change_e = display_device::parsed_config_t::refresh_rate_change_e;
  using resolution_change_e = display_device::parsed_config_t::resolution_change_e;

  proc::proc_t
  make_processor(proc::ctx_t app) {
    std::vector<proc::ctx_t> apps;
    apps.emplace_back(std::move(app));
    return proc::proc_t {
      boost::this_process::environment(),
      std::move(apps)
    };
  }

  proc::ctx_t
  make_app(int target) {
    proc::ctx_t app {};
    app.id = "42";
    app.name = "Display profile test";
    app.display_target = target;
    return app;
  }

  TEST(AppDisplayProfile, UnconfiguredAppPreservesClientDisplayRequest) {
    auto processor = make_processor(make_app(-1));
    rtsp_stream::launch_session_t session {};
    session.use_vdd = true;
    session.custom_screen_mode = static_cast<int>(device_prep_e::ensure_primary);
    session.custom_vdd_screen_mode = static_cast<int>(display_device::parsed_config_t::vdd_prep_e::vdd_as_secondary);
    session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "client-display";

    EXPECT_FALSE(processor.apply_app_display_profile(42, session));
    EXPECT_TRUE(session.use_vdd);
    EXPECT_EQ(session.custom_screen_mode, static_cast<int>(device_prep_e::ensure_primary));
    EXPECT_EQ(session.custom_vdd_screen_mode, static_cast<int>(display_device::parsed_config_t::vdd_prep_e::vdd_as_secondary));
    EXPECT_EQ(session.env["SUNSHINE_CLIENT_DISPLAY_NAME"].to_string(), "client-display");
    EXPECT_EQ(session.display_target_override, -1);
  }

  TEST(AppDisplayProfile, VirtualAppOverridesTargetAndKeepsClientModes) {
    auto app = make_app(1);
    app.display_device_prep = static_cast<int>(device_prep_e::ensure_secondary);
    app.display_resolution_mode = static_cast<int>(resolution_change_e::automatic);
    app.display_refresh_rate_mode = static_cast<int>(refresh_rate_change_e::automatic);
    auto processor = make_processor(std::move(app));

    rtsp_stream::launch_session_t session {};
    session.use_vdd = false;
    session.custom_screen_mode = static_cast<int>(device_prep_e::ensure_primary);
    session.custom_vdd_screen_mode = static_cast<int>(display_device::parsed_config_t::vdd_prep_e::vdd_as_primary);
    session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "client-display";

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_TRUE(session.use_vdd);
    EXPECT_EQ(session.display_target_override, 1);
    EXPECT_EQ(session.custom_screen_mode, static_cast<int>(device_prep_e::ensure_secondary));
    EXPECT_EQ(session.custom_vdd_screen_mode, -1);
    EXPECT_EQ(session.resolution_change_override, static_cast<int>(resolution_change_e::automatic));
    EXPECT_EQ(session.refresh_rate_change_override, static_cast<int>(refresh_rate_change_e::automatic));
    EXPECT_EQ(session.env.find("SUNSHINE_CLIENT_DISPLAY_NAME"), session.env.end());
  }

  TEST(AppDisplayProfile, PhysicalAppUsesConfiguredDisplayAndFixedModes) {
    auto app = make_app(0);
    app.display_device_prep = static_cast<int>(device_prep_e::ensure_primary);
    app.display_resolution_mode = static_cast<int>(resolution_change_e::manual);
    app.display_resolution = "1920x1080";
    app.display_refresh_rate_mode = static_cast<int>(refresh_rate_change_e::manual);
    app.display_refresh_rate = "60";
    app.display_output_name = "configured-display";
    auto processor = make_processor(std::move(app));

    rtsp_stream::launch_session_t session {};
    session.use_vdd = true;
    session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "client-display";

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_FALSE(session.use_vdd);
    EXPECT_EQ(session.display_target_override, 0);
    EXPECT_EQ(session.app_display_output_name_override, "configured-display");
    EXPECT_EQ(session.resolution_change_override, static_cast<int>(resolution_change_e::manual));
    EXPECT_EQ(session.manual_resolution_override, "1920x1080");
    EXPECT_EQ(session.refresh_rate_change_override, static_cast<int>(refresh_rate_change_e::manual));
    EXPECT_EQ(session.manual_refresh_rate_override, "60");
    EXPECT_EQ(session.env["SUNSHINE_CLIENT_DISPLAY_NAME"].to_string(), "configured-display");
  }

  TEST(AppDisplayProfile, DedicatedVddLayoutFallsBackWhenMissing) {
    EXPECT_EQ(
      display_device::parsed_config_t::resolve_vdd_prep(
        static_cast<int>(display_device::parsed_config_t::vdd_prep_e::vdd_as_primary),
        device_prep_e::ensure_only_display),
      display_device::parsed_config_t::vdd_prep_e::vdd_as_primary);
    EXPECT_EQ(
      display_device::parsed_config_t::resolve_vdd_prep(
        -1,
        device_prep_e::ensure_only_display),
      display_device::parsed_config_t::vdd_prep_e::display_off);
  }
}  // namespace
