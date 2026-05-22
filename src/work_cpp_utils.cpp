#include "work_cpp_utils.h"

#include "command_runner.h"
#include "dir_cpp.h"
#include "file_cpp.h"

#include <regex>
#include <cstdlib>
#include <filesystem>
#include <charconv>

#if __has_include(<paths.h>)
#include <paths.h>
#endif

#include <unistd.h>

namespace
{
const WorkCppUtils::Hooks *g_hooks = nullptr;

std::string fallbackPathVariableStd()
{
#if defined(_PATH_DEFPATH)
    return std::string(_PATH_DEFPATH);
#endif
#if defined(_CS_PATH)
    size_t n = confstr(_CS_PATH, nullptr, 0);
    if (n) {
        std::string buf;
        buf.resize(n);
        confstr(_CS_PATH, buf.data(), n);
        if (!buf.empty() && buf.back() == '\0') {
            buf.pop_back();
        }
        return buf;
    }
#endif
    return {};
}

std::vector<std::string> splitPathList(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ':') {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

std::string trimTrailingSlash(const std::string &p)
{
    if (p.size() > 1 && !p.empty() && p.back() == '/') {
        return p.substr(0, p.size() - 1);
    }
    return p;
}

std::string checkExecutableStd(const std::string &path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec)) {
        return {};
    }

    const bool ok = (access(path.c_str(), X_OK) == 0);
    if (!ok) {
        return {};
    }

    return DirCpp::cleanPath(path);
}

std::string makeAbsoluteCandidate(const std::string &searchPath, const std::string &exeName)
{
    std::error_code ec;
    const std::filesystem::path base(searchPath);
    std::filesystem::path candidate = base / exeName;
    if (candidate.is_relative()) {
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            candidate = cwd / candidate;
        }
    }
    return candidate.lexically_normal().string();
}
}

void WorkCppUtils::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

bool WorkCppUtils::checkInstalled(const std::string &package)
{
    static const std::regex validPackageName("^[a-z0-9+.:-]+$");
    if (!std::regex_match(package, validPackageName)) {
        return false;
    }

    const CommandRunner::Result r = CommandRunner::proc(
        "dpkg-query",
        {"-W", "-f=${Status}", "--", package},
        std::string(),
        CommandRunner::QuietMode::Yes,
        CommandRunner::Elevation::No);

    if (!(r.started && r.normalExit && r.exitCode == 0)) {
        return false;
    }

    return r.stdoutText.find("install ok installed") != std::string::npos;
}

std::string WorkCppUtils::findExecutable(const std::string &executableName, const std::vector<std::string> &paths)
{
    if (g_hooks && g_hooks->findExecutable) {
        return g_hooks->findExecutable(executableName, paths);
    }
    if (executableName.empty()) {
        return {};
    }

    const bool isAbsolute = (!executableName.empty() && executableName.front() == '/');
    if (isAbsolute) {
        return checkExecutableStd(executableName);
    }

    std::vector<std::string> searchPaths = paths;
    if (searchPaths.empty()) {
        const char *p = std::getenv("PATH");
        std::string pEnv;
        if (p) {
            pEnv = p;
        } else {
            pEnv = fallbackPathVariableStd();
        }
        for (const std::string &raw : splitPathList(pEnv)) {
            const std::string clean = trimTrailingSlash(DirCpp::cleanPath(raw));
            if (!clean.empty()) {
                searchPaths.push_back(clean);
            }
        }
    }

    for (const std::string &searchPath : searchPaths) {
        const std::string candidate = makeAbsoluteCandidate(searchPath, executableName);
        const std::string absPath = checkExecutableStd(candidate);
        if (!absPath.empty()) {
            return absPath;
        }
    }
    return {};
}

bool WorkCppUtils::isEnvironmentReady(const std::string &workDir, const std::string &snapshotDir)
{
    if (workDir.empty() || !DirCpp::exists(workDir)) {
        return false;
    }
    if (snapshotDir.empty() || !DirCpp::exists(snapshotDir)) {
        return false;
    }

    const std::vector<std::string> requiredTools {"mksquashfs", "xorriso"};
    for (const std::string &tool : requiredTools) {
        if (findExecutable(tool).empty()) {
            return false;
        }
    }

    return true;
}

std::uint64_t WorkCppUtils::parseDuKilobytes(const std::string &output, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    if (output.empty()) {
        return 0;
    }

    // Find last non-empty line (trim CR/LF)
    size_t end = output.size();
    while (end > 0 && (output[end - 1] == '\n' || output[end - 1] == '\r')) {
        --end;
    }
    if (end == 0) {
        return 0;
    }
    size_t start = output.rfind('\n', end - 1);
    if (start == std::string::npos) {
        start = 0;
    } else {
        start += 1;
    }
    while (start < end && output[start] == '\r') {
        ++start;
    }
    if (start >= end) {
        return 0;
    }

    // First field up to TAB, trim spaces
    size_t tabPos = output.find('\t', start);
    if (tabPos == std::string::npos || tabPos > end) {
        tabPos = end;
    }
    size_t f0b = start;
    while (f0b < tabPos && output[f0b] == ' ') {
        ++f0b;
    }
    size_t f0e = tabPos;
    while (f0e > f0b && output[f0e - 1] == ' ') {
        --f0e;
    }
    if (f0e <= f0b) {
        return 0;
    }

    std::uint64_t value = 0;
    const char *p = output.data() + f0b;
    const char *pEnd = output.data() + f0e;
    auto res = std::from_chars(p, pEnd, value, 10);
    if (res.ec != std::errc() || res.ptr != pEnd) {
        return 0;
    }

    if (ok) {
        *ok = true;
    }
    return value;
}

bool WorkCppUtils::writeTextFileUtf8NoBomTruncate(const std::string &filePath, const std::string &text)
{
    if (g_hooks && g_hooks->writeTextFileUtf8NoBomTruncate) {
        return g_hooks->writeTextFileUtf8NoBomTruncate(filePath, text);
    }
    FileCpp f(filePath);
    if (!f.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Truncate)) {
        return false;
    }
    const std::int64_t w = f.write(text);
    if (w < 0 || static_cast<std::size_t>(w) != text.size()) {
        f.close();
        return false;
    }
    f.close();
    return true;
}

WorkCppUtils::ReplaceStringError WorkCppUtils::replaceStringInFileUtf8NoBom(
    const std::string &filePath,
    const std::string &oldText,
    const std::string &newText)
{
    if (g_hooks && g_hooks->replaceStringInFileUtf8NoBom) {
        return g_hooks->replaceStringInFileUtf8NoBom(filePath, oldText, newText);
    }
    if (oldText.empty()) {
        return ReplaceStringError::None;
    }

    FileCpp f(filePath);
    if (!f.open(FileCpp::OpenMode::ReadWrite | FileCpp::OpenMode::Text)) {
        return ReplaceStringError::OpenFailed;
    }

    const std::vector<std::uint8_t> bytes = f.readAll();
    if (!f.errorString().empty() && bytes.empty()) {
        f.close();
        return ReplaceStringError::OpenFailed;
    }

    std::string content(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (content.find(oldText) == std::string::npos) {
        f.close();
        return ReplaceStringError::None;
    }

    // Replace all occurrences (QString::replace default behavior)
    std::string out;
    out.reserve(content.size());
    size_t pos = 0;
    for (;;) {
        const size_t p = content.find(oldText, pos);
        if (p == std::string::npos) {
            out.append(content, pos, std::string::npos);
            break;
        }
        out.append(content, pos, p - pos);
        out.append(newText);
        pos = p + oldText.size();
    }

    // Truncate then rewrite (mirror QFile::resize(0) + QTextStream seek/write)
    f.close();
    FileCpp wf(filePath);
    if (!wf.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Truncate)) {
        return ReplaceStringError::WriteFailed;
    }
    const std::int64_t w = wf.write(out);
    wf.close();
    if (w < 0 || static_cast<std::size_t>(w) != out.size()) {
        return ReplaceStringError::WriteFailed;
    }
    return ReplaceStringError::None;
}

bool WorkCppUtils::writeQSettingsNativeGeneralString(const std::string &filePath,
                                                    const std::string &key,
                                                    const std::string &value)
{
    if (g_hooks && g_hooks->writeQSettingsNativeGeneralString) {
        return g_hooks->writeQSettingsNativeGeneralString(filePath, key, value);
    }
    // Qt6 on Unix uses an INI-like format for NativeFormat via writeIniFile().
    // For an empty section, it writes "[General]" followed by "key=value\n" lines.
    if (key.empty()) {
        return false;
    }

    const auto toHexUpper = [](unsigned v) -> char {
        return static_cast<char>((v < 10) ? ('0' + v) : ('A' + (v - 10)));
    };

    const auto isAsciiLetterOrNumber = [](unsigned ch) -> bool {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    };

    const auto isHexDigit = [](unsigned ch) -> bool {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    };

    const auto iniEscapedKey = [&](const std::string &k) -> std::string {
        std::string out;
        out.reserve(k.size() * 2);
        // Note: Qt operates on UTF-16 QChars, but for our backend the keys used are ASCII.
        // For non-ASCII bytes, we escape each byte as %XX to stay deterministic.
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
    };

    const auto iniEscapedString = [&](const std::string &s) -> std::string {
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
                // Qt: "\\x" + QByteArray::number(ch, 16) (lowercase)
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
                    // UTF-8 passthrough
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
    };

    std::string text;
    text.reserve(32 + key.size() + value.size());
    text += "[General]\n";
    text += iniEscapedKey(key);
    text += '=';
    text += iniEscapedString(value);
    text += '\n';

    return writeTextFileUtf8NoBomTruncate(filePath, text);
}

std::string WorkCppUtils::buildLsbReleaseContent(const std::string &projectName,
                                                const std::string &distroVersion,
                                                const std::string &codename)
{
    std::string out;
    out.reserve(projectName.size() * 4 + distroVersion.size() * 3 + codename.size() * 4 + 128);

    out += "PRETTY_NAME=\"";
    out += projectName;
    out += ' ';
    out += distroVersion;
    out += ' ';
    out += codename;
    out += "\"\n";

    out += "DISTRIB_ID=\"";
    out += projectName;
    out += "\"\n";

    out += "DISTRIB_RELEASE=";
    out += distroVersion;
    out += "\n";

    out += "DISTRIB_CODENAME=\"";
    out += codename;
    out += "\"\n";

    out += "DISTRIB_DESCRIPTION=\"";
    out += projectName;
    out += ' ';
    out += distroVersion;
    out += ' ';
    out += codename;
    out += "\"\n";

    return out;
}

bool WorkCppUtils::writeLinuxfsInfoFromMksquashfsOutput(const std::string &mksquashfsOutput,
                                                       const std::string &linuxfsInfoPath)
{
    // Parse mksquashfs output to extract uncompressed filesystem size
    // Looking for pattern: " uncompressed filesystem size ("
    // Example: "Filesystem size 1234.56 Mbytes (1234567 Kbytes / 1234 Mbytes)"
    // We want to extract the KB value: "1234567"
    
    const std::string separator = " uncompressed filesystem size (";
    std::string value;
    
    const size_t pos = mksquashfsOutput.find(separator);
    if (pos != std::string::npos) {
        // Found the separator, extract text after it
        std::string after = mksquashfsOutput.substr(pos + separator.length());
        
        // Check if there's a second occurrence (Qt code does this)
        const size_t secondPos = after.find(separator);
        if (secondPos != std::string::npos) {
            after = after.substr(0, secondPos);
        }
        
        // Extract value up to first space
        const size_t spacePos = after.find(' ');
        if (spacePos != std::string::npos) {
            value = after.substr(0, spacePos);
        } else {
            value = after;
        }
    }
    
    // Write to linuxfs.info file using QSettings format
    return writeQSettingsNativeGeneralString(linuxfsInfoPath, "UncompressedSizeKB", value);
}


bool WorkCppUtils::replaceMenuStrings(const std::string &workDir,
                                     const std::string &projectName,
                                     const std::string &distroVersion,
                                     const std::string &fullDistroName,
                                     const std::string &releaseDate,
                                     const std::string &codename,
                                     const std::string &bootOptions)
{
    // Replace underscores with spaces in fullDistroName
    std::string fullDistroNameSpace = fullDistroName;
    for (char &ch : fullDistroNameSpace) {
        if (ch == '_') {
            ch = ' ';
        }
    }

    // Process grub.cfg
    const std::string grubCfg = workDir + "/iso-template/boot/grub/grub.cfg";
    const std::string distro = projectName + "-" + distroVersion;
    
    if (replaceStringInFileUtf8NoBom(grubCfg, "%DISTRO%", distro) != ReplaceStringError::None) {
        return false;
    }
    if (replaceStringInFileUtf8NoBom(grubCfg, "%DISTRO_NAME%", projectName) != ReplaceStringError::None) {
        return false;
    }
    if (replaceStringInFileUtf8NoBom(grubCfg, "%FULL_DISTRO_NAME%", fullDistroName) != ReplaceStringError::None) {
        return false;
    }
    if (replaceStringInFileUtf8NoBom(grubCfg, "%FULL_DISTRO_NAME_SPACE%", fullDistroNameSpace) != ReplaceStringError::None) {
        return false;
    }
    if (replaceStringInFileUtf8NoBom(grubCfg, "%RELEASE_DATE%", releaseDate) != ReplaceStringError::None) {
        return false;
    }

    // Process grubenv.cfg - extract boot parameters and update grub.cfg
    const std::string grubenvCfg = workDir + "/iso-template/boot/grub/grubenv.cfg";
    const std::string bootParameterRegexp = "(lang|kbd|kbvar|kbopt|tz)=[^[:space:]]*";
    
    // Extract boot parameters matching the regexp and append to grubenv.cfg
    std::string grepCmd = "printf '%s\\n' " + bootOptions + " | grep -E '^" + bootParameterRegexp + "' >> '" + grubenvCfg + "'";
    (void)CommandRunner::run(grepCmd, CommandRunner::QuietMode::Yes);
    
    // Replace %OPTIONS% in grub.cfg with filtered boot options
    std::string sedCmd = "sed -i \"s|%OPTIONS%|$(sed -r 's/[[:space:]]" + bootParameterRegexp + "/ /g; s/^[[:space:]]+//; s/[[:space:]]+/ /g'<<<' " + bootOptions + "')|\" '" + grubCfg + "'";
    (void)CommandRunner::run(sedCmd, CommandRunner::QuietMode::Yes);

    // Process syslinux.cfg and isolinux.cfg
    const std::string syslinuxCfg = workDir + "/iso-template/boot/syslinux/syslinux.cfg";
    const std::string isolinuxCfg = workDir + "/iso-template/boot/isolinux/isolinux.cfg";
    
    for (const std::string &file : {syslinuxCfg, isolinuxCfg}) {
        if (replaceStringInFileUtf8NoBom(file, "%OPTIONS%", bootOptions) != ReplaceStringError::None) {
            return false;
        }
        if (replaceStringInFileUtf8NoBom(file, "%CODE_NAME%", codename) != ReplaceStringError::None) {
            return false;
        }
    }

    // Process readme.msg files
    const std::string sysReadme = workDir + "/iso-template/boot/syslinux/readme.msg";
    const std::string isoReadme = workDir + "/iso-template/boot/isolinux/readme.msg";
    
    for (const std::string &file : {syslinuxCfg, isolinuxCfg, sysReadme, isoReadme}) {
        if (replaceStringInFileUtf8NoBom(file, "%FULL_DISTRO_NAME%", fullDistroName) != ReplaceStringError::None) {
            return false;
        }
        if (replaceStringInFileUtf8NoBom(file, "%RELEASE_DATE%", releaseDate) != ReplaceStringError::None) {
            return false;
        }
    }

    // Process theme files
    const std::string themeDir = workDir + "/iso-template/boot/grub/theme";
    DirCpp dir(themeDir);
    const std::vector<std::string> themeFiles = dir.entryInfoList({"*.txt"}, DirCpp::EntryType::Files);
    
    for (const std::string &themeFile : themeFiles) {
        if (replaceStringInFileUtf8NoBom(themeFile, "%ASCII_CODE_NAME%", codename) != ReplaceStringError::None) {
            return false;
        }
        if (replaceStringInFileUtf8NoBom(themeFile, "%DISTRO%", distro) != ReplaceStringError::None) {
            return false;
        }
    }

    return true;
}
