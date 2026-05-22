#include "work_space_cpp.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>

#include "command_runner.h"
#include "dir_cpp.h"
#include "filesystemutils_cpp.h"
#include "file_cpp.h"
#include "tempfile_cpp.h"
#include "work_cpp_utils.h"

namespace {

static void cb_message(const WorkSpaceCpp::Callbacks &cb, const std::string &text)
{
    if (cb.message) {
        cb.message(text);
    }
}

static void cb_warning(const WorkSpaceCpp::Callbacks &cb, const std::string &text)
{
    if (cb.warning) {
        cb.warning(text);
    }
}

static std::string format_double_fixed_2(double v)
{
    // Match Qt QString::number(x, 'f', 2): always '.' decimal separator.
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

static std::uint8_t compression_factor_value_like_settings_qt(const std::string &compressionName)
{
    // Must match Settings::compressionFactorValue() behavior.
    if (compressionName == "xz") {
        return 31;
    }
    if (compressionName == "zstd") {
        return 35;
    }
    if (compressionName == "gzip") {
        return 37;
    }
    if (compressionName == "lzo") {
        return 52;
    }
    if (compressionName == "lzma") {
        return 52;
    }
    if (compressionName == "lz4") {
        return 52;
    }
    return 0;
}

static bool glob_qt_default_match_component_impl(const std::string &pattern,
                                                std::size_t pi,
                                                const std::string &text,
                                                std::size_t ti)
{
    while (pi < pattern.size()) {
        const char pc = pattern[pi];

        if (pc == '*') {
            ++pi;
            const bool atEnd = (pi == pattern.size());
            std::size_t maxK = text.size();
            if (!atEnd) {
                const std::size_t slash = text.find('/', ti);
                if (slash != std::string::npos) {
                    maxK = slash;
                }
            }
            for (std::size_t k = ti; k <= maxK; ++k) {
                if (glob_qt_default_match_component_impl(pattern, pi, text, k)) {
                    return true;
                }
            }
            return false;
        }

        if (ti >= text.size()) {
            return false;
        }

        if (pc == '?') {
            if (text[ti] == '/') {
                return false;
            }
            ++pi;
            ++ti;
            continue;
        }

        if (pc == '[') {
            const std::size_t closePos = pattern.find(']', pi + 1);
            if (closePos == std::string::npos) {
                return false;
            }

            const char tc = text[ti];
            if (tc == '/') {
                return false;
            }

            bool matched = false;
            bool negated = false;
            std::size_t k = pi + 1;
            if (k < closePos && (pattern[k] == '!' || pattern[k] == '^')) {
                negated = true;
                ++k;
            }

            for (; k < closePos; ++k) {
                const char cc = pattern[k];
                if (cc == '/') {
                    return false;
                }
                if (k + 2 < closePos && pattern[k + 1] == '-') {
                    const char start = cc;
                    const char end = pattern[k + 2];
                    if (tc >= start && tc <= end) {
                        matched = true;
                    }
                    k += 2;
                    continue;
                }
                if (tc == cc) {
                    matched = true;
                }
            }

            if (negated) {
                matched = !matched;
            }
            if (!matched) {
                return false;
            }

            pi = closePos + 1;
            ++ti;
            continue;
        }

        if (pc != text[ti]) {
            return false;
        }
        ++pi;
        ++ti;
    }

    return ti == text.size();
}

static bool glob_qt_default_match_component(const std::string &pattern, const std::string &text)
{
    // Pre-scan: disallow '/' inside character classes.
    std::size_t i = 0;
    while (i < pattern.size()) {
        const char c = pattern[i];
        if (c == '[') {
            const std::size_t closePos = pattern.find(']', i + 1);
            if (closePos == std::string::npos) {
                return false;
            }
            for (std::size_t k = i + 1; k < closePos; ++k) {
                if (pattern[k] == '/') {
                    return false;
                }
            }
            i = closePos + 1;
            continue;
        }
        ++i;
    }

    return glob_qt_default_match_component_impl(pattern, 0, text, 0);
}

static bool contains_wildcard(const std::string &path)
{
    return path.find_first_of("*?[{") != std::string::npos;
}

static std::string normalize_exclude(std::string path)
{
    std::string collapsed;
    collapsed.reserve(path.size());

    for (std::size_t i = 0; i < path.size(); ++i) {
        const char ch = path[i];
        if (ch == '/') {
            collapsed.push_back('/');
            while (i + 1 < path.size() && path[i + 1] == '/') {
                ++i;
            }
        } else {
            collapsed.push_back(ch);
        }
    }

    path = collapsed;
    if (path.size() > 1 && !path.empty() && path.back() == '/') {
        path.pop_back();
    }

    return path;
}

static std::string strip_trailing_newline(std::string s)
{
    if (!s.empty() && s.back() == '\n') {
        s.pop_back();
        if (!s.empty() && s.back() == '\r') {
            s.pop_back();
        }
    }
    return s;
}

static bool starts_with(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static std::uint64_t device_id_u64(const std::string &path)
{
    return static_cast<std::uint64_t>(FileSystemUtilsCpp::deviceId(path));
}

static std::vector<std::string> split_session_excludes_like_qt(const std::string &sessionExcludes)
{
    // Qt: QStringList excludeList = sessionExcludes.split("\" \""); then remove quotes.
    std::cerr << "=== DEBUG split_session_excludes_like_qt START ===" << std::endl;
    std::cerr << "Input sessionExcludes length: " << sessionExcludes.length() << std::endl;
    std::cerr << "Input sessionExcludes: " << sessionExcludes << std::endl;
    
    if (sessionExcludes.empty()) {
        std::cerr << "sessionExcludes is EMPTY, returning empty vector" << std::endl;
        std::cerr << "=== DEBUG split_session_excludes_like_qt END ===" << std::endl;
        return {};
    }

    std::vector<std::string> out;
    std::size_t start = 0;
    const std::string sep = "\" \"";
    int partIndex = 0;
    
    while (true) {
        const std::size_t pos = sessionExcludes.find(sep, start);
        std::string part = (pos == std::string::npos) ? sessionExcludes.substr(start)
                                                      : sessionExcludes.substr(start, pos - start);

        std::cerr << "Part " << partIndex << " BEFORE cleanup: [" << part << "]" << std::endl;
        
        part.erase(std::remove(part.begin(), part.end(), '"'), part.end());
        // trimmed
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t' || part.front() == '\n' || part.front() == '\r')) {
            part.erase(part.begin());
        }
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t' || part.back() == '\n' || part.back() == '\r')) {
            part.pop_back();
        }

        std::cerr << "Part " << partIndex << " AFTER cleanup: [" << part << "]" << std::endl;
        
        if (!part.empty()) {
            out.push_back(part);
            std::cerr << "Part " << partIndex << " ADDED to output" << std::endl;
        } else {
            std::cerr << "Part " << partIndex << " SKIPPED (empty)" << std::endl;
        }

        if (pos == std::string::npos) {
            break;
        }
        start = pos + sep.size();
        partIndex++;
    }

    std::cerr << "Total parts extracted: " << out.size() << std::endl;
    std::cerr << "=== DEBUG split_session_excludes_like_qt END ===" << std::endl;
    return out;
}

static std::string join_path_like_dircpp_filePath(const std::string &base, const std::string &rel)
{
    return DirCpp(base).filePath(rel);
}

static std::vector<std::string> split_path_components_no_empty(const std::string &s)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == '/') {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        const std::size_t j = s.find('/', i);
        if (j == std::string::npos) {
            out.push_back(s.substr(i));
            break;
        }
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

} // namespace

std::uint64_t WorkSpaceCpp::getRequiredSpaceLikeQt(const SettingsCpp &settings,
                                                   const std::string &applicationName,
                                                   const Callbacks &cb)
{
    std::cerr << "=== DEBUG getRequiredSpaceLikeQt START ===" << std::endl;
    std::cerr << "settings.sessionExcludes length: " << settings.sessionExcludes.length() << std::endl;
    std::cerr << "settings.sessionExcludes: " << settings.sessionExcludes << std::endl;
    
    std::vector<std::string> excludes;

    // Open and read the excludes file
    if (!settings.snapshotExcludesPath.empty()) {
        FileCpp f(settings.snapshotExcludesPath);
        if (!f.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
            cb_warning(cb, std::string("Could not open file: ") + settings.snapshotExcludesPath);
        } else {
            while (true) {
                std::string lineBytes = f.readLine();
                if (lineBytes.empty()) {
                    break;
                }
                lineBytes = strip_trailing_newline(lineBytes);
                // Qt: fromLocal8Bit + trimmed
                while (!lineBytes.empty()
                       && (lineBytes.front() == ' ' || lineBytes.front() == '\t' || lineBytes.front() == '\n'
                           || lineBytes.front() == '\r')) {
                    lineBytes.erase(lineBytes.begin());
                }
                while (!lineBytes.empty()
                       && (lineBytes.back() == ' ' || lineBytes.back() == '\t' || lineBytes.back() == '\n'
                           || lineBytes.back() == '\r')) {
                    lineBytes.pop_back();
                }

                if (!lineBytes.empty() && lineBytes.front() != '#'
                    && !starts_with(lineBytes, ".bind-root")) {
                    excludes.push_back(lineBytes);
                }
            }
            f.close();
        }
    }

    std::cerr << "Excludes from file: " << excludes.size() << std::endl;

    // Add session excludes
    if (!settings.sessionExcludes.empty()) {
        const std::vector<std::string> xs = split_session_excludes_like_qt(settings.sessionExcludes);
        std::cerr << "Session excludes parsed: " << xs.size() << std::endl;
        excludes.insert(excludes.end(), xs.begin(), xs.end());
    }
    
    std::cerr << "Total excludes BEFORE expansion: " << excludes.size() << std::endl;

    std::string sizeRoot = "/.bind-root";
    const std::string overlayLower = std::string("/run/") + applicationName + "/bind-root-overlay/lower";
    if (FileCpp::exists(overlayLower)) {
        sizeRoot = overlayLower;
    }

    std::string sizeRootPrefix = sizeRoot;
    if (sizeRootPrefix.empty() || sizeRootPrefix.back() != '/') {
        sizeRootPrefix.push_back('/');
    }

    const std::uint64_t sizeRootDevice = device_id_u64(sizeRoot);
    const std::uint64_t rootInfoDevice = device_id_u64("/");
    const std::uint64_t homeInfoDevice = device_id_u64("/home");

    std::uint64_t rootDevice = sizeRootDevice;
    bool includeHomeDevice = false;
    if (!settings.live) {
        rootDevice = rootInfoDevice;
        const bool homeIsMount = FileSystemUtilsCpp::isMountPoint(std::string("/home"));
        if (homeIsMount && homeInfoDevice != 0 && homeInfoDevice != rootDevice) {
            includeHomeDevice = true;
        }
    }

    auto is_bind_mount = [](const std::string &mountPoint) -> bool {
        FileCpp mounts(std::string("/proc/self/mounts"));
        if (!mounts.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
            return false;
        }

        while (true) {
            std::string lineBytes = mounts.readLine();
            if (lineBytes.empty()) {
                break;
            }
            lineBytes = strip_trailing_newline(lineBytes);

            // parts separated by spaces.
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : lineBytes) {
                if (ch == ' ') {
                    if (!cur.empty()) {
                        parts.push_back(cur);
                        cur.clear();
                    }
                } else {
                    cur.push_back(ch);
                }
            }
            if (!cur.empty()) {
                parts.push_back(cur);
            }
            if (parts.size() < 4) {
                continue;
            }

            std::string target = parts[1];
            // Qt replaces "\\040" by " "
            for (std::size_t p = target.find("\\040"); p != std::string::npos; p = target.find("\\040")) {
                target.replace(p, 4, " ");
            }
            if (target != mountPoint) {
                continue;
            }

            const std::string opts = parts[3];
            bool hasBind = false;
            std::size_t start = 0;
            while (start <= opts.size()) {
                const std::size_t comma = opts.find(',', start);
                const std::string opt = (comma == std::string::npos) ? opts.substr(start)
                                                                     : opts.substr(start, comma - start);
                if (opt == "bind" || opt == "rbind") {
                    hasBind = true;
                    break;
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            mounts.close();
            return hasBind;
        }

        mounts.close();
        return false;
    };

    // If /home is bind-mounted or reset (empty in overlay), exclude it from the size estimate.
    if (!settings.live && FileSystemUtilsCpp::isMountPoint(std::string("/home")) && homeInfoDevice == rootDevice) {
        bool shouldExclude = is_bind_mount("/home");

        const std::string overlayHome = std::string("/run/") + applicationName + "/bind-root-overlay/root/home";
        if (!shouldExclude) {
            const DirCpp homeDir(overlayHome);
            if (homeDir.exists() && homeDir.isEmpty()) {
                shouldExclude = true;
            }
        }

        if (shouldExclude) {
            excludes.push_back("home");
        }
    }

    const std::string sizeRootBase = (sizeRootPrefix.size() > 1 && sizeRootPrefix.back() == '/')
                                        ? sizeRootPrefix.substr(0, sizeRootPrefix.size() - 1)
                                        : sizeRootPrefix;

    auto is_allowed_device = [&](const std::string &path) -> bool {
        std::string probePath = path;
        if (FileCpp::isSymLink(path)) {
            probePath = DirCpp::absolutePathOfContainingDir(path);
        }
        const std::uint64_t probeDevice = device_id_u64(probePath);
        return probeDevice == sizeRootDevice || probeDevice == rootInfoDevice || probeDevice == homeInfoDevice;
    };

    auto expand_exclude_pattern = [&](std::string rawPattern) -> std::vector<std::string> {
        while (!rawPattern.empty() && rawPattern.front() == '/') {
            rawPattern.erase(rawPattern.begin());
        }

        if (rawPattern.size() >= 2 && rawPattern.compare(rawPattern.size() - 2, 2, "/*") == 0) {
            rawPattern.resize(rawPattern.size() - 2);
        }

        std::string fullPattern = join_path_like_dircpp_filePath(sizeRootPrefix, rawPattern);
        fullPattern = DirCpp::cleanPath(fullPattern);

        std::string relativePattern = fullPattern;
        if (starts_with(relativePattern, sizeRootBase)) {
            relativePattern.erase(0, sizeRootBase.size());
        }
        while (!relativePattern.empty() && relativePattern.front() == '/') {
            relativePattern.erase(relativePattern.begin());
        }

        if (relativePattern.empty()) {
            return {sizeRootBase};
        }

        std::vector<std::string> components = split_path_components_no_empty(relativePattern);
        std::vector<std::string> current{sizeRootBase};

        for (std::size_t i = 0; i < components.size(); ++i) {
            const std::string &component = components[i];
            const bool isLast = (i == components.size() - 1);

            std::vector<std::string> next;

            if (contains_wildcard(component)) {
                for (const std::string &base : current) {
                    if (!FileCpp::exists(base) || !FileCpp::isDir(base) || FileCpp::isSymLink(base)) {
                        continue;
                    }

                    DirCpp::Filter f = DirCpp::Filter::NoDotAndDotDot;
                    if (isLast) {
                        f = static_cast<DirCpp::Filter>(static_cast<unsigned>(f)
                                                        | static_cast<unsigned>(DirCpp::Filter::AllEntries));
                    } else {
                        f = static_cast<DirCpp::Filter>(static_cast<unsigned>(f)
                                                        | static_cast<unsigned>(DirCpp::Filter::Dirs)
                                                        | static_cast<unsigned>(DirCpp::Filter::NoSymLinks));
                    }

                    const std::vector<DirCpp::FileInfo> entries = DirCpp(base).entryInfoList(f);
                    for (const DirCpp::FileInfo &entry : entries) {
                        if (!glob_qt_default_match_component(component, entry.fileName)) {
                            continue;
                        }
                        if (!isLast && entry.isSymLink) {
                            continue;
                        }
                        next.push_back(entry.filePath);
                    }
                }
            } else {
                for (const std::string &base : current) {
                    const std::string candidate = join_path_like_dircpp_filePath(base, component);
                    if (!FileCpp::exists(candidate) && !FileCpp::isSymLink(candidate)) {
                        continue;
                    }
                    if (!isLast) {
                        if (!FileCpp::isDir(candidate) || FileCpp::isSymLink(candidate)) {
                            continue;
                        }
                    }
                    next.push_back(candidate);
                }
            }

            current = next;
            if (current.empty()) {
                break;
            }
        }

        return current;
    };

    std::vector<std::string> expandedExcludes;
    expandedExcludes.reserve(excludes.size());

    std::cerr << "=== EXPANDING EXCLUDES ===" << std::endl;
    for (const std::string &rawValue : excludes) {
        std::cerr << "Processing exclude: [" << rawValue << "]" << std::endl;
        std::string cleaned = rawValue;
        const std::size_t bangIndex = cleaned.find('!');
        if (bangIndex != std::string::npos) {
            cleaned.resize(bangIndex);
            std::cerr << "  After removing !: [" << cleaned << "]" << std::endl;
        }
        if (cleaned.empty()) {
            std::cerr << "  SKIPPED (empty after cleanup)" << std::endl;
            continue;
        }

        const std::vector<std::string> matches = expand_exclude_pattern(cleaned);
        std::cerr << "  Expanded to " << matches.size() << " matches" << std::endl;
        for (const std::string &match : matches) {
            if (!is_allowed_device(match)) {
                std::cerr << "    SKIPPED (not allowed device): " << match << std::endl;
                continue;
            }
            const std::string normalized = normalize_exclude(match);
            if (!normalized.empty()) {
                expandedExcludes.push_back(normalized);
                std::cerr << "    ADDED: " << normalized << std::endl;
            } else {
                std::cerr << "    SKIPPED (empty after normalize): " << match << std::endl;
            }
        }
    }

    excludes = expandedExcludes;
    std::cerr << "Total excludes AFTER expansion: " << excludes.size() << std::endl;

    // Filter out nested paths to avoid double-counting in size calculation
    std::sort(excludes.begin(), excludes.end(), [](const std::string &a, const std::string &b) {
        return a.size() < b.size();
    });

    std::cerr << "=== FILTERING NESTED PATHS ===" << std::endl;
    std::vector<std::string> filteredExcludes;
    for (const std::string &path : excludes) {
        bool isNested = false;
        for (const std::string &accepted : filteredExcludes) {
            if (accepted == "/") {
                isNested = (path != "/");
                break;
            }
            if (path == accepted || starts_with(path, accepted + "/")) {
                isNested = true;
                std::cerr << "  NESTED: [" << path << "] is nested under [" << accepted << "]" << std::endl;
                break;
            }
        }
        if (!isNested) {
            filteredExcludes.push_back(path);
            std::cerr << "  KEPT: [" << path << "]" << std::endl;
        }
    }

    excludes = filteredExcludes;
    std::cerr << "Total excludes AFTER filtering: " << excludes.size() << std::endl;

    cb_message(cb, "Calculating total size of excluded files...");

    bool ok = true;
    std::uint64_t excl_size = 0;

    if (!excludes.empty()) {
        TempFileCpp excludeList;
        excludeList.setAutoRemove(true);

        if (!excludeList.open()) {
            ok = false;
        } else {
            for (const std::string &path : excludes) {
                if (!excludeList.writeAll(path.c_str(), path.size())) {
                    ok = false;
                    break;
                }
                const char z = '\0';
                if (!excludeList.writeAll(&z, 1)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                ok = excludeList.flush();
            }

            std::vector<std::string> duArgs = settings.live
                                                 ? std::vector<std::string>{"-sc", "-P", "--apparent-size"}
                                                 : std::vector<std::string>{"-sxc", "-P"};

            if (ok) {
                duArgs.push_back(std::string("--files0-from=") + excludeList.fileName());
                excl_size = WorkCppUtils::parseDuKilobytes(
                    CommandRunner::getOutAsRoot("du", duArgs, CommandRunner::QuietMode::Yes),
                    &ok);
            }

            excludeList.close();
        }
    }

    if (!ok) {
        cb_warning(cb,
                   "Error: calculating size of excluded files\nIf you are sure you have enough free space rerun the program with -o/--override-size option");
        return 0;
    }

    cb_message(cb, "Calculating size of root...");

    std::uint64_t root_size = 0;
    if (settings.live) {
        root_size = WorkCppUtils::parseDuKilobytes(
            CommandRunner::getOutAsRoot(
                "du",
                {"-s", "-P", "--apparent-size", sizeRoot},
                CommandRunner::QuietMode::Yes),
            &ok);
    } else {
        root_size = WorkCppUtils::parseDuKilobytes(
            CommandRunner::getOutAsRoot(
                "du",
                {"-sx", "-P", sizeRoot},
                CommandRunner::QuietMode::Yes),
            &ok);
    }

    if (!settings.live) {
        const std::uint64_t rootTotal = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesTotal(std::string("/")));
        const std::uint64_t rootFree = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesFree(std::string("/")));
        ok = (rootTotal != 0);
        if (ok) {
            root_size = (rootTotal - rootFree) / 1024;
            if (includeHomeDevice) {
                const std::uint64_t homeTotal
                    = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesTotal(std::string("/home")));
                const std::uint64_t homeFree
                    = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesFree(std::string("/home")));
                ok = (homeTotal != 0);
                if (ok) {
                    root_size += (homeTotal - homeFree) / 1024;
                }
            }
        }
    }

    if (!ok) {
        cb_warning(cb,
                   "Error: calculating root size.\nIf you are sure you have enough free space rerun the program with -o/--override-size option");
        return 0;
    }

    if (excl_size > root_size) {
        std::cerr << "WARNING: Excluded size exceeds root size; clamping excluded size for estimate." << std::endl;
        std::cerr << "Root: " << root_size << " KB, Excluded: " << excl_size << " KB" << std::endl;
        excl_size = root_size;
    }

    constexpr double kibToMib = 1024.0;
    std::cerr << "=== SIZE CALCULATION SUMMARY ===" << std::endl;
    std::cerr << "SIZE         " << format_double_fixed_2(root_size / kibToMib) << " MiB" << std::endl;
    std::cerr << "SIZE EXCLUDES" << format_double_fixed_2(excl_size / kibToMib) << " MiB" << std::endl;
    
    const std::uint8_t c_factor = compression_factor_value_like_settings_qt(settings.compression);
    std::cerr << "COMPRESSION  " << static_cast<int>(c_factor) << "%" << std::endl;
    
    const std::uint64_t result = (root_size - excl_size) * static_cast<std::uint64_t>(c_factor) / 100ULL;
    std::cerr << "SIZE NEEDED  " << format_double_fixed_2(result / kibToMib) << " MiB" << std::endl;
    std::cerr << "SIZE FREE    " << format_double_fixed_2(settings.freeSpace / kibToMib) << " MiB" << std::endl;
    std::cerr << "=== END SIZE CALCULATION ===" << std::endl;
    
    return result;
}

WorkSpaceCpp::CheckEnoughSpaceResult WorkSpaceCpp::checkEnoughSpaceLikeQt(const SettingsCpp &settings,
                                                                          const std::string &applicationName,
                                                                          const Callbacks &cb)
{
    CheckEnoughSpaceResult out;

    const std::uint64_t required_space = getRequiredSpaceLikeQt(settings, applicationName, cb);
    out.requiredSpaceKiB = required_space;

    auto check_no_space = [&](std::uint64_t needed_space, std::uint64_t free_space, const std::string &dir) {
        if (needed_space > free_space) {
            constexpr double factor = 1024.0 * 1024.0;
            out.ok = false;
            out.messageBoxTitle = "Error";
            out.messageBoxText = std::string("There's not enough free space on your target disk, you need at least ")
                + format_double_fixed_2(static_cast<double>(needed_space) / factor) + "GiB\n"
                + std::string("You have ") + format_double_fixed_2(static_cast<double>(free_space) / factor)
                + "GiB free space on " + dir + "\n"
                + "If you are sure you have enough free space rerun the program with -o/--override-size option";
        }
    };

    // Check foremost if enough space for ISO on snapshot_dir
    std::cerr << "=== CHECK ENOUGH SPACE ===" << std::endl;
    std::cerr << "required_space: " << required_space << " KiB (" << format_double_fixed_2(required_space / 1024.0 / 1024.0) << " GiB)" << std::endl;
    std::cerr << "settings.freeSpace: " << settings.freeSpace << " KiB (" << format_double_fixed_2(settings.freeSpace / 1024.0 / 1024.0) << " GiB)" << std::endl;
    std::cerr << "settings.snapshotDir: " << settings.snapshotDir << std::endl;
    std::cerr << "settings.workDir: " << settings.workDir << std::endl;
    
    check_no_space(required_space, settings.freeSpace, settings.snapshotDir);
    if (!out.ok) {
        return out;
    }

    const std::uint64_t workDirDevice = device_id_u64(settings.workDir + "/");
    const std::uint64_t snapshotDirDevice = device_id_u64(settings.snapshotDir + "/");
    std::cerr << "workDir device ID: " << workDirDevice << std::endl;
    std::cerr << "snapshotDir device ID: " << snapshotDirDevice << std::endl;
    
    if (workDirDevice == snapshotDirDevice) {
        std::cerr << "workDir and snapshotDir are on the SAME device" << std::endl;
        if (settings.freeSpace < required_space * 2) {
            std::cerr << "Free space (" << settings.freeSpace << ") < 2x required (" << (required_space * 2) << ")" << std::endl;
            std::cerr << "Trying to move workDir to /tmp or /home..." << std::endl;
            auto check_and_move = [&](const std::string &dir, std::uint64_t req_size) -> bool {
                if (device_id_u64(dir + "/") != device_id_u64(settings.snapshotDir + "/")
                    && FileSystemUtilsCpp::getFreeSpaceKiB(dir) > req_size) {
                    out.shouldMoveWorkDir = true;
                    out.moveWorkDirTo = dir;
                    return true;
                }
                return false;
            };

            if (check_and_move("/tmp", required_space)) {
                return out;
            }
            if (check_and_move("/home", required_space)) {
                return out;
            }

            check_no_space(required_space * 2, settings.freeSpace, settings.snapshotDir);
        }
    } else {
        check_no_space(required_space, settings.freeSpaceWork, settings.workDir);
    }

    return out;
}
