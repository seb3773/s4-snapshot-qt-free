#include "settings_debianver_cpp.h"

#include "file_cpp.h"
#include "string_cpp.h"

#include <cerrno>
#include <cstdlib>

namespace {

static void cb_critical(const SettingsDebianVerNumCpp::Callbacks &cb, const std::string &text)
{
    if (cb.critical) {
        cb.critical(text);
    }
}

static std::string strip_crlf(const std::string &s)
{
    std::string out = s;
    if (!out.empty() && out.back() == '\n') {
        out.pop_back();
        if (!out.empty() && out.back() == '\r') {
            out.pop_back();
        }
    }
    return out;
}

static bool parse_int_like_qstring_toInt_base10(const std::string &utf8, int *out)
{
    if (!out) {
        return false;
    }

    const std::string t = StringCpp::trimmedLikeQStringUtf8(utf8);
    if (t.empty()) {
        *out = 0;
        return false;
    }

    const char *begin = t.c_str();
    char *end = nullptr;
    errno = 0;
    const long v = std::strtol(begin, &end, 10);
    if (errno != 0) {
        *out = 0;
        return false;
    }
    if (end == begin) {
        *out = 0;
        return false;
    }
    // QString::toInt sets ok=false if there are trailing non-space chars.
    if (end && *end != '\0') {
        *out = 0;
        return false;
    }

    *out = static_cast<int>(v);
    return true;
}

} // namespace

int SettingsDebianVerNumCpp::parseDebianVersionLineLikeSettingsQt(const std::string &lineUtf8,
                                                                 const SettingsDebianVerNumCpp::Callbacks &cb)
{
    const std::string line = strip_crlf(lineUtf8);

    // Qt: list = line.split('.'); first = list.at(0)
    std::string first = line;
    const std::size_t dot = first.find('.');
    if (dot != std::string::npos) {
        first = first.substr(0, dot);
    }

    int ver = 0;
    const bool ok = parse_int_like_qstring_toInt_base10(first, &ver);
    if (ok) {
        return ver;
    }

    // Qt: verName = list.at(0).split('/').at(0)
    std::string verName = first;
    const std::size_t slash = verName.find('/');
    if (slash != std::string::npos) {
        verName = verName.substr(0, slash);
    }

    if (verName == "bullseye") {
        return 11;
    }
    if (verName == "bookworm") {
        return 12;
    }
    if (verName == "trixie") {
        return 13;
    }
    if (verName == "forky") {
        return 14;
    }
    if (verName == "duke") {
        return 15;
    }

    cb_critical(cb, std::string("Unknown Debian version: ") + std::to_string(ver) + " Assumes Bullseye\n");
    return 11;
}

int SettingsDebianVerNumCpp::getDebianVerNumLikeSettingsQt(const SettingsDebianVerNumCpp::Callbacks &cb)
{
    FileCpp file(std::string("/etc/debian_version"));
    if (!file.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
        cb_critical(cb,
                    std::string("Could not open /etc/debian_version: ") + file.errorString()
                        + " Assumes Bullseye\n");
        return 11;
    }

    const std::string lineBytes = file.readLine();
    file.close();

    return parseDebianVersionLineLikeSettingsQt(lineBytes, cb);
}
