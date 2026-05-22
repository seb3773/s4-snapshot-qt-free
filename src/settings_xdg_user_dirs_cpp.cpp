#include "settings_xdg_user_dirs_cpp.h"

#include <unordered_map>

#include "command_runner.h"
#include "string_cpp.h"

namespace {

std::vector<std::string> split_lines_skip_empty(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch == '\n') {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }

    return out;
}

std::string trimmed_like_qt(const std::string &s)
{
    return StringCpp::trimmedLikeQStringUtf8(s);
}

bool starts_with_char(const std::string &s, char ch)
{
    return !s.empty() && s.front() == ch;
}

std::string remove_prefix_slash_once(const std::string &s)
{
    if (!s.empty() && s.front() == '/') {
        return s.substr(1);
    }
    return s;
}

std::string last_section_slash_minus1_like_qstring(const std::string &s)
{
    const size_t pos = s.find_last_of('/');
    if (pos == std::string::npos) {
        return s;
    }
    if (pos + 1 >= s.size()) {
        return std::string();
    }
    return s.substr(pos + 1);
}

void debug_line(const SettingsXdgUserDirsCpp::Callbacks &cb, const std::string &line)
{
    if (cb.debugLine) {
        cb.debugLine(cb.ctx, line);
    }
}

} // namespace

std::string SettingsXdgUserDirsCpp::getXdgUserDirsLikeSettingsQt(const std::vector<std::string> &users,
                                                                const std::string &folder,
                                                                Callbacks callbacks)
{
    std::vector<std::string> resultParts;
    resultParts.reserve(18);

    const std::unordered_map<std::string, std::string> englishDirs = {
        {"DOCUMENTS", "Documents"}, {"DOWNLOAD", "Downloads"}, {"DESKTOP", "Desktop"},
        {"MUSIC", "Music"},         {"PICTURES", "Pictures"},  {"VIDEOS", "Videos"},
    };

    for (const std::string &user : users) {
        const std::string raw = CommandRunner::getOutAsRoot(
            "runuser",
            {"-u", user, "--", "/usr/bin/xdg-user-dir", folder},
            CommandRunner::QuietMode::Yes);
        debug_line(callbacks, std::string("runuser out='") + raw + "'");

        std::string dir;
        const std::vector<std::string> lines = split_lines_skip_empty(raw);
        for (const std::string &line : lines) {
            const std::string candidate = trimmed_like_qt(line);
            if (starts_with_char(candidate, '/')) {
                dir = candidate;
                break;
            }
        }

        const auto itEng = englishDirs.find(folder);
        const std::string englishName = (itEng == englishDirs.end()) ? std::string() : itEng->second;

        const std::string userHome = std::string("/home/") + user;
        if (!dir.empty() && englishName != last_section_slash_minus1_like_qstring(dir) && dir != userHome
            && dir != (userHome + '/')) {
            dir = remove_prefix_slash_once(dir);
            std::string exclusion;
            if (folder == "DESKTOP") {
                exclusion = "/!(minstall.desktop)";
            } else {
                exclusion = "/*\" \"" + dir + "/.*";
            }
            dir.append(exclusion);
            resultParts.push_back(dir);
        }
    }

    std::string joined;
    for (size_t i = 0; i < resultParts.size(); ++i) {
        if (i != 0) {
            joined.append("\" \"");
        }
        joined.append(resultParts[i]);
    }

    if (joined.empty()) {
        return std::string();
    }
    return std::string("\" \"") + joined;
}

std::string SettingsXdgUserDirsCpp::getXdgUserDirsLikeSettingsQt(const std::vector<std::string> &users,
                                                                const std::string &folder)
{
    return getXdgUserDirsLikeSettingsQt(users, folder, Callbacks{});
}
