#include "qt_library_info_cpp.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

bool isDir(const std::string &p)
{
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path(p), ec);
}

std::string ensureNoTrailingSlash(const std::string &p)
{
    if (!p.empty() && p.back() == '/') {
        return p.substr(0, p.size() - 1);
    }
    return p;
}

} // namespace

std::string QtLibraryInfoCpp::translationsPath()
{
    const char *env = std::getenv("QT_INSTALL_TRANSLATIONS");
    if (env && *env) {
        const std::string p = ensureNoTrailingSlash(std::string(env));
        if (isDir(p)) {
            return p;
        }
    }

    const std::vector<std::string> candidates {
        "/usr/share/qt6/translations",
        "/usr/share/qt/translations",
        "/usr/lib/qt/translations",
        "/usr/lib/x86_64-linux-gnu/qt6/translations",
        "/usr/lib64/qt6/translations",
    };

    for (const std::string &pRaw : candidates) {
        const std::string p = ensureNoTrailingSlash(pRaw);
        if (isDir(p)) {
            return p;
        }
    }

    return std::string();
}
