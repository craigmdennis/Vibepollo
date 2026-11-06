/**
 * @file tests/unit/test_steam_activity.cpp
 * @brief Tests for the activity built from Vibepollo's Steam catalog, art lookup, and wallpaper loading.
 */
#include <fstream>
#include <random>

#include <gtest/gtest.h>

#include "../../src/steam_activity.h"

namespace fs = std::filesystem;
using namespace steam_activity;

namespace {
  class SteamActivityTest: public ::testing::Test {
  protected:
    fs::path root;

    void SetUp() override {
      std::random_device rd;
      root = fs::temp_directory_path() / ("steam_activity_" + std::to_string(rd()));
      fs::create_directories(root);
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(root, ec);
    }

    static void write(const fs::path &file, const std::string &text) {
      fs::create_directories(file.parent_path());
      std::ofstream(file, std::ios::binary) << text;
    }
  };
}  // namespace

namespace {
  platf::steam::game_t game(uint32_t id, std::string name, uint64_t last_played, bool installed = true) {
    platf::steam::game_t g;
    g.app_id = id;
    g.name = std::move(name);
    g.last_played = last_played;
    g.installed = installed;
    return g;
  }

  std::optional<uint32_t> account_from(const std::string &loginusers) {
    return most_recent_account(platf::steam::parse_vdf(loginusers));
  }
}  // namespace

TEST_F(SteamActivityTest, ConvertsAccountIdToSteamId) {
  EXPECT_EQ(to_steam_id(39734272), 76561198000000000ULL);
}

TEST_F(SteamActivityTest, FindsMostRecentAccount) {
  EXPECT_EQ(account_from(R"("users" {
    "76561198000000001" { "AccountName" "a" "MostRecent" "0" }
    "76561198000000000" { "AccountName" "b" "MostRecent" "1" }
  })"), 39734272u);
  EXPECT_EQ(account_from("\"users\" { }"), std::nullopt);
}

TEST_F(SteamActivityTest, FallsBackToAutoLoginThenNewestTimestamp) {
  // Current Steam clients write no MostRecent key.
  EXPECT_EQ(account_from(R"("users" {
    "76561198000000001" { "AutoLogin" "0" "timestamp" "2000" }
    "76561198000000000" { "AutoLogin" "1" "timestamp" "1000" }
  })"), 39734272u);
  EXPECT_EQ(account_from(R"("users" {
    "76561198000000000" { "AutoLogin" "0" "timestamp" "1000" }
    "76561198000000001" { "AutoLogin" "0" "timestamp" "2000" }
  })"), 39734273u);
  EXPECT_EQ(account_from(R"("users" { "76561198000000000" { "AccountName" "a" } })"), 39734272u);
}

TEST_F(SteamActivityTest, RefusesAnAccountIdThatDoesNotFitIn32Bits) {
  EXPECT_EQ(account_from("\"users\" { \"" + std::to_string(steam_id_base + (1ULL << 32) + 5) + "\" { \"MostRecent\" \"1\" } }"), std::nullopt);
}

TEST_F(SteamActivityTest, PicksNewestInstalledGames) {
  std::vector<platf::steam::game_t> catalog {
    game(100, "Game A", 300),
    game(200, "Uninstalled", 500, false),
    game(300, "Game C", 400),
    game(400, "Never played", 0),
    game(500, "", 450),
  };
  auto a = make_activity(catalog, 42, 300, 1000);
  ASSERT_EQ(a.recent.size(), 2u);
  EXPECT_EQ(a.recent[0].app_id, 300u);
  EXPECT_EQ(a.recent[0].name, "Game C");
  EXPECT_EQ(a.recent[0].at, 400);
  EXPECT_EQ(a.recent[1].app_id, 100u);
  EXPECT_EQ(a.running_app_id, 300u);
  EXPECT_EQ(a.running_name, "Game C");
  EXPECT_EQ(a.account_id, 42u);
  EXPECT_EQ(a.updated_at, 1000);
}

TEST_F(SteamActivityTest, KeepsAtMostFiveRecentGames) {
  std::vector<platf::steam::game_t> catalog;
  for (uint32_t id = 1; id <= 8; ++id) {
    catalog.push_back(game(id, "Game " + std::to_string(id), 100 + id));
  }
  auto a = make_activity(catalog, 42, 0, 1);
  ASSERT_EQ(a.recent.size(), 5u);
  EXPECT_EQ(a.recent.front().app_id, 8u);
  EXPECT_FALSE(a.running_app_id.has_value());
}

TEST_F(SteamActivityTest, ReportsNoGameForAnEmptyCatalog) {
  auto a = make_activity({}, 0, 7, 1);
  EXPECT_TRUE(a.recent.empty());
  EXPECT_EQ(a.running_app_id, 7u);
  EXPECT_FALSE(a.running_name.has_value());
}

TEST_F(SteamActivityTest, JsonHasTheContractShape) {
  auto j = to_json(make_activity({game(100, "Game A", 300)}, 39734272, 0, 1000));
  EXPECT_EQ(j["version"], 1);
  EXPECT_EQ(j["steamInstalled"], true);
  EXPECT_EQ(j["account"], "76561198000000000");
  EXPECT_TRUE(j["runningAppId"].is_null());
  EXPECT_TRUE(j["runningName"].is_null());
  EXPECT_EQ(j["lastPlayed"]["appId"], 100);
  EXPECT_EQ(j["lastPlayed"]["name"], "Game A");
  EXPECT_EQ(j["lastPlayed"]["at"], 300);
  EXPECT_EQ(j["recent"].size(), 1u);
  EXPECT_EQ(j["updatedAt"], 1000);

  auto none = to_json(make_activity({}, 0, 0, 1));
  EXPECT_TRUE(none["account"].is_null());
  EXPECT_TRUE(none["lastPlayed"].is_null());
  EXPECT_TRUE(none["recent"].empty());
}

TEST_F(SteamActivityTest, ShortResponsesHaveTheContractShape) {
  EXPECT_EQ(json_withheld().dump(), R"({"sharing":false,"version":1})");
  EXPECT_EQ(json_no_steam().dump(), R"({"steamInstalled":false,"version":1})");
}

TEST_F(SteamActivityTest, ParsesOnlyValidAppIdsAndKinds) {
  EXPECT_EQ(parse_app_id("1234"), 1234u);
  EXPECT_EQ(parse_app_id("4294967295"), 4294967295u);
  EXPECT_EQ(parse_app_id("4294967296"), std::nullopt);
  EXPECT_EQ(parse_app_id("0"), std::nullopt);
  EXPECT_EQ(parse_app_id(""), std::nullopt);
  EXPECT_EQ(parse_app_id("12345678901"), std::nullopt);
  EXPECT_EQ(parse_app_id("../1"), std::nullopt);
  EXPECT_EQ(parse_app_id("12a"), std::nullopt);
  EXPECT_EQ(parse_art_kind("hero"), art_kind::hero);
  EXPECT_EQ(parse_art_kind("logo"), art_kind::logo);
  EXPECT_EQ(parse_art_kind("capsule"), art_kind::capsule);
  EXPECT_EQ(parse_art_kind("Hero"), std::nullopt);
  EXPECT_EQ(parse_art_kind("../../x"), std::nullopt);
}

TEST_F(SteamActivityTest, FindsArtInTheFlatLayout) {
  auto cache = root / "appcache" / "librarycache";
  write(cache / "100_library_hero.jpg", "x");
  write(cache / "100_logo.png", "x");
  write(cache / "100_header.jpg", "x");
  EXPECT_EQ(find_art(root, 100, art_kind::hero)->filename(), "100_library_hero.jpg");
  EXPECT_EQ(find_art(root, 100, art_kind::logo)->filename(), "100_logo.png");
  EXPECT_EQ(find_art(root, 100, art_kind::capsule)->filename(), "100_header.jpg");
  EXPECT_EQ(find_art(root, 101, art_kind::hero), std::nullopt);
}

TEST_F(SteamActivityTest, FindsArtInTheNestedLayout) {
  auto cache = root / "appcache" / "librarycache";
  write(cache / "200" / "abc123" / "library_hero.jpg", "x");
  write(cache / "200" / "def456" / "library_600x900.jpg", "x");
  EXPECT_EQ(find_art(root, 200, art_kind::hero)->parent_path().filename(), "abc123");
  EXPECT_EQ(find_art(root, 200, art_kind::capsule)->filename(), "library_600x900.jpg");
  EXPECT_EQ(find_art(root, 200, art_kind::logo), std::nullopt);
}

TEST_F(SteamActivityTest, FindsCapsuleUnderCurrentSteamNames) {
  auto cache = root / "appcache" / "librarycache";
  write(cache / "200" / "abc123" / "library_capsule.jpg", "x");
  write(cache / "200" / "def456" / "library_header.jpg", "x");
  EXPECT_EQ(find_art(root, 200, art_kind::capsule)->filename(), "library_capsule.jpg");
  write(cache / "300" / "def456" / "library_header.jpg", "x");
  EXPECT_EQ(find_art(root, 300, art_kind::capsule)->filename(), "library_header.jpg");
}

TEST_F(SteamActivityTest, RefusesArtThatLeavesTheCacheOrIsTooLarge) {
  auto cache = root / "appcache" / "librarycache";
  fs::create_directories(cache);
  write(root / "secret.jpg", "x");
  std::error_code ec;
  fs::create_symlink(root / "secret.jpg", cache / "300_library_hero.jpg", ec);
  if (!ec) {
    EXPECT_EQ(find_art(root, 300, art_kind::hero), std::nullopt);
  }
  write(cache / "400_library_hero.jpg", std::string(max_art_bytes + 1, 'x'));
  EXPECT_EQ(find_art(root, 400, art_kind::hero), std::nullopt);
  write(cache / "500_library_hero.txt", "x");
  EXPECT_EQ(find_art(root, 500, art_kind::hero), std::nullopt);
}

TEST_F(SteamActivityTest, NamesMediaTypesFromExtensionAndBytes) {
  EXPECT_EQ(media_type_for_extension("a.JPG"), "image/jpeg");
  EXPECT_EQ(media_type_for_extension("a.png"), "image/png");
  EXPECT_EQ(media_type_for_extension("a.webp"), "image/webp");
  EXPECT_EQ(media_type_for_extension("a.txt"), "");
  EXPECT_EQ(sniff_image_type("\xFF\xD8\xFF\xE0"), "image/jpeg");
  EXPECT_EQ(sniff_image_type("\x89PNG"), "image/png");
  EXPECT_EQ(sniff_image_type("BM\x00\x00"), "image/bmp");
  EXPECT_EQ(sniff_image_type("GIF8"), "");
  EXPECT_EQ(sniff_image_type(""), "");
}

TEST_F(SteamActivityTest, LoadsOnlyImageWallpapersOfReasonableSize) {
  write(root / "a", "\xFF\xD8\xFF\xE0 data");
  write(root / "b", "not an image");
  auto image = load_wallpaper(root / "a");
  ASSERT_TRUE(image.has_value());
  EXPECT_EQ(image->type, "image/jpeg");
  EXPECT_EQ(image->data, "\xFF\xD8\xFF\xE0 data");
  EXPECT_FALSE(load_wallpaper(root / "b").has_value());
  EXPECT_FALSE(load_wallpaper(root / "missing").has_value());
  write(root / "c", "BM" + std::string(max_wallpaper_bytes, 'x'));
  EXPECT_FALSE(load_wallpaper(root / "c").has_value());
}

TEST_F(SteamActivityTest, LoadsArtByExtensionAndSize) {
  write(root / "a.png", "png data");
  write(root / "b.txt", "text");
  write(root / "c.jpg", std::string(max_art_bytes + 1, 'x'));
  auto image = load_art(root / "a.png");
  ASSERT_TRUE(image.has_value());
  EXPECT_EQ(image->type, "image/png");
  EXPECT_EQ(image->data, "png data");
  EXPECT_FALSE(load_art(root / "b.txt").has_value());
  EXPECT_FALSE(load_art(root / "c.jpg").has_value());
}

TEST_F(SteamActivityTest, RefusesArtWhenTheCacheFolderIsALink) {
  fs::create_directories(root / "elsewhere");
  write(root / "elsewhere" / "600_library_hero.jpg", "x");
  fs::create_directories(root / "appcache");
  std::error_code ec;
  fs::create_directory_symlink(root / "elsewhere", root / "appcache" / "librarycache", ec);
  if (!ec) {
    EXPECT_EQ(find_art(root, 600, art_kind::hero), std::nullopt);
  }
}

TEST_F(SteamActivityTest, RefusesArtWhenAppcacheIsALink) {
  fs::create_directories(root / "elsewhere" / "librarycache");
  write(root / "elsewhere" / "librarycache" / "700_library_hero.jpg", "x");
  std::error_code ec;
  fs::create_directory_symlink(root / "elsewhere", root / "appcache", ec);
  if (!ec) {
    EXPECT_EQ(find_art(root, 700, art_kind::hero), std::nullopt);
  }
}
