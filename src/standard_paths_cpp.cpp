#include "standard_paths_cpp.h"

#include <cstdlib>
#include <sys/stat.h>
#include <vector>

namespace {

std::string getenvNonEmpty(const char *name)
{
    const char *v = std::getenv(name);
    if (v && *v) {
        return std::string(v);
    }
    return std::string();
}

bool isAbsolutePath(const std::string &p)
{
    return !p.empty() && p.front() == '/';
}

std::vector<std::string> splitColonList(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ':') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(cur);
    return out;
}

bool isRegularFile(const std::string &p)
{
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

std::string ensureNoTrailingSlash(const std::string &p)
{
    if (!p.empty() && p.back() == '/') {
        return p.substr(0, p.size() - 1);
    }
    return p;
}

} // namespace

std::string StandardPathsCpp::writableConfigLocation()
{
    // Mirror the common XDG behavior used by Qt on Unix:
    // - if XDG_CONFIG_HOME is set and non-empty, use it
    // - otherwise, use $HOME/.config
    // - if HOME is not set, return empty string
    const std::string xdg = getenvNonEmpty("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        // Qt behavior (see Qt6 qsettings.cpp: make_user_path_without_qstandard_paths()):
        // - if XDG_CONFIG_HOME is absolute, use it
        // - if it's relative, QStandardPaths ignores it and falls back to $HOME/.config
        if (!xdg.empty() && xdg.front() == '/') {
            return xdg;
        }
    }

    const std::string home = getenvNonEmpty("HOME");
    if (home.empty()) {
        return std::string();
    }

    if (!home.empty() && home.back() == '/') {
        return home + ".config";
    }
    return home + "/.config";
}

std::string StandardPathsCpp::locateApplicationsFile(const std::string &fileName)
{
    if (fileName.empty()) {
        return std::string();
    }

    // Equivalent to: QStandardPaths::locate(ApplicationsLocation, fileName, LocateFile)
    // for Unix: searches XDG data locations and checks for a regular file.

    std::vector<std::string> roots;

    const std::string xdgDataHome = getenvNonEmpty("XDG_DATA_HOME");
    if (!xdgDataHome.empty() && isAbsolutePath(xdgDataHome)) {
        roots.push_back(ensureNoTrailingSlash(xdgDataHome));
    } else {
        const std::string home = getenvNonEmpty("HOME");
        if (!home.empty() && isAbsolutePath(home)) {
            roots.push_back(ensureNoTrailingSlash(home) + "/.local/share");
        }
    }

    std::string xdgDataDirs = getenvNonEmpty("XDG_DATA_DIRS");
    if (xdgDataDirs.empty()) {
        xdgDataDirs = "/usr/local/share:/usr/share";
    }
    for (const std::string &raw : splitColonList(xdgDataDirs)) {
        if (!raw.empty() && isAbsolutePath(raw)) {
            roots.push_back(ensureNoTrailingSlash(raw));
        }
    }

    for (const std::string &root : roots) {
        const std::string candidate = root + "/applications/" + fileName;
        if (isRegularFile(candidate)) {
            return candidate;
        }
    }

    return std::string();
}
