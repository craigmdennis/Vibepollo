/**
 * @file src/steam_activity_host.cpp
 * @brief Finds Steam and the signed-in user's values on this host, and caches the activity response.
 *
 * Every read of a path the user can influence goes through Vibepollo's Steam discovery scope
 * (platf::steam::windows_discovery), the same as Vibepollo's own Steam library: it runs under the
 * signed-in console user's access checks, and nothing is read while nobody is signed in.
 */
#include "steam_activity.h"

// standard includes
#include <chrono>
#include <mutex>

// local includes
#include "logging.h"

#ifdef _WIN32
  // misc.h includes Boost.Asio, which needs winsock2.h ahead of windows.h.
  #include "platform/windows/misc.h"
  #include "platform/windows/steam_discovery.h"

  // platform includes
  #include <ShlObj.h>
  #include <Windows.h>
#endif

using namespace std::literals;
namespace fs = std::filesystem;

namespace steam_activity {
#ifdef _WIN32
  namespace {
    namespace discovery = platf::steam::windows_discovery;

    std::optional<DWORD> reg_dword(HKEY hive, const std::wstring &sub, const wchar_t *name) {
      DWORD value = 0;
      DWORD size = sizeof(value);
      if (RegGetValueW(hive, sub.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return std::nullopt;
      }
      return value;
    }

    // Call inside a discovery scope.
    std::optional<fs::path> find_root() {
      std::error_code ec;
      for (const auto &root : platf::steam::default_library_roots()) {
        if (fs::is_directory(root / "steamapps", ec)) {
          return root;
        }
      }
      return std::nullopt;
    }

    // Call inside a discovery scope.
    std::optional<uint32_t> last_signed_in_account(const fs::path &root) {
      auto text = read_limited(root / "config" / "loginusers.vdf", 4ULL << 20);
      return text ? most_recent_account(platf::steam::parse_vdf(*text)) : std::nullopt;
    }

    /// Windows keeps a copy of the current wallpaper here, whichever file the user picked.
    fs::path transcoded_wallpaper() {
      HANDLE token = platf::retrieve_users_token(false);
      if (!token) {
        return {};
      }
      PWSTR roaming = nullptr;
      fs::path path;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, token, &roaming))) {
        path = fs::path(roaming) / L"Microsoft" / L"Windows" / L"Themes" / L"TranscodedWallpaper";
      }
      CoTaskMemFree(roaming);
      CloseHandle(token);
      return path;
    }
  }  // namespace

  std::optional<fs::path> locate_root() {
    return discovery::read_user_source(find_root);
  }

  std::optional<image_t> current_wallpaper() {
    auto file = transcoded_wallpaper();
    if (file.empty()) {
      return std::nullopt;
    }
    return discovery::read_user_source([&] {
      return load_wallpaper(file);
    });
  }

  std::optional<image_t> current_art(uint32_t app_id, art_kind kind) {
    return discovery::read_user_source([&]() -> std::optional<image_t> {
      auto root = find_root();
      auto file = root ? find_art(*root, app_id, kind) : std::nullopt;
      return file ? load_art(*file) : std::nullopt;
    });
  }

  namespace {
    // Called only from current_activity(), which holds its mutex.
    nlohmann::json build_activity() {
      // While nobody is signed in, the last result is served and nothing is read.
      static nlohmann::json last_good;

      discovery::discovery_scope scope;
      const auto &sid = scope.user_hive();
      if (!sid) {
        return last_good.is_null() ? json_no_steam() : last_good;
      }
      auto root = find_root();
      if (!root) {
        return json_no_steam();
      }

      uint32_t account = reg_dword(HKEY_USERS, *sid + L"\\Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser").value_or(0);
      if (account == 0) {
        // No Steam session in the user's hive, so use the last account that signed in.
        account = last_signed_in_account(*root).value_or(0);
      }
      uint32_t running = reg_dword(HKEY_USERS, *sid + L"\\Software\\Valve\\Steam", L"RunningAppID").value_or(0);

      auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      last_good = to_json(make_activity(platf::steam::discover_catalog({*root}), account, running, now));
      return last_good;
    }
  }  // namespace
#else
  std::optional<fs::path> locate_root() {
    return std::nullopt;
  }

  std::optional<image_t> current_wallpaper() {
    return std::nullopt;
  }

  std::optional<image_t> current_art(uint32_t, art_kind) {
    return std::nullopt;
  }

  namespace {
    nlohmann::json build_activity() {
      return json_no_steam();
    }
  }  // namespace
#endif

  nlohmann::json current_activity() {
    using clock = std::chrono::steady_clock;
    static std::mutex mutex;
    static nlohmann::json cached;
    static clock::time_point expires;

    std::lock_guard lock(mutex);
    auto now = clock::now();
    if (!cached.is_null() && now < expires) {
      return cached;
    }
    try {
      cached = build_activity();
    } catch (const std::exception &e) {
      BOOST_LOG(debug) << "Steam activity unavailable: "sv << e.what();
      cached = json_no_steam();
    }
    expires = now + std::chrono::seconds(30);
    return cached;
  }
}  // namespace steam_activity
