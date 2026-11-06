/**
 * @file src/steam_activity_host.cpp
 * @brief Finds Steam and the signed-in user's values on this host, and caches the activity response.
 *
 * Steam's files are read the way Vibepollo's Steam library reads them: by the host process. The
 * signed-in console user's token is used only to name that user's registry hive (HKEY_USERS\<SID>)
 * and profile folder; nothing runs as the user.
 */
#include "steam_activity.h"

// standard includes
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>

// local includes
#include "logging.h"

#ifdef _WIN32
  // misc.h includes Boost.Asio, which needs winsock2.h ahead of windows.h.
  #include "platform/windows/misc.h"

  // platform includes
  #include <sddl.h>
  #include <ShlObj.h>
  #include <Windows.h>
#endif

using namespace std::literals;
namespace fs = std::filesystem;

namespace steam_activity {
#ifdef _WIN32
  namespace {
    std::optional<DWORD> reg_dword(HKEY hive, const std::wstring &sub, const wchar_t *name) {
      DWORD value = 0;
      DWORD size = sizeof(value);
      if (RegGetValueW(hive, sub.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return std::nullopt;
      }
      return value;
    }

    /**
     * @brief The signed-in console user's token. Read only for the user's SID and profile folder.
     */
    class console_user_t {
    public:
      console_user_t():
          token_(platf::retrieve_users_token(false)) {
      }

      ~console_user_t() {
        if (token_) {
          CloseHandle(token_);
        }
      }

      console_user_t(const console_user_t &) = delete;
      console_user_t &operator=(const console_user_t &) = delete;

      explicit operator bool() const {
        return token_ != nullptr;
      }

      /// The user's SID as a string, which names their loaded hive under HKEY_USERS.
      std::wstring sid() const {
        DWORD size = 0;
        GetTokenInformation(token_, TokenUser, nullptr, 0, &size);
        if (size == 0) {
          return {};
        }
        std::vector<std::uint8_t> buffer(size);
        if (!GetTokenInformation(token_, TokenUser, buffer.data(), size, &size)) {
          return {};
        }
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text)) {
          return {};
        }
        std::wstring out {text};
        LocalFree(text);
        return out;
      }

      /// Windows keeps a copy of the current wallpaper here, whichever file the user picked.
      fs::path transcoded_wallpaper() const {
        PWSTR roaming = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, token_, &roaming))) {
          return {};
        }
        fs::path path = fs::path(roaming) / L"Microsoft" / L"Windows" / L"Themes" / L"TranscodedWallpaper";
        CoTaskMemFree(roaming);
        return path;
      }

    private:
      HANDLE token_;
    };

    struct user_values_t {
      uint32_t account_id = 0;
      uint32_t running_app_id = 0;
    };

    user_values_t read_user_values(const console_user_t &user) {
      user_values_t values;
      const auto sid = user.sid();
      if (sid.empty()) {
        return values;
      }
      values.account_id = reg_dword(HKEY_USERS, sid + L"\\Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser").value_or(0);
      values.running_app_id = reg_dword(HKEY_USERS, sid + L"\\Software\\Valve\\Steam", L"RunningAppID").value_or(0);
      return values;
    }

    std::optional<uint32_t> last_signed_in_account(const fs::path &root) {
      constexpr std::uintmax_t max_bytes = 4ULL << 20;
      std::error_code ec;
      const auto file = root / "config" / "loginusers.vdf";
      if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) > max_bytes || ec) {
        return std::nullopt;
      }
      std::ifstream input(file, std::ios::binary);
      std::stringstream buffer;
      buffer << input.rdbuf();
      return most_recent_account(platf::steam::parse_vdf(buffer.str()));
    }
  }  // namespace

  std::optional<fs::path> locate_root() {
    std::error_code ec;
    for (const auto &root : platf::steam::default_library_roots()) {
      if (fs::is_directory(root / "steamapps", ec)) {
        return root;
      }
    }
    return std::nullopt;
  }

  std::optional<image_t> current_wallpaper() {
    console_user_t user;
    if (!user) {
      return std::nullopt;
    }
    return load_wallpaper(user.transcoded_wallpaper());
  }

  std::optional<image_t> current_art(uint32_t app_id, art_kind kind) {
    auto root = locate_root();
    if (!root) {
      return std::nullopt;
    }
    auto file = find_art(*root, app_id, kind);
    return file ? load_art(*file) : std::nullopt;
  }

  namespace {
    nlohmann::json build_activity() {
      auto root = locate_root();
      if (!root) {
        return json_no_steam();
      }

      user_values_t values;
      if (console_user_t user; user) {
        values = read_user_values(user);
      }
      if (values.account_id == 0) {
        // No Steam session in the user's hive, so use the last account that signed in.
        values.account_id = last_signed_in_account(*root).value_or(0);
      }

      auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      return to_json(make_activity(platf::steam::discover_catalog({*root}), values.account_id, values.running_app_id, now));
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
