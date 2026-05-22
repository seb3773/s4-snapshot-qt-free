#include "batchprocessing_cpp.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <utime.h>

#include "app_translator_cpp.h"
#include "datetime_cpp.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "process_runner.h"
#include "stdio_cpp.h"

std::string BatchprocessingCpp::colorizeDiffAnsi(const std::string &diff)
{
    const std::string green = "\033[32m";
    const std::string red = "\033[31m";
    const std::string blue = "\033[34m";
    const std::string gray = "\033[90m";
    const std::string reset = "\033[0m";

    std::string out;
    out.reserve(diff.size() + diff.size() / 4);

    std::size_t i = 0;
    while (i <= diff.size()) {
        if (i == diff.size()) {
            out.push_back('\n');
            break;
        }
        const std::size_t nl = diff.find('\n', i);
        const bool hasNl = (nl != std::string::npos);
        const std::size_t end = hasNl ? nl : diff.size();
        const std::string line = diff.substr(i, end - i);

        if (line.rfind("+++ ", 0) == 0 || line.rfind("--- ", 0) == 0) {
            out += gray;
            out += line;
            out += reset;
        } else if (line.rfind("@@", 0) == 0) {
            out += blue;
            out += line;
            out += reset;
        } else if (!line.empty() && line[0] == '+') {
            out += green;
            out += line;
            out += reset;
        } else if (!line.empty() && line[0] == '-') {
            out += red;
            out += line;
            out += reset;
        } else {
            out += line;
        }

        if (hasNl) {
            out.push_back('\n');
            i = nl + 1;
        } else {
            break;
        }
    }

    return out;
}

BatchprocessingCpp::ExcludesDiffResult BatchprocessingCpp::isSourceExcludesNewer(const SettingsCpp &settings)
{
    ExcludesDiffResult out;

    const std::string configuredPath = settings.snapshotExcludesPath;
    const std::string sourcePath = settings.excludesSourcePath;

    if (sourcePath.empty() || configuredPath.empty()) {
        return out;
    }

    const bool configuredExists = FileCpp::exists(configuredPath);
    const bool sourceExists = FileCpp::exists(sourcePath);
    if (!configuredExists || !sourceExists) {
        return out;
    }

    const std::int64_t configuredMtime = FileCpp::lastModifiedSecsSinceEpoch(configuredPath);
    const std::int64_t sourceMtime = FileCpp::lastModifiedSecsSinceEpoch(sourcePath);

    if (sourceMtime <= configuredMtime) {
        return out;
    }

    const ProcessRunner::Result r = ProcessRunner::run(
        "diff",
        {"--unified", configuredPath, sourcePath},
        std::string(),
        30000);

    if (!r.started) {
        return out;
    }

    if (r.exitCode == 0) {
        return out;
    }

    if (r.exitCode != 1) {
        return out;
    }

    out.sourceIsNewer = true;
    out.diffOutput = !r.stdoutText.empty() ? r.stdoutText : r.stderrText;
    if (out.diffOutput.empty()) {
        out.diffOutput = "No diff output available.";
    }

    return out;
}

bool BatchprocessingCpp::resetCustomExcludesCli(const std::string &configuredPath,
                                               const std::string &sourcePath,
                                               std::string *errorOut)
{
    if (errorOut) {
        errorOut->clear();
    }

    if (sourcePath.empty() || configuredPath.empty()) {
        if (errorOut) {
            *errorOut = "empty path";
        }
        return false;
    }

    if (!FileCpp::exists(sourcePath)) {
        if (errorOut) {
            *errorOut = "source not found";
        }
        return false;
    }

    const std::string targetDir = DirCpp::absolutePathOfContainingDir(configuredPath);
    if (!targetDir.empty()) {
        (void)DirCpp().mkpath(targetDir);
    }

    if (FileCpp::exists(configuredPath)) {
        const std::string stamp = DateTimeCpp::nowLocalYmdHmsMillis();
        std::string stampDigits;
        stampDigits.reserve(stamp.size());
        for (char ch : stamp) {
            if (ch >= '0' && ch <= '9') {
                stampDigits.push_back(ch);
            }
        }
        if (stampDigits.size() >= 14) {
            stampDigits.resize(14);
        }
        const std::string backupPath = configuredPath + "." + stampDigits;

        (void)FileCpp::copy(configuredPath, backupPath);

        if (!FileCpp::remove(configuredPath)) {
            if (errorOut) {
                *errorOut = std::string("remove failed: ") + std::strerror(errno);
            }
            return false;
        }
    }

    if (!FileCpp::copy(sourcePath, configuredPath)) {
        if (errorOut) {
            *errorOut = "copy failed";
        }
        return false;
    }

    return true;
}

bool BatchprocessingCpp::touchMtimeNowLikeQtCurrentSecs(const std::string &path)
{
    if (path.empty()) {
        return false;
    }

    struct utimbuf times {};
    times.actime = static_cast<time_t>(FileCpp::lastReadSecsSinceEpoch(path));
    times.modtime = ::time(nullptr);
    return ::utime(path.c_str(), &times) == 0;
}

namespace {

static std::string trim_ascii_like_qstring_trimmed(std::string s)
{
    auto is_space = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };

    std::size_t begin = 0;
    while (begin < s.size() && is_space(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && is_space(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

static bool iequals_ascii(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        const unsigned char la = (ca >= 'A' && ca <= 'Z') ? static_cast<unsigned char>(ca - 'A' + 'a') : ca;
        const unsigned char lb = (cb >= 'A' && cb <= 'Z') ? static_cast<unsigned char>(cb - 'A' + 'a') : cb;
        if (la != lb) {
            return false;
        }
    }
    return true;
}

static std::string format_percent_1_to_8(std::string s,
                                        const std::string &a1,
                                        const std::string &a2,
                                        const std::string &a3,
                                        const std::string &a4,
                                        const std::string &a5,
                                        const std::string &a6,
                                        const std::string &a7,
                                        const std::string &a8)
{
    const std::string args[8] = {a1, a2, a3, a4, a5, a6, a7, a8};
    for (int n = 1; n <= 8; ++n) {
        const std::string needle = std::string("%") + std::to_string(n);
        std::string::size_type pos = 0;
        while ((pos = s.find(needle, pos)) != std::string::npos) {
            s.replace(pos, needle.size(), args[n - 1]);
            pos += args[n - 1].size();
        }
    }
    return s;
}

} // namespace

BatchprocessingCpp::ExcludesPromptResult BatchprocessingCpp::checkUpdatedDefaultExcludesCli(const SettingsCpp &settings,
                                                                                           FILE *in,
                                                                                           FILE *out)
{
    ExcludesPromptResult r;

    const ExcludesDiffResult diff = isSourceExcludesNewer(settings);
    if (!diff.sourceIsNewer) {
        return r;
    }
    r.prompted = true;

    const std::string configuredPath = settings.snapshotExcludesPath;
    const std::string sourcePath = settings.excludesSourcePath;

    const std::string showOptionKey = AppTranslatorCpp::tQt(
        "QObject",
        "s",
        "CLI excludes prompt: single-letter shortcut for 'show diff'");
    const std::string useOptionKey = AppTranslatorCpp::tQt(
        "QObject",
        "u",
        "CLI excludes prompt: single-letter shortcut for 'use updated default'");
    const std::string keepOptionKey = AppTranslatorCpp::tQt(
        "QObject",
        "k",
        "CLI excludes prompt: single-letter shortcut for 'keep custom (update timestamp)'");
    const std::string quitOptionKey = AppTranslatorCpp::tQt(
        "QObject",
        "q",
        "CLI excludes prompt: single-letter shortcut for 'quit'");

    const std::string showOptionText = AppTranslatorCpp::tQt(
        "QObject",
        "show diff",
        "CLI excludes prompt option label");
    const std::string useOptionText = AppTranslatorCpp::tQt(
        "QObject",
        "use updated default",
        "CLI excludes prompt option label");
    const std::string keepOptionText = AppTranslatorCpp::tQt(
        "QObject",
        "keep custom (update timestamp)",
        "CLI excludes prompt option label");
    const std::string quitOptionText = AppTranslatorCpp::tQt(
        "QObject",
        "quit",
        "CLI excludes prompt option label");

    const std::string optionPrompt = format_percent_1_to_8(
        AppTranslatorCpp::tQt("QObject", "[%1]%2  [%3]%4  [%5]%6  [%7]%8: "),
        showOptionKey,
        showOptionText,
        useOptionKey,
        useOptionText,
        keepOptionKey,
        keepOptionText,
        quitOptionKey,
        quitOptionText);

    while (true) {
        (void)StdioCpp::write(out,
                              format_percent_1_to_8(
                                  AppTranslatorCpp::tQt(
                                      "QObject",
                                      "The exclusion file at %1 is newer than your configured file at %2."),
                                  sourcePath,
                                  configuredPath,
                                  std::string(),
                                  std::string(),
                                  std::string(),
                                  std::string(),
                                  std::string(),
                                  std::string()));
        (void)StdioCpp::write(out, "\n");
        (void)StdioCpp::write(out, optionPrompt);
        (void)StdioCpp::flush(out);

        std::string response = trim_ascii_like_qstring_trimmed(StdioCpp::readLine(in));

        if (iequals_ascii(response, showOptionKey) || iequals_ascii(response, showOptionText)) {
            (void)StdioCpp::write(out, colorizeDiffAnsi(diff.diffOutput));
            (void)StdioCpp::flush(out);
            continue;
        }

        if (iequals_ascii(response, useOptionKey) || iequals_ascii(response, useOptionText)) {
            std::string err;
            (void)resetCustomExcludesCli(configuredPath, sourcePath, &err);
            return r;
        }

        if (iequals_ascii(response, keepOptionKey) || iequals_ascii(response, keepOptionText)) {
            (void)touchMtimeNowLikeQtCurrentSecs(configuredPath);
            return r;
        }

        if (iequals_ascii(response, quitOptionKey) || iequals_ascii(response, quitOptionText) || response.empty()) {
            r.shouldExit = true;
            const bool debugStop = std::getenv("MX_SNAPSHOT_EXCLUDES_DEBUG_STOP") != nullptr;
            r.exitCode = debugStop ? 0 : EXIT_SUCCESS;
            return r;
        }

        (void)StdioCpp::write(out, AppTranslatorCpp::tQt("QObject", "Invalid choice. Please select again."));
        (void)StdioCpp::write(out, "\n");
    }
}
