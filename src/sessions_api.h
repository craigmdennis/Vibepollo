/**
 * @file src/sessions_api.h
 * @brief JSON for GET /api/sessions: which paired clients are streaming now, for home automation (Apollo fork).
 *
 * Moonlight clients hold no connection while idle, so an active stream is the only reliable
 * "online" signal. The contract is in docs/api.md.
 */
#pragma once

// standard includes
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace sessions_api {
  struct session_t {
    std::string client_uuid;
    std::string client_name;
    std::string state;  ///< "starting", "running", "stopping", or "stopped"
    std::string peer;
    std::int64_t started_at_unix {0};
    bool virtual_display {false};
    int width {0};
    int height {0};
    int fps {0};
    bool hdr {false};
  };

  struct app_t {
    int id {0};
    std::string name;
  };

  inline std::string iso8601_utc(std::int64_t unix_seconds) {
    const auto time = static_cast<std::time_t>(unix_seconds);
    std::tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    char text[sizeof "2026-11-06T18:22:31Z"] {};
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return text;
  }

  /**
   * @brief A stream counts while it starts or runs. A stopping stream no longer counts, so
   * `streaming` turns false as soon as the last stream begins to end.
   */
  inline bool counts(const session_t &session) {
    return session.state == "starting" || session.state == "running";
  }

  inline nlohmann::json to_json(const std::vector<session_t> &sessions, const std::optional<app_t> &app) {
    auto list = nlohmann::json::array();
    for (const auto &session : sessions) {
      if (!counts(session)) {
        continue;
      }
      list.push_back({
        {"client", {{"uuid", session.client_uuid}, {"name", session.client_name}}},
        {"app", app ? nlohmann::json {{"id", app->id}, {"name", app->name}} : nlohmann::json()},
        {"state", session.state},
        {"started_at", iso8601_utc(session.started_at_unix)},
        {"peer", session.peer},
        {"display", {{"virtual", session.virtual_display}, {"width", session.width}, {"height", session.height}, {"fps", session.fps}, {"hdr", session.hdr}}},
      });
    }
    const auto count = list.size();
    return {
      {"status", true},
      {"version", 1},
      {"streaming", count > 0},
      {"session_count", count},
      {"sessions", std::move(list)},
    };
  }
}  // namespace sessions_api
