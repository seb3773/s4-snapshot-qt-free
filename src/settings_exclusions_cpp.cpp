#include "settings_exclusions_cpp.h"

#include <cctype>
#include <cstdlib>

#include "file_cpp.h"
#include "settings_xdg_user_dirs_cpp.h"

namespace {

static constexpr std::uint32_t EXCL_DESKTOP = (1u << 0);
static constexpr std::uint32_t EXCL_DOCUMENTS = (1u << 1);
static constexpr std::uint32_t EXCL_DOWNLOADS = (1u << 2);
static constexpr std::uint32_t EXCL_FLATPAKS = (1u << 3);
static constexpr std::uint32_t EXCL_MUSIC = (1u << 4);
static constexpr std::uint32_t EXCL_NETWORKS = (1u << 5);
static constexpr std::uint32_t EXCL_PICTURES = (1u << 6);
static constexpr std::uint32_t EXCL_STEAM = (1u << 7);
static constexpr std::uint32_t EXCL_VIDEOS = (1u << 8);
static constexpr std::uint32_t EXCL_VIRTUALBOX = (1u << 9);

static std::string etc_base_dir()
{
#ifdef UNIT_TESTS
    const char *p = std::getenv("S4SNAPSHOT_UT_ETC_DIR");
    if (p != nullptr && *p != '\0') {
        return std::string(p);
    }
#endif
    return std::string("/etc");
}

static std::string trim_like_qt_for_fstab_line(std::string s)
{
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

static void add_remove_exclusion_like_settings_qt(SettingsCpp &settings, bool add, std::string exclusion)
{
    if (!exclusion.empty() && exclusion[0] == '/') {
        exclusion.erase(exclusion.begin());
    }

    const std::string token = std::string("\"") + exclusion + "\" ";

    if (add) {
        settings.sessionExcludes += token;
        return;
    }

    std::size_t pos = 0;
    while ((pos = settings.sessionExcludes.find(token, pos)) != std::string::npos) {
        settings.sessionExcludes.erase(pos, token.size());
    }
}

static void exclude_swap_file_like_settings_qt(SettingsCpp &settings, const SettingsExclusionsCpp::Callbacks &callbacks)
{
    FileCpp file(etc_base_dir() + "/fstab");
    if (!file.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
        if (callbacks.onWarning) {
            callbacks.onWarning(std::string("Failed to open /etc/fstab"));
        }
        return;
    }

    const std::vector<std::uint8_t> content = file.readAll();
    file.close();

    std::string all(reinterpret_cast<const char *>(content.data()), content.size());

    std::size_t i = 0;
    while (i <= all.size()) {
        const std::size_t nl = all.find('\n', i);
        const bool hasNl = (nl != std::string::npos);
        const std::size_t end = hasNl ? nl : all.size();

        std::string line = all.substr(i, end - i);
        line = trim_like_qt_for_fstab_line(std::move(line));

        if (!line.empty() && line[0] == '/' && line.rfind("/dev/", 0) != 0) {
            // Split by whitespace, collapsing runs.
            std::vector<std::string> parts;
            std::string cur;
            cur.reserve(line.size());
            for (std::size_t j = 0; j < line.size(); ++j) {
                const unsigned char ch = static_cast<unsigned char>(line[j]);
                if (std::isspace(ch)) {
                    parts.push_back(cur);
                    cur.clear();
                    while (j + 1 < line.size() && std::isspace(static_cast<unsigned char>(line[j + 1]))) {
                        ++j;
                    }
                } else {
                    cur.push_back(static_cast<char>(ch));
                }
            }
            parts.push_back(cur);

            if (parts.size() > 3 && parts[2] == "swap") {
                std::string p0 = parts[0];
                if (!p0.empty() && p0[0] == '/') {
                    p0.erase(p0.begin());
                }
                add_remove_exclusion_like_settings_qt(settings, true, p0);
            }
        }

        if (!hasNl) {
            break;
        }
        i = nl + 1;
    }
}

static void exclude_swap_file_like_settings_qt(SettingsCpp &settings)
{
    FileCpp file(etc_base_dir() + "/fstab");
    if (!file.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
        return;
    }

    const std::vector<std::uint8_t> content = file.readAll();
    file.close();

    std::string all(reinterpret_cast<const char *>(content.data()), content.size());

    std::size_t i = 0;
    while (i <= all.size()) {
        const std::size_t nl = all.find('\n', i);
        const bool hasNl = (nl != std::string::npos);
        const std::size_t end = hasNl ? nl : all.size();

        std::string line = all.substr(i, end - i);
        line = trim_like_qt_for_fstab_line(std::move(line));

        if (!line.empty() && line[0] == '/' && line.rfind("/dev/", 0) != 0) {
            // Split by whitespace, collapsing runs.
            std::vector<std::string> parts;
            std::string cur;
            cur.reserve(line.size());
            for (std::size_t j = 0; j < line.size(); ++j) {
                const unsigned char ch = static_cast<unsigned char>(line[j]);
                if (std::isspace(ch)) {
                    parts.push_back(cur);
                    cur.clear();
                    while (j + 1 < line.size() && std::isspace(static_cast<unsigned char>(line[j + 1]))) {
                        ++j;
                    }
                } else {
                    cur.push_back(static_cast<char>(ch));
                }
            }
            parts.push_back(cur);

            if (parts.size() > 3 && parts[2] == "swap") {
                std::string p0 = parts[0];
                if (!p0.empty() && p0[0] == '/') {
                    p0.erase(p0.begin());
                }
                add_remove_exclusion_like_settings_qt(settings, true, p0);
            }
        }

        if (!hasNl) {
            break;
        }
        i = nl + 1;
    }
}

} // namespace

void SettingsExclusionsCpp::otherExclusionsLikeSettingsQt(SettingsCpp &settings)
{
    // Add exclusions snapshot and work dirs
    add_remove_exclusion_like_settings_qt(settings, true, settings.snapshotDir);
    add_remove_exclusion_like_settings_qt(settings, true, settings.workDir);

    if (settings.resetAccounts) {
        add_remove_exclusion_like_settings_qt(settings, true, etc_base_dir() + "/minstall.conf");

        // Exclude /etc/localtime if link and timezone not America/New_York
        FileCpp timezoneFile(etc_base_dir() + "/timezone");
        if (FileCpp::isSymLink(etc_base_dir() + "/localtime")) {
            if (timezoneFile.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
                std::string lineBytes = timezoneFile.readLine();
                timezoneFile.close();

                if (!lineBytes.empty() && lineBytes.back() == '\n') {
                    lineBytes.pop_back();
                    if (!lineBytes.empty() && lineBytes.back() == '\r') {
                        lineBytes.pop_back();
                    }
                }

                if (lineBytes != "America/New_York") {
                    add_remove_exclusion_like_settings_qt(settings, true, etc_base_dir() + "/localtime");
                }
            }
        }
    }

    exclude_swap_file_like_settings_qt(settings);
}

void SettingsExclusionsCpp::otherExclusionsLikeSettingsQt(SettingsCpp &settings, const Callbacks &callbacks)
{
    // Add exclusions snapshot and work dirs
    add_remove_exclusion_like_settings_qt(settings, true, settings.snapshotDir);
    add_remove_exclusion_like_settings_qt(settings, true, settings.workDir);

    if (settings.resetAccounts) {
        add_remove_exclusion_like_settings_qt(settings, true, etc_base_dir() + "/minstall.conf");

        // Exclude /etc/localtime if link and timezone not America/New_York
        FileCpp timezoneFile(etc_base_dir() + "/timezone");
        if (FileCpp::isSymLink(etc_base_dir() + "/localtime")) {
            if (timezoneFile.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
                std::string lineBytes = timezoneFile.readLine();
                timezoneFile.close();

                if (!lineBytes.empty() && lineBytes.back() == '\n') {
                    lineBytes.pop_back();
                    if (!lineBytes.empty() && lineBytes.back() == '\r') {
                        lineBytes.pop_back();
                    }
                }

                if (lineBytes != "America/New_York") {
                    add_remove_exclusion_like_settings_qt(settings, true, etc_base_dir() + "/localtime");
                }
            }
        }
    }

    exclude_swap_file_like_settings_qt(settings, callbacks);
}

void SettingsExclusionsCpp::excludeSwapFileLikeSettingsQt(SettingsCpp &settings, const Callbacks &callbacks)
{
    exclude_swap_file_like_settings_qt(settings, callbacks);
}

void SettingsExclusionsCpp::excludeDesktopLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_DESKTOP;
    }
    const std::string xdg = SettingsXdgUserDirsCpp::getXdgUserDirsLikeSettingsQt(users, "DESKTOP");
    std::string exclusion = std::string("/home/*/Desktop/!(minstall.desktop)") + xdg;
    add_remove_exclusion_like_settings_qt(settings, exclude, std::move(exclusion));
}

static void exclude_two_tokens_with_xdg(SettingsCpp &settings,
                                       bool exclude,
                                       const std::vector<std::string> &users,
                                       const char *folder,
                                       const char *xdgName)
{
    const std::string base(folder);
    const std::string xdg = SettingsXdgUserDirsCpp::getXdgUserDirsLikeSettingsQt(users, xdgName);
    std::string exclusion = base + "*\" \"" + base + ".*" + xdg;
    add_remove_exclusion_like_settings_qt(settings, exclude, std::move(exclusion));
}

void SettingsExclusionsCpp::excludeDocumentsLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_DOCUMENTS;
    }
    exclude_two_tokens_with_xdg(settings, exclude, users, "home/*/Documents/", "DOCUMENTS");
}

void SettingsExclusionsCpp::excludeDownloadsLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_DOWNLOADS;
    }
    exclude_two_tokens_with_xdg(settings, exclude, users, "home/*/Downloads/", "DOWNLOAD");
}

void SettingsExclusionsCpp::excludeMusicLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_MUSIC;
    }
    exclude_two_tokens_with_xdg(settings, exclude, users, "home/*/Music/", "MUSIC");
}

void SettingsExclusionsCpp::excludePicturesLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_PICTURES;
    }
    exclude_two_tokens_with_xdg(settings, exclude, users, "home/*/Pictures/", "PICTURES");
}

void SettingsExclusionsCpp::excludeVideosLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_VIDEOS;
    }
    exclude_two_tokens_with_xdg(settings, exclude, users, "home/*/Videos/", "VIDEOS");
}

void SettingsExclusionsCpp::excludeFlatpaksLikeSettingsQt(SettingsCpp &settings, bool exclude)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_FLATPAKS;
    }
    const std::string exclusion =
        std::string("home/*/.local/share/flatpak/*\" \"home/*/.local/share/flatpak/.*\" ")
        + "\"var/lib/flatpak/*\" \"var/lib/flatpak/.*\" "
        + "\"home/*/.var/app/*\" \"home/*/.var/app/.*";
    add_remove_exclusion_like_settings_qt(settings, exclude, exclusion);
}

void SettingsExclusionsCpp::excludeNetworksLikeSettingsQt(SettingsCpp &settings, bool exclude)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_NETWORKS;
    }
    add_remove_exclusion_like_settings_qt(settings, exclude, std::string("/etc/NetworkManager/system-connections/*"));
    add_remove_exclusion_like_settings_qt(settings, exclude, std::string("/etc/wicd/*"));
    add_remove_exclusion_like_settings_qt(settings, exclude, std::string("/var/lib/connman/*"));
}

void SettingsExclusionsCpp::excludeSteamLikeSettingsQt(SettingsCpp &settings, bool exclude)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_STEAM;
    }
    add_remove_exclusion_like_settings_qt(settings, exclude, std::string("home/*/.steam"));
    add_remove_exclusion_like_settings_qt(settings, exclude, std::string("home/*/.local/share/Steam"));
}

void SettingsExclusionsCpp::excludeVirtualBoxLikeSettingsQt(SettingsCpp &settings, bool exclude)
{
    if (exclude) {
        settings.exclusionsMask |= EXCL_VIRTUALBOX;
    }
    add_remove_exclusion_like_settings_qt(settings, exclude, std::string("home/*/VirtualBox VMs"));
}

void SettingsExclusionsCpp::excludeAllLikeSettingsQt(SettingsCpp &settings, const std::vector<std::string> &users)
{
    excludeDesktopLikeSettingsQt(settings, true, users);
    excludeDocumentsLikeSettingsQt(settings, true, users);
    excludeDownloadsLikeSettingsQt(settings, true, users);
    excludeFlatpaksLikeSettingsQt(settings, true);
    excludeMusicLikeSettingsQt(settings, true, users);
    excludeNetworksLikeSettingsQt(settings, true);
    excludePicturesLikeSettingsQt(settings, true, users);
    excludeSteamLikeSettingsQt(settings, true);
    excludeVideosLikeSettingsQt(settings, true, users);
    excludeVirtualBoxLikeSettingsQt(settings, true);
}
