/**
 * @file tests/unit/test_sessions_api.cpp
 * @brief Tests for the GET /api/sessions response (Apollo fork).
 */
#include <gtest/gtest.h>
#include <src/sessions_api.h>

namespace {
  sessions_api::session_t apple_tv(std::string state = "running") {
    return {
      .client_uuid = "0F8E3D1C-1111-2222-3333-444455556666",
      .client_name = "Apple TV",
      .state = std::move(state),
      .peer = "192.168.1.42",
      .started_at_unix = 1793989351,  // 2026-11-06T18:22:31Z
      .virtual_display = true,
      .width = 3840,
      .height = 2160,
      .fps = 60,
      .hdr = true,
    };
  }
}  // namespace

TEST(SessionsApi, IdleHostReportsNoStream) {
  const auto json = sessions_api::to_json({}, std::nullopt);
  EXPECT_EQ(json, nlohmann::json::parse(R"({"status":true,"version":1,"streaming":false,"session_count":0,"sessions":[]})"));
}

TEST(SessionsApi, OneSessionMatchesTheContract) {
  const auto json = sessions_api::to_json({apple_tv()}, sessions_api::app_t {3, "Steam"});
  EXPECT_EQ(json, nlohmann::json::parse(R"({
    "status": true, "version": 1, "streaming": true, "session_count": 1,
    "sessions": [{
      "client": {"uuid": "0F8E3D1C-1111-2222-3333-444455556666", "name": "Apple TV"},
      "app": {"id": 3, "name": "Steam"},
      "state": "running",
      "started_at": "2026-11-06T18:22:31Z",
      "peer": "192.168.1.42",
      "display": {"virtual": true, "width": 3840, "height": 2160, "fps": 60, "hdr": true}
    }]
  })"));
}

TEST(SessionsApi, CountsStartingAndRunningStreams) {
  auto second = apple_tv("starting");
  second.client_name = "MacBook";
  const auto json = sessions_api::to_json({apple_tv(), second}, sessions_api::app_t {1, "Desktop"});
  EXPECT_TRUE(json["streaming"]);
  EXPECT_EQ(json["session_count"], 2);
  EXPECT_EQ(json["sessions"][1]["client"]["name"], "MacBook");
}

TEST(SessionsApi, StoppingStreamNoLongerCounts) {
  const auto json = sessions_api::to_json({apple_tv("stopping"), apple_tv("stopped")}, sessions_api::app_t {3, "Steam"});
  EXPECT_FALSE(json["streaming"]);
  EXPECT_EQ(json["session_count"], 0);
  EXPECT_TRUE(json["sessions"].empty());
}

TEST(SessionsApi, NoRunningAppGivesNullApp) {
  const auto json = sessions_api::to_json({apple_tv()}, std::nullopt);
  EXPECT_TRUE(json["sessions"][0]["app"].is_null());
}

TEST(SessionsApi, FormatsUtcTimestamps) {
  EXPECT_EQ(sessions_api::iso8601_utc(0), "1970-01-01T00:00:00Z");
  EXPECT_EQ(sessions_api::iso8601_utc(1793989351), "2026-11-06T18:22:31Z");
}
