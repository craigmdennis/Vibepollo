/**
 * @file src/steam_activity.h
 * @brief Reads the last Steam game, its art, and the desktop wallpaper for paired clients.
 *
 * The parsing, selection, and file lookup are platform-neutral and take explicit inputs, so
 * they build and test off Windows. Only locating Steam and the signed-in user is Windows-specific.
 */
#pragma once

// standard includes
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "steam_integration.h"

namespace steam_activity {
  constexpr uint64_t steam_id_base = 76561197960265728ULL;

  inline uint64_t to_steam_id(uint32_t account_id) {
    return steam_id_base + account_id;
  }

  /**
   * @brief Account ID of the `loginusers.vdf` entry that signed in last.
   *
   * Prefers `MostRecent` 1, then `AutoLogin` 1, then the newest `timestamp`.
   */
  std::optional<uint32_t> most_recent_account(const platf::steam::vdf_node &loginusers);

  struct game_t {
    uint32_t app_id;
    std::string name;
    int64_t at;  ///< Unix seconds
  };

  struct activity_t {
    uint32_t account_id = 0;  ///< 0 when unknown
    std::optional<uint32_t> running_app_id;
    std::optional<std::string> running_name;
    std::vector<game_t> recent;  ///< Newest first, at most 5. The first entry is the last played game.
    int64_t updated_at = 0;
  };

  /**
   * @brief Build the activity from Vibepollo's Steam catalog: the running app's name and the five newest
   * games by LastPlayed. The catalog holds the newest LastPlayed across every Steam account on the PC.
   */
  activity_t make_activity(const std::vector<platf::steam::game_t> &catalog, uint32_t account_id, uint32_t running_app_id, int64_t now);

  nlohmann::json to_json(const activity_t &activity);

  /** @brief Response when the host switch or the client's permission withholds activity. */
  nlohmann::json json_withheld();

  /** @brief Response when Steam is not installed. */
  nlohmann::json json_no_steam();

  enum class art_kind {
    hero,
    logo,
    capsule,
  };

  std::optional<art_kind> parse_art_kind(std::string_view kind);

  /**
   * @brief Parse 1 to 10 decimal digits that fit in 32 bits.
   */
  std::optional<uint32_t> parse_app_id(std::string_view text);

  constexpr uintmax_t max_art_bytes = 8ULL << 20;
  constexpr uintmax_t max_wallpaper_bytes = 20ULL << 20;

  /** @brief An image held in memory, with its media type. */
  struct image_t {
    std::string type;
    std::string data;
  };

  /**
   * @brief Find the art file for an app in Steam's library cache. Handles the flat and nested layouts.
   * @return A file inside `<root>/appcache/librarycache` of at most `max_art_bytes`, or nothing.
   *         Nothing is returned when `appcache` or `librarycache` is a link, because a link could point outside Steam.
   */
  std::optional<std::filesystem::path> find_art(const std::filesystem::path &root, uint32_t app_id, art_kind kind);

  /**
   * @brief Media type from the file extension (jpg, png, webp). Empty for any other extension.
   */
  std::string_view media_type_for_extension(const std::filesystem::path &file);

  /**
   * @brief Media type from the first bytes: JPEG, PNG, or BMP. Empty for anything else.
   */
  std::string_view sniff_image_type(std::string_view head);

  /**
   * @brief Read an art file when its extension names a supported type and it fits in `max_art_bytes`.
   */
  std::optional<image_t> load_art(const std::filesystem::path &file);

  /**
   * @brief Read a wallpaper when its first bytes are a JPEG, PNG, or BMP and it fits in `max_wallpaper_bytes`.
   */
  std::optional<image_t> load_wallpaper(const std::filesystem::path &file);

  // Windows-backed lookups. On other platforms they report that nothing is installed.
  //
  // Like Vibepollo's Steam library, these read Steam's files as the host process. The signed-in
  // console user's registry values (the running app) come from HKEY_USERS by that user's SID, and
  // the wallpaper is only Windows' TranscodedWallpaper copy in that user's profile.

  /** @brief First Steam folder that Vibepollo's Steam library finds, or nothing when Steam is not installed. */
  std::optional<std::filesystem::path> locate_root();

  /**
   * @brief Activity for the signed-in user, cached for 30 seconds.
   * @return JSON for the full response, or `json_no_steam()`.
   */
  nlohmann::json current_activity();

  /** @brief Art for an app from Steam's library cache. Nothing when no file matches. */
  std::optional<image_t> current_art(uint32_t app_id, art_kind kind);

  /** @brief Wallpaper of the signed-in console user. Nothing when no user is signed in. */
  std::optional<image_t> current_wallpaper();
}  // namespace steam_activity
