#include <array>
#include <utility>

#include <gtest/gtest.h>

#include "src/display_device/parsed_config.h"
#include "src/process.h"

namespace {
  using device_prep_e = display_device::parsed_config_t::device_prep_e;
  using refresh_rate_change_e = display_device::parsed_config_t::refresh_rate_change_e;
  using resolution_change_e = display_device::parsed_config_t::resolution_change_e;

  proc::proc_t
  make_processor(proc::ctx_t app) {
    auto environment = boost::this_process::environment();
    std::vector<proc::ctx_t> apps;
    apps.emplace_back(std::move(app));
    return proc::proc_t {std::move(environment), std::move(apps)};
  }

  proc::ctx_t
  make_app(int target, int prep = -1) {
    proc::ctx_t app {};
    app.id = "42";
    app.name = "Display profile test";
    app.display_target = target;
    app.display_device_prep = prep;
    return app;
  }

  TEST(AppDisplayProfile, UnconfiguredAppPreservesEveryClientDisplayRequest) {
    auto processor = make_processor(make_app(-1));
    rtsp_stream::launch_session_t session {};
    session.use_vdd = true;
    session.custom_screen_mode = static_cast<int>(device_prep_e::ensure_primary);
    session.custom_vdd_screen_mode = 2;
    session.dynamic_resolution_follow_display_override = 1;
    session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "client-display";

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_TRUE(session.use_vdd);
    EXPECT_EQ(session.custom_screen_mode, static_cast<int>(device_prep_e::ensure_primary));
    EXPECT_EQ(session.custom_vdd_screen_mode, 2);
    EXPECT_EQ(session.dynamic_resolution_follow_display_override, 1);
    EXPECT_EQ(session.env["SUNSHINE_CLIENT_DISPLAY_NAME"].to_string(), "client-display");
    EXPECT_EQ(session.display_target_override, -1);
  }

  TEST(AppDisplayProfile, VirtualLayoutsOverrideClientTargetAndLayout) {
    constexpr std::array layouts {
      device_prep_e::no_operation,
      device_prep_e::ensure_primary,
      device_prep_e::ensure_secondary,
      device_prep_e::ensure_only_display,
    };

    for (const auto layout : layouts) {
      SCOPED_TRACE(static_cast<int>(layout));
      auto processor = make_processor(make_app(1, static_cast<int>(layout)));
      rtsp_stream::launch_session_t session {};
      session.use_vdd = false;
      session.custom_screen_mode = static_cast<int>(device_prep_e::ensure_active);
      session.custom_vdd_screen_mode = 2;
      session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "client-physical-display";

      ASSERT_TRUE(processor.apply_app_display_profile(42, session));
      EXPECT_TRUE(session.use_vdd);
      EXPECT_EQ(session.display_target_override, 1);
      EXPECT_EQ(session.custom_screen_mode, static_cast<int>(layout));
      EXPECT_EQ(session.custom_vdd_screen_mode, -1);
      EXPECT_EQ(session.env.find("SUNSHINE_CLIENT_DISPLAY_NAME"), session.env.end());
    }
  }

  TEST(AppDisplayProfile, MissingLayoutDefaultsToEnsureActive) {
    auto processor = make_processor(make_app(1));
    rtsp_stream::launch_session_t session {};
    session.custom_screen_mode = static_cast<int>(device_prep_e::ensure_only_display);

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_EQ(session.custom_screen_mode, static_cast<int>(device_prep_e::ensure_active));
  }

  TEST(AppDisplayProfile, PhysicalTargetOverridesClientAndUsesConfiguredDisplay) {
    auto app = make_app(0, static_cast<int>(device_prep_e::ensure_primary));
    app.display_output_name = "configured-physical-display";
    auto processor = make_processor(std::move(app));
    rtsp_stream::launch_session_t session {};
    session.use_vdd = true;
    session.custom_screen_mode = static_cast<int>(device_prep_e::ensure_secondary);
    session.custom_vdd_screen_mode = 2;
    session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "client-display";

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_FALSE(session.use_vdd);
    EXPECT_EQ(session.display_target_override, 0);
    EXPECT_EQ(session.custom_screen_mode, static_cast<int>(device_prep_e::ensure_primary));
    EXPECT_EQ(session.custom_vdd_screen_mode, -1);
    EXPECT_EQ(session.env["SUNSHINE_CLIENT_DISPLAY_NAME"].to_string(), "configured-physical-display");
  }

  TEST(AppDisplayProfile, DisplayModeAndDynamicResolutionOverridesUseBaseEnums) {
    auto app = make_app(1, static_cast<int>(device_prep_e::ensure_active));
    app.display_resolution_mode = static_cast<int>(resolution_change_e::no_operation);
    app.display_refresh_rate_mode = static_cast<int>(refresh_rate_change_e::no_operation);
    app.display_dynamic_resolution_follow_display = 0;
    auto processor = make_processor(std::move(app));
    rtsp_stream::launch_session_t session {};
    session.resolution_change_override = static_cast<int>(resolution_change_e::automatic);
    session.refresh_rate_change_override = static_cast<int>(refresh_rate_change_e::automatic);
    session.dynamic_resolution_follow_display_override = 1;

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_EQ(session.resolution_change_override, static_cast<int>(resolution_change_e::no_operation));
    EXPECT_EQ(session.refresh_rate_change_override, static_cast<int>(refresh_rate_change_e::no_operation));
    EXPECT_EQ(session.dynamic_resolution_follow_display_override, 0);
  }

}  // namespace
