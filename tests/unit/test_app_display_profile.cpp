#include <array>
#include <utility>

#include <gtest/gtest.h>

#include "src/display_device/parsed_config.h"
#include "src/display_device/session.h"
#include "src/nvhttp_stream_start.h"
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
    EXPECT_EQ(session.appid, 42);
  }

  TEST(AppDisplayProfile, ExplicitPhysicalTargetBlocksAutomaticVddFallback) {
    auto processor = make_processor(make_app(0, static_cast<int>(device_prep_e::ensure_active)));
    rtsp_stream::launch_session_t session {};

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    const display_device::display_intent_t intent {
      display_device::display_intent_t::target_e::physical,
      {},
      false,
      device_prep_e::ensure_active,
    };

    EXPECT_FALSE(nvhttp::stream_start::automatic_vdd_fallback_allowed_by_request(intent, session));
  }

  TEST(AppDisplayProfile, UnconfiguredTargetKeepsAutomaticVddFallbackAvailable) {
    auto processor = make_processor(make_app(-1));
    rtsp_stream::launch_session_t session {};

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    const display_device::display_intent_t intent {
      display_device::display_intent_t::target_e::physical,
      {},
      false,
      device_prep_e::ensure_active,
    };

    EXPECT_TRUE(nvhttp::stream_start::automatic_vdd_fallback_allowed_by_request(intent, session));
  }

  TEST(AppDisplayProfile, VddIdentityDoesNotChangeWhenSameClientSwitchesApps) {
    rtsp_stream::launch_session_t first_session {};
    first_session.appid = 41;
    first_session.client_cert_uuid = "same-client";
    rtsp_stream::launch_session_t second_session {};
    second_session.appid = 42;
    second_session.client_cert_uuid = "same-client";

    EXPECT_EQ(
      display_device::resolve_vdd_identifier(false, first_session),
      display_device::resolve_vdd_identifier(false, second_session));
    EXPECT_EQ(display_device::resolve_vdd_identifier(false, first_session), "same-client");
    EXPECT_EQ(display_device::resolve_vdd_identifier(true, first_session), "shared_vdd");
    EXPECT_EQ(display_device::resolve_vdd_identifier(true, second_session), "shared_vdd");
  }

  TEST(AppDisplayProfile, PersistentDualGpuRestorePrecedesVddDestruction) {
    using cleanup_timing_e = display_device::vdd_cleanup_timing_e;

    EXPECT_EQ(
      display_device::resolve_vdd_cleanup_timing(true, false, true, false),
      cleanup_timing_e::after_restore);
    EXPECT_EQ(
      display_device::resolve_vdd_cleanup_timing(true, false, false, false),
      cleanup_timing_e::before_restore);
    EXPECT_EQ(
      display_device::resolve_vdd_cleanup_timing(true, true, true, false),
      cleanup_timing_e::none);
    EXPECT_EQ(
      display_device::resolve_vdd_cleanup_timing(true, false, true, true),
      cleanup_timing_e::none);
    EXPECT_EQ(
      display_device::resolve_vdd_cleanup_timing(false, false, true, false),
      cleanup_timing_e::none);
  }

  TEST(AppDisplayProfile, UnifiedLayoutsMapToTheExistingPhysicalAndVddActions) {
    using vdd_prep_e = display_device::parsed_config_t::vdd_prep_e;

    EXPECT_EQ(
      display_device::parsed_config_t::to_vdd_prep(device_prep_e::ensure_primary),
      vdd_prep_e::vdd_as_primary);
    EXPECT_EQ(
      display_device::parsed_config_t::to_vdd_prep(device_prep_e::ensure_secondary),
      vdd_prep_e::vdd_as_secondary);
    EXPECT_EQ(
      display_device::parsed_config_t::to_vdd_prep(device_prep_e::ensure_only_display),
      vdd_prep_e::display_off);
    EXPECT_EQ(
      display_device::parsed_config_t::to_physical_device_prep(device_prep_e::ensure_secondary),
      device_prep_e::ensure_active);
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

  TEST(AppDisplayProfile, MissingConfiguredPhysicalDisplayFallsBackToPrimary) {
    auto app = make_app(0, static_cast<int>(device_prep_e::ensure_active));
    app.display_output_name = "__sunshine_app_display_profile_missing_8f6d4f2c__";
    auto processor = make_processor(std::move(app));
    rtsp_stream::launch_session_t session {};

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_EQ(session.app_display_output_name_override, "__sunshine_app_display_profile_missing_8f6d4f2c__");

    config::video_t config {};
    const auto intent = display_device::resolve_display_intent(config, session);
    EXPECT_EQ(intent.target, display_device::display_intent_t::target_e::physical);
    EXPECT_TRUE(intent.device_id.empty());
    EXPECT_FALSE(intent.user_named_display);
  }

  TEST(AppDisplayProfile, MissingClientPhysicalDisplayIsUnavailable) {
    rtsp_stream::launch_session_t session {};
    session.env["SUNSHINE_CLIENT_DISPLAY_NAME"] = "__sunshine_client_display_missing_4c91a7be__";

    config::video_t config {};
    const auto intent = display_device::resolve_display_intent(config, session);
    EXPECT_EQ(intent.target, display_device::display_intent_t::target_e::unavailable);
    EXPECT_EQ(intent.device_id, "__sunshine_client_display_missing_4c91a7be__");
    EXPECT_TRUE(intent.user_named_display);
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

  TEST(AppDisplayProfile, ForcedHdrStateOverridesClientRequest) {
    auto app = make_app(1, static_cast<int>(device_prep_e::ensure_active));
    app.display_hdr_policy = 2;
    app.display_hdr_state = 1;
    auto processor = make_processor(std::move(app));
    rtsp_stream::launch_session_t session {};
    session.enable_hdr = false;

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_EQ(session.hdr_policy_override, 2);
    EXPECT_EQ(session.hdr_state_override, 1);
    EXPECT_TRUE(session.enable_hdr);
  }

  TEST(AppDisplayProfile, IgnoreClientHdrPolicyDoesNotChangeSessionRequest) {
    auto app = make_app(1, static_cast<int>(device_prep_e::ensure_active));
    app.display_hdr_policy = 0;
    auto processor = make_processor(std::move(app));
    rtsp_stream::launch_session_t session {};
    session.enable_hdr = true;

    ASSERT_TRUE(processor.apply_app_display_profile(42, session));
    EXPECT_EQ(session.hdr_policy_override, 0);
    EXPECT_TRUE(session.enable_hdr);
  }

}  // namespace
