#include "qsettings_cpp.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <vector>

// Forward declarations for helpers used before their definitions.
std::string iniUnescapedKey(const std::string &keyRaw);
std::string iniUnescapedString(const std::string &valueRaw);
std::string qtStringToVariantToString(const std::string &s);
bool iequals(const std::string &a, const std::string &b);

#ifdef UNIT_TESTS
static const QSettingsCpp::Hooks *g_hooks = nullptr;

void QSettingsCpp::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}
#endif

namespace
{
bool isSpace(unsigned char ch)
{
    return std::isspace(ch) != 0;
}

std::string trimCopy(const std::string &s)
{
    std::size_t b = 0;
    while (b < s.size() && isSpace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && isSpace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

bool isHexDigit(unsigned char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool isOctDigit(unsigned char ch)
{
    return ch >= '0' && ch <= '7';
}

unsigned hexValue(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return static_cast<unsigned>(ch - '0');
    if (ch >= 'a' && ch <= 'f') return static_cast<unsigned>(10 + (ch - 'a'));
    return static_cast<unsigned>(10 + (ch - 'A'));
}

unsigned octValue(unsigned char ch)
{
    return static_cast<unsigned>(ch - '0');
}

void appendUtf8(std::string &out, unsigned codepoint)
{
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::vector<std::string> qtNativeUserCandidateFilesFromBaseDir(const std::string &configDir,
                                                               const std::string &organization,
                                                               const std::string &application)
{
    std::string base = configDir;
    if (!base.empty() && base.back() != '/') {
        base.push_back('/');
    }

    std::vector<std::string> out;
    out.reserve(2);
    // Mirror Qt's order for user scope when looking up keys:
    // 1) <userPath>/<org>/<app>.conf
    // 2) <userPath>/<org>.conf
    out.push_back(base + organization + std::string("/") + application + std::string(".conf"));
    out.push_back(base + organization + std::string(".conf"));
    return out;
}

bool nativeGeneralTryReadAllKeyValues(const std::string &filePath, std::vector<std::string> *keysOut,
                                      std::map<std::string, std::string> *kvOut)
{
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }

    std::string data;
    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (sz > 0) {
        data.resize(static_cast<std::size_t>(sz));
        f.read(&data[0], sz);
    }

    std::size_t pos = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        pos = 3;
    }

    bool inGeneral = true;
    std::size_t lineStart = pos;

    while (lineStart < data.size()) {
        std::size_t lineEnd = data.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = data.size();
        }
        std::string line = data.substr(lineStart, lineEnd - lineStart);

        std::size_t next = lineEnd;
        if (next < data.size() && data[next] == '\r') {
            ++next;
            if (next < data.size() && data[next] == '\n') {
                ++next;
            }
        } else if (next < data.size() && data[next] == '\n') {
            ++next;
        }

        std::string trimmed = trimCopy(line);
        if (!trimmed.empty() && trimmed[0] == '[') {
            const std::size_t rb = trimmed.find(']');
            std::string secName = (rb == std::string::npos)
                ? trimmed.substr(1)
                : trimmed.substr(1, rb - 1);
            secName = trimCopy(secName);
            if (iequals(secName, "general")) {
                inGeneral = true;
            } else if (!secName.empty() && secName[0] == '%' && iequals(secName.substr(1), "general")) {
                inGeneral = false;
            } else {
                inGeneral = false;
            }
            lineStart = next;
            continue;
        }

        if (inGeneral) {
            if (trimmed.empty() || trimmed[0] == ';') {
                lineStart = next;
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = trimCopy(line.substr(0, eq));
                std::string v = line.substr(eq + 1);
                k = trimCopy(k);
                const std::string uk = iniUnescapedKey(k);
                const std::string uv = qtStringToVariantToString(iniUnescapedString(v));
                if (keysOut) {
                    keysOut->push_back(uk);
                }
                if (kvOut) {
                    (*kvOut)[uk] = uv;
                }
            }
        }

        lineStart = next;
    }
    return true;
}

bool isAsciiLetterOrNumber(unsigned ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

bool isHexDigit(unsigned ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

char toHexUpper(unsigned v)
{
    return static_cast<char>((v < 10) ? ('0' + v) : ('A' + (v - 10)));
}

std::string iniEscapedKeyCpp(const std::string &k)
{
    std::string out;
    out.reserve(k.size() * 2);
    for (unsigned char uch : k) {
        const unsigned ch = uch;
        if (ch == '/') {
            out.push_back('\\');
        } else if (isAsciiLetterOrNumber(ch) || ch == '_' || ch == '-' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(toHexUpper((ch >> 4) & 0xF));
            out.push_back(toHexUpper(ch & 0xF));
        }
    }
    return out;
}

std::string iniEscapedStringCpp(const std::string &s)
{
    bool needsQuotes = false;
    bool escapeNextIfDigit = false;
    std::string out;
    out.reserve(s.size() * 2);

    for (unsigned char uch : s) {
        const unsigned ch = uch;
        if (ch == ';' || ch == ',' || ch == '=') {
            needsQuotes = true;
        }
        if (escapeNextIfDigit && isHexDigit(ch)) {
            out += "\\x";
            const char *hex = "0123456789abcdef";
            out.push_back(hex[(ch >> 4) & 0xF]);
            out.push_back(hex[ch & 0xF]);
            escapeNextIfDigit = false;
            continue;
        }
        escapeNextIfDigit = false;

        switch (ch) {
        case 0x00:
            out += "\\0";
            escapeNextIfDigit = true;
            break;
        case '\a':
            out += "\\a";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\v':
            out += "\\v";
            break;
        case '"':
        case '\\':
            out.push_back('\\');
            out.push_back(static_cast<char>(ch));
            break;
        default:
            if (ch <= 0x1F) {
                out += "\\x";
                const char *hex = "0123456789abcdef";
                out.push_back(hex[(ch >> 4) & 0xF]);
                out.push_back(hex[ch & 0xF]);
                escapeNextIfDigit = true;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }

    if (!out.empty() && (out.front() == ' ' || out.front() == '\t' || out.back() == ' ' || out.back() == '\t')) {
        needsQuotes = true;
    }
    if (needsQuotes) {
        out.insert(out.begin(), '"');
        out.push_back('"');
    }
    return out;
}
}

std::string iniUnescapedKey(const std::string &keyRaw)
{
    std::string out;
    out.reserve(keyRaw.size());

    std::size_t i = 0;
    while (i < keyRaw.size()) {
        const unsigned char ch = static_cast<unsigned char>(keyRaw[i]);
        if (ch == '\\') {
            out.push_back('/');
            ++i;
            continue;
        }
        if (ch != '%' || i + 1 >= keyRaw.size()) {
            out.push_back(static_cast<char>(ch));
            ++i;
            continue;
        }

        int numDigits = 2;
        std::size_t firstDigitPos = i + 1;
        if (keyRaw[firstDigitPos] == 'U') {
            ++firstDigitPos;
            numDigits = 4;
        }

        if (firstDigitPos + static_cast<std::size_t>(numDigits) > keyRaw.size()) {
            out.push_back('%');
            ++i;
            continue;
        }

        bool ok = true;
        unsigned v = 0;
        for (int d = 0; d < numDigits; ++d) {
            const unsigned char hd = static_cast<unsigned char>(keyRaw[firstDigitPos + static_cast<std::size_t>(d)]);
            if (!isHexDigit(hd)) {
                ok = false;
                break;
            }
            v = (v << 4) | hexValue(hd);
        }

        if (!ok) {
            out.push_back('%');
            ++i;
            continue;
        }

        appendUtf8(out, v);
        i = firstDigitPos + static_cast<std::size_t>(numDigits);
    }

    return out;
}

std::string qtStringToVariantToString(const std::string &s)
{
    if (!s.empty() && s.front() == '@') {
        if (s.size() >= 2 && s[1] == '@') {
            return s.substr(1);
        }
        if (s == "@Invalid()") {
            return std::string();
        }
        if (s.size() >= 11 && s.rfind("@ByteArray(", 0) == 0 && s.back() == ')') {
            return s.substr(11, s.size() - 12);
        }
        if (s.size() >= 8 && s.rfind("@String(", 0) == 0 && s.back() == ')') {
            return s.substr(8, s.size() - 9);
        }
    }
    return s;
}

std::string iniUnescapedString(const std::string &valueRaw)
{
    std::string out;
    out.reserve(valueRaw.size());

    static const struct EscapeMap {
        char key;
        char value;
    } escapeCodes[] = {
        {'a', '\a'},
        {'b', '\b'},
        {'f', '\f'},
        {'n', '\n'},
        {'r', '\r'},
        {'t', '\t'},
        {'v', '\v'},
        {'"', '"'},
        {'?', '?'},
        {'\'', '\''},
        {'\\', '\\'},
    };

    bool inQuotedString = false;
    bool currentValueIsQuoted = false;
    std::size_t i = 0;

    while (i < valueRaw.size() && (valueRaw[i] == ' ' || valueRaw[i] == '\t')) {
        ++i;
    }

    if (i < valueRaw.size() && valueRaw[i] == '"') {
        inQuotedString = true;
        currentValueIsQuoted = true;
        ++i;
    }

    const std::size_t chopLimit = out.size();
    while (i < valueRaw.size()) {
        const unsigned char ch = static_cast<unsigned char>(valueRaw[i]);

        if (!inQuotedString && ch == ';') {
            break;
        }

        if (inQuotedString && ch == '"') {
            ++i;
            inQuotedString = false;
            break;
        }

        if (ch == '\\') {
            ++i;
            if (i >= valueRaw.size()) {
                break;
            }
            const unsigned char e = static_cast<unsigned char>(valueRaw[i++]);

            bool handled = false;
            for (const auto &m : escapeCodes) {
                if (e == static_cast<unsigned char>(m.key)) {
                    out.push_back(m.value);
                    handled = true;
                    break;
                }
            }
            if (handled) {
                continue;
            }

            if (e == 'x') {
                unsigned v = 0;
                int digits = 0;
                while (i < valueRaw.size() && digits < 2 && isHexDigit(static_cast<unsigned char>(valueRaw[i]))) {
                    v = (v << 4) | hexValue(static_cast<unsigned char>(valueRaw[i]));
                    ++i;
                    ++digits;
                }
                if (digits > 0) {
                    out.push_back(static_cast<char>(v));
                    continue;
                }
                out.push_back('x');
                continue;
            }

            if (isOctDigit(e)) {
                unsigned v = octValue(e);
                int digits = 1;
                while (i < valueRaw.size() && digits < 3 && isOctDigit(static_cast<unsigned char>(valueRaw[i]))) {
                    v = (v << 3) | octValue(static_cast<unsigned char>(valueRaw[i]));
                    ++i;
                    ++digits;
                }
                out.push_back(static_cast<char>(v));
                continue;
            }

            if (e == '\n' || e == '\r') {
                // line continuation: skip newline(s)
                while (i < valueRaw.size() && (valueRaw[i] == ' ' || valueRaw[i] == '\t')) {
                    ++i;
                }
                continue;
            }

            out.push_back(static_cast<char>(e));
            continue;
        }

        out.push_back(static_cast<char>(ch));
        ++i;
    }

    if (!currentValueIsQuoted) {
        std::size_t n = out.size();
        while (n > chopLimit && (out[n - 1] == ' ' || out[n - 1] == '\t')) {
            --n;
        }
        out.resize(n);
    }

    return out;
}

bool iequals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

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

std::string ensureTrailingSlash(std::string p)
{
    if (p.empty() || p.back() != '/') {
        p.push_back('/');
    }
    return p;
}

std::string trimAsciiSpacesCopy(const std::string &s)
{
    std::size_t b = 0;
    while (b < s.size()) {
        const unsigned char ch = static_cast<unsigned char>(s[b]);
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v') {
            break;
        }
        ++b;
    }
    std::size_t e = s.size();
    while (e > b) {
        const unsigned char ch = static_cast<unsigned char>(s[e - 1]);
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v') {
            break;
        }
        --e;
    }
    return s.substr(b, e - b);
}

bool iequalsAscii(const std::string &a, const char *b)
{
    if (!b) {
        return false;
    }
    const std::size_t blen = std::char_traits<char>::length(b);
    if (a.size() != blen) {
        return false;
    }
    for (std::size_t i = 0; i < blen; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

std::string qtMakeUserPath_XdgStyle()
{
    // Qt6 make_user_path_without_qstandard_paths() semantics (see _ref_src_qt6/qsettings.cpp)
    // - if XDG_CONFIG_HOME empty => $HOME/.config/
    // - else if absolute => XDG_CONFIG_HOME + '/'
    // - else => $HOME + '/' + XDG_CONFIG_HOME + '/'
    const std::string xdg = getenvNonEmpty("XDG_CONFIG_HOME");
    if (xdg.empty()) {
        const std::string home = getenvNonEmpty("HOME");
        if (home.empty()) {
            return std::string();
        }
        return ensureTrailingSlash(home) + ".config/";
    }
    if (isAbsolutePath(xdg)) {
        return ensureTrailingSlash(xdg);
    }
    const std::string home = getenvNonEmpty("HOME");
    if (home.empty()) {
        return std::string();
    }
    return ensureTrailingSlash(home) + xdg + "/";
}

std::vector<std::string> qtGenericConfigLocations_XdgStyle()
{
    // Equivalent intent to: QStandardPaths::standardLocations(GenericConfigLocation)
    // on XDG platforms.
    // - first: writable (XDG_CONFIG_HOME / fallback)
    // - then: XDG_CONFIG_DIRS (default /etc/xdg)
    std::vector<std::string> out;
    const std::string userPath = qtMakeUserPath_XdgStyle();
    if (!userPath.empty()) {
        out.push_back(userPath);
    }

    std::string dirs = getenvNonEmpty("XDG_CONFIG_DIRS");
    if (dirs.empty()) {
        dirs = "/etc/xdg";
    }
    for (const std::string &d : splitColonList(dirs)) {
        if (!d.empty() && isAbsolutePath(d)) {
            out.push_back(ensureTrailingSlash(d));
        }
    }
    return out;
}

std::vector<std::string> qtNativeUserCandidateFiles(const std::string &organization,
                                                    const std::string &application)
{
    // Equivalent intent to Qt6 QConfFileSettingsPrivate constructor:
    // confFiles order for UserScope:
    // - <userPath>/<org>/<app>.conf
    // - <userPath>/<org>.conf
    // And for system scope on XDG platforms, Qt scans all GenericConfigLocation entries except the first (writable)
    // in the same pattern.

    // NativeFormat extension on Unix is ".conf".
    const std::string ext = ".conf";
    const std::vector<std::string> dirs = qtGenericConfigLocations_XdgStyle();

    std::vector<std::string> out;
    if (organization.empty()) {
        return out;
    }

    const std::string appFile = organization + "/" + application + ext;
    const std::string orgFile = organization + ext;

    if (!dirs.empty()) {
        const std::string &userDir = dirs.front();
        if (!application.empty()) {
            out.push_back(userDir + appFile);
        }
        out.push_back(userDir + orgFile);
    }

    // System locations (dirs excluding first writable)
    for (std::size_t i = 1; i < dirs.size(); ++i) {
        const std::string &d = dirs[i];
        if (!application.empty()) {
            out.push_back(d + appFile);
        }
        out.push_back(d + orgFile);
    }
    return out;
}

bool nativeGeneralContainsKeyInFile(const std::string &filePath, const std::string &key)
{
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }

    std::string data;
    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (sz > 0) {
        data.resize(static_cast<std::size_t>(sz));
        f.read(&data[0], sz);
    }

    std::size_t pos = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        pos = 3;
    }

    bool inGeneral = true;
    std::size_t lineStart = pos;

    while (lineStart < data.size()) {
        std::size_t lineEnd = data.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = data.size();
        }
        std::string line = data.substr(lineStart, lineEnd - lineStart);

        std::size_t next = lineEnd;
        if (next < data.size() && data[next] == '\r') {
            ++next;
            if (next < data.size() && data[next] == '\n') {
                ++next;
            }
        } else if (next < data.size() && data[next] == '\n') {
            ++next;
        }

        std::string trimmed = trimCopy(line);
        if (!trimmed.empty() && trimmed[0] == '[') {
            const std::size_t rb = trimmed.find(']');
            std::string secName = (rb == std::string::npos)
                ? trimmed.substr(1)
                : trimmed.substr(1, rb - 1);
            secName = trimCopy(secName);
            if (iequals(secName, "general")) {
                inGeneral = true;
            } else if (!secName.empty() && secName[0] == '%' && iequals(secName.substr(1), "general")) {
                inGeneral = false;
            } else {
                inGeneral = false;
            }
            lineStart = next;
            continue;
        }

        if (inGeneral) {
            if (trimmed.empty() || trimmed[0] == ';') {
                lineStart = next;
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = trimCopy(line.substr(0, eq));
                k = trimCopy(k);
                if (iniUnescapedKey(k) == key) {
                    return true;
                }
            }
        }

        lineStart = next;
    }

    return false;
}

std::string QSettingsCpp::nativeGeneralValueString(const std::string &filePath,
                                                   const std::string &key,
                                                   const std::string &defaultValue)
{
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->nativeGeneralValueString) {
        return g_hooks->nativeGeneralValueString(filePath, key, defaultValue);
    }
#endif
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        return defaultValue;
    }

    std::string data;
    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (sz > 0) {
        data.resize(static_cast<std::size_t>(sz));
        f.read(&data[0], sz);
    }

    std::size_t pos = 0;
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        pos = 3;
    }

    bool inGeneral = true;
    std::size_t lineStart = pos;

    while (lineStart < data.size()) {
        std::size_t lineEnd = data.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = data.size();
        }
        std::string line = data.substr(lineStart, lineEnd - lineStart);

        std::size_t next = lineEnd;
        if (next < data.size() && data[next] == '\r') {
            ++next;
            if (next < data.size() && data[next] == '\n') {
                ++next;
            }
        } else if (next < data.size() && data[next] == '\n') {
            ++next;
        }

        std::string trimmed = trimCopy(line);
        if (!trimmed.empty() && trimmed[0] == '[') {
            const std::size_t rb = trimmed.find(']');
            std::string secName = (rb == std::string::npos)
                ? trimmed.substr(1)
                : trimmed.substr(1, rb - 1);
            secName = trimCopy(secName);
            if (iequals(secName, "general")) {
                inGeneral = true;
            } else if (!secName.empty() && secName[0] == '%' && iequals(secName.substr(1), "general")) {
                inGeneral = false;
            } else {
                inGeneral = false;
            }
            lineStart = next;
            continue;
        }

        if (inGeneral) {
            if (trimmed.empty() || trimmed[0] == ';') {
                lineStart = next;
                continue;
            }

            const std::size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = trimCopy(line.substr(0, eq));
                std::string v = line.substr(eq + 1);
                k = trimCopy(k);
                if (iniUnescapedKey(k) == key) {
                    return qtStringToVariantToString(iniUnescapedString(v));
                }
            }
        }

        lineStart = next;
    }

    return defaultValue;
}

bool QSettingsCpp::variantStringToBoolLikeQt(const std::string &s)
{
    // Oracle from unit tests (QVariant(QString).toBool()):
    // - empty string => false
    // - case-insensitive "false" => false
    // - exactly "0" (no trimming) => false
    // - everything else => true (including whitespace-only and non-numeric strings)
    if (s.empty()) {
        return false;
    }
    if (iequalsAscii(s, "false")) {
        return false;
    }
    if (s == "0") {
        return false;
    }
    return true;
}

std::string QSettingsCpp::iniGeneralValueString(const std::string &filePath,
                                                const std::string &key,
                                                const std::string &defaultValue)
{
    // On Unix, QSettings IniFormat uses the same INI parser semantics as NativeFormat;
    // only the default file naming differs.
    return nativeGeneralValueString(filePath, key, defaultValue);
}

bool QSettingsCpp::nativeUserContainsKey(const std::string &organization,
                                        const std::string &application,
                                        const std::string &key)
{
    const std::vector<std::string> candidates = qtNativeUserCandidateFiles(organization, application);
    for (const std::string &p : candidates) {
        if (nativeGeneralContainsKeyInFile(p, key)) {
            return true;
        }
    }
    return false;
}

std::string QSettingsCpp::nativeUserValueString(const std::string &organization,
                                               const std::string &application,
                                               const std::string &key,
                                               const std::string &defaultValue)
{
    const std::vector<std::string> candidates = qtNativeUserCandidateFiles(organization, application);
    for (const std::string &p : candidates) {
        if (nativeGeneralContainsKeyInFile(p, key)) {
            return nativeGeneralValueString(p, key, defaultValue);
        }
    }
    return defaultValue;
}

bool QSettingsCpp::nativeUserContainsKeyFromBaseDir(const std::string &configDir,
                                                    const std::string &organization,
                                                    const std::string &application,
                                                    const std::string &key)
{
    const std::vector<std::string> candidates = qtNativeUserCandidateFilesFromBaseDir(configDir, organization, application);
    for (const std::string &p : candidates) {
        if (nativeGeneralContainsKeyInFile(p, key)) {
            return true;
        }
    }
    return false;
}

std::string QSettingsCpp::nativeUserValueStringFromBaseDir(const std::string &configDir,
                                                           const std::string &organization,
                                                           const std::string &application,
                                                           const std::string &key,
                                                           const std::string &defaultValue)
{
    const std::vector<std::string> candidates = qtNativeUserCandidateFilesFromBaseDir(configDir, organization, application);
    for (const std::string &p : candidates) {
        if (nativeGeneralContainsKeyInFile(p, key)) {
            return nativeGeneralValueString(p, key, defaultValue);
        }
    }
    return defaultValue;
}

std::string QSettingsCpp::nativeUserPrimaryFilePathFromBaseDir(const std::string &configDir,
                                                               const std::string &organization,
                                                               const std::string &application)
{
    const std::vector<std::string> candidates = qtNativeUserCandidateFilesFromBaseDir(configDir, organization, application);
    if (candidates.empty()) {
        return std::string();
    }
    return candidates.front();
}

std::vector<std::string> QSettingsCpp::nativeGeneralAllKeys(const std::string &filePath)
{
    std::vector<std::string> keys;
    (void)nativeGeneralTryReadAllKeyValues(filePath, &keys, nullptr);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

std::map<std::string, std::string> QSettingsCpp::nativeGeneralAllKeyValues(const std::string &filePath)
{
    std::map<std::string, std::string> kv;
    (void)nativeGeneralTryReadAllKeyValues(filePath, nullptr, &kv);
    return kv;
}

bool QSettingsCpp::nativeGeneralSetValueString(const std::string &filePath,
                                               const std::string &key,
                                               const std::string &value)
{
    if (key.empty()) {
        return false;
    }

    std::map<std::string, std::string> kv;
    (void)nativeGeneralTryReadAllKeyValues(filePath, nullptr, &kv);
    kv[key] = value;

    const std::size_t slash = filePath.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string dir = filePath.substr(0, slash);
        if (!dir.empty()) {
            std::string cur;
            cur.reserve(dir.size());
            for (std::size_t i = 0; i < dir.size(); ++i) {
                const char c = dir[i];
                cur.push_back(c);
                if (c == '/' && cur.size() > 1) {
                    ::mkdir(cur.c_str(), 0755);
                }
            }
            ::mkdir(cur.c_str(), 0755);
        }
    }

    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "[General]\n";
    for (const auto &it : kv) {
        out << iniEscapedKeyCpp(it.first) << '=' << iniEscapedStringCpp(it.second) << '\n';
    }
    return true;
}
