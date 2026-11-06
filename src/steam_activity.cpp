/**
 * @file src/steam_activity.cpp
 * @brief Selene's Steam activity: the response built from Vibepollo's Steam catalog, art, and wallpaper.
 */
#include "steam_activity.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <tuple>

namespace fs = std::filesystem;

namespace steam_activity {
  namespace {
    bool iequals(std::string_view a, std::string_view b) {
      return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
      });
    }


    std::optional<std::string> read_text(const fs::path &file, uintmax_t max_bytes) {
      std::ifstream in(file, std::ios::binary);
      if (!in) {
        return std::nullopt;
      }
      std::string text;
      char chunk[64 * 1024];
      while (in.read(chunk, sizeof(chunk)) || in.gcount() > 0) {
        text.append(chunk, static_cast<size_t>(in.gcount()));
        if (text.size() > max_bytes) {
          return std::nullopt;
        }
      }
      return text;
    }

    std::optional<uint64_t> to_u64(std::string_view text) {
      if (text.empty() || text.size() > 20 || !std::ranges::all_of(text, [](unsigned char c) {
            return std::isdigit(c);
          })) {
        return std::nullopt;
      }
      return std::strtoull(std::string(text).c_str(), nullptr, 10);
    }

    bool inside(const fs::path &dir, const fs::path &file) {
      auto rel = file.lexically_relative(dir);
      return !rel.empty() && *rel.begin() != "..";
    }
  }  // namespace

  std::optional<uint32_t> most_recent_account(const platf::steam::vdf_node &loginusers) {
    const auto *users = loginusers.find("users");
    if (!users) {
      return std::nullopt;
    }
    // Current Steam clients often omit MostRecent, so rank by MostRecent, then AutoLogin,
    // then the newest sign-in timestamp.
    auto flag = [](const platf::steam::vdf_node &user, const std::string &name) {
      const auto *value = user.find(name);
      return value && value->value == "1";
    };
    std::optional<uint32_t> best;
    std::tuple<bool, bool, uint64_t> best_rank {};
    for (const auto &[key, user] : users->children) {
      auto id = to_u64(key);
      if (!id || *id <= steam_id_base || *id - steam_id_base > UINT32_MAX) {
        continue;
      }
      const auto *timestamp = user.find("timestamp");
      std::tuple<bool, bool, uint64_t> rank {
        flag(user, "MostRecent"),
        flag(user, "AutoLogin"),
        timestamp ? to_u64(timestamp->value).value_or(0) : 0
      };
      if (!best || rank > best_rank) {
        best = static_cast<uint32_t>(*id - steam_id_base);
        best_rank = rank;
      }
    }
    return best;
  }

  activity_t make_activity(const std::vector<platf::steam::game_t> &catalog, uint32_t account_id, uint32_t running_app_id, int64_t now) {
    activity_t out;
    out.account_id = account_id;
    out.updated_at = now;

    auto name_of = [&](uint32_t id) -> std::optional<std::string> {
      for (const auto &game : catalog) {
        if (game.app_id == id && !game.name.empty()) {
          return game.name;
        }
      }
      return std::nullopt;
    };
    if (running_app_id != 0) {
      out.running_app_id = running_app_id;
      out.running_name = name_of(running_app_id);
    }

    // Vibepollo's catalog keeps the newest LastPlayed across every Steam account on the PC.
    // Only installed games, which a client can launch.
    std::vector<const platf::steam::game_t *> played;
    for (const auto &game : catalog) {
      if (game.installed && game.last_played > 0 && game.last_played <= INT64_MAX && !game.name.empty()) {
        played.push_back(&game);
      }
    }
    std::ranges::sort(played, [](const auto *a, const auto *b) {
      return a->last_played != b->last_played ? a->last_played > b->last_played : a->app_id < b->app_id;
    });
    for (const auto *game : played) {
      if (out.recent.size() == 5) {
        break;
      }
      out.recent.push_back({game->app_id, game->name, static_cast<int64_t>(game->last_played)});
    }
    return out;
  }

  nlohmann::json to_json(const activity_t &a) {
    auto game = [](const game_t &g) {
      return nlohmann::json {{"appId", g.app_id}, {"name", g.name}, {"at", g.at}};
    };
    nlohmann::json j;
    j["version"] = 1;
    j["steamInstalled"] = true;
    j["account"] = a.account_id ? nlohmann::json(std::to_string(to_steam_id(a.account_id))) : nlohmann::json(nullptr);
    j["runningAppId"] = a.running_app_id ? nlohmann::json(*a.running_app_id) : nlohmann::json(nullptr);
    j["runningName"] = a.running_name ? nlohmann::json(*a.running_name) : nlohmann::json(nullptr);
    j["lastPlayed"] = a.recent.empty() ? nlohmann::json(nullptr) : game(a.recent.front());
    j["recent"] = nlohmann::json::array();
    for (const auto &g : a.recent) {
      j["recent"].push_back(game(g));
    }
    j["updatedAt"] = a.updated_at;
    return j;
  }

  nlohmann::json json_withheld() {
    return {{"version", 1}, {"sharing", false}};
  }

  nlohmann::json json_no_steam() {
    return {{"version", 1}, {"steamInstalled", false}};
  }

  std::optional<art_kind> parse_art_kind(std::string_view kind) {
    if (kind == "hero") {
      return art_kind::hero;
    }
    if (kind == "logo") {
      return art_kind::logo;
    }
    if (kind == "capsule") {
      return art_kind::capsule;
    }
    return std::nullopt;
  }

  std::optional<uint32_t> parse_app_id(std::string_view text) {
    if (text.empty() || text.size() > 10) {
      return std::nullopt;
    }
    auto value = to_u64(text);
    if (!value || *value == 0 || *value > UINT32_MAX) {
      return std::nullopt;
    }
    return static_cast<uint32_t>(*value);
  }

  std::string_view media_type_for_extension(const fs::path &file) {
    std::string ext = file.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
      return std::tolower(c);
    });
    if (ext == ".jpg" || ext == ".jpeg") {
      return "image/jpeg";
    }
    if (ext == ".png") {
      return "image/png";
    }
    if (ext == ".webp") {
      return "image/webp";
    }
    return {};
  }

  std::string_view sniff_image_type(std::string_view head) {
    if (head.starts_with("\xFF\xD8\xFF")) {
      return "image/jpeg";
    }
    if (head.starts_with("\x89PNG")) {
      return "image/png";
    }
    if (head.starts_with("BM")) {
      return "image/bmp";
    }
    return {};
  }

  namespace {
    std::optional<std::string> read_limited(const fs::path &file, uintmax_t max_bytes) {
      std::error_code ec;
      if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) > max_bytes || ec) {
        return std::nullopt;
      }
      return read_text(file, max_bytes);
    }
  }  // namespace

  std::optional<image_t> load_art(const fs::path &file) {
    auto type = media_type_for_extension(file);
    auto data = type.empty() ? std::nullopt : read_limited(file, max_art_bytes);
    if (!data) {
      return std::nullopt;
    }
    return image_t {std::string(type), std::move(*data)};
  }

  std::optional<image_t> load_wallpaper(const fs::path &file) {
    auto data = read_limited(file, max_wallpaper_bytes);
    if (!data) {
      return std::nullopt;
    }
    auto type = sniff_image_type(*data);
    if (type.empty()) {
      return std::nullopt;
    }
    return image_t {std::string(type), std::move(*data)};
  }

  std::optional<fs::path> find_art(const fs::path &root, uint32_t app_id, art_kind kind) {
    std::error_code ec;
    auto cache = fs::canonical(root / "appcache" / "librarycache", ec);
    auto expected = fs::canonical(root, ec) / "appcache" / "librarycache";
    // A link at appcache or librarycache makes the resolved path differ from the literal one.
    if (ec || !iequals(cache.generic_string(), expected.generic_string()) || !fs::is_directory(cache, ec)) {
      return std::nullopt;
    }

    // Names in preference order. The capsule falls back to the header image.
    std::vector<std::string> names;
    switch (kind) {
      case art_kind::hero:
        names = {"library_hero.jpg"};
        break;
      case art_kind::logo:
        names = {"logo.png"};
        break;
      case art_kind::capsule:
        // Current Steam clients write library_capsule.jpg and library_header.jpg instead.
        names = {"library_600x900.jpg", "library_capsule.jpg", "header.jpg", "library_header.jpg"};
        break;
    }

    auto accept = [&](const fs::path &candidate) -> std::optional<fs::path> {
      std::error_code e;
      if (!fs::is_regular_file(candidate, e) || fs::file_size(candidate, e) > max_art_bytes) {
        return std::nullopt;
      }
      auto canonical = fs::canonical(candidate, e);
      if (e || !inside(cache, canonical) || media_type_for_extension(canonical).empty()) {
        return std::nullopt;
      }
      return canonical;
    };

    const std::string id = std::to_string(app_id);
    for (const auto &name : names) {
      // Flat layout: librarycache/<appId>_<name>
      if (auto hit = accept(cache / (id + "_" + name))) {
        return hit;
      }
      // Nested layout: librarycache/<appId>/<hash>/<name>, hash folder name unknown.
      auto dir = cache / id;
      if (!fs::is_directory(dir, ec)) {
        continue;
      }
      if (auto hit = accept(dir / name)) {
        return hit;
      }
      std::set<fs::path> subdirs;
      for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec)) {
          subdirs.insert(entry.path());
        }
      }
      for (const auto &sub : subdirs) {
        if (auto hit = accept(sub / name)) {
          return hit;
        }
      }
    }
    return std::nullopt;
  }
}  // namespace steam_activity
