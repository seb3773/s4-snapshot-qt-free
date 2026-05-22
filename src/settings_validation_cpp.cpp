#include "settings_validation_cpp.h"

#include <algorithm>
#include <cctype>

#include "file_cpp.h"
#include "process_runner.h"

namespace {

static void cb_debug(const SettingsValidationCpp::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }
}

static void cb_critical(const SettingsValidationCpp::Callbacks &cb, const std::string &text)
{
    if (cb.critical) {
        cb.critical(text);
    }
}

} // namespace

bool SettingsValidationCpp::validateExclusionsLikeSettingsQt(const SettingsCpp &settings, const Callbacks &cb)
{
    cb_debug(cb, std::string("+++ bool Settings::validateExclusions() const +++"));

    if (!settings.snapshotExcludesPath.empty() && !FileCpp::exists(settings.snapshotExcludesPath)) {
        cb_critical(cb, std::string("Exclusion file does not exist: ") + settings.snapshotExcludesPath);
        return false;
    }

    if (!settings.sessionExcludes.empty()) {
        const std::size_t quoteCount = static_cast<std::size_t>(
            std::count(settings.sessionExcludes.begin(), settings.sessionExcludes.end(), '"'));
        if (quoteCount % 2 != 0) {
            cb_critical(cb, std::string("Unbalanced quotes in exclusion list"));
            return false;
        }
    }

    cb_debug(cb, std::string("Exclusion validation passed"));
    return true;
}

bool SettingsValidationCpp::checkCompressionLikeSettingsQt(const SettingsCpp &settings)
{
    if (settings.compression == "gzip") {
        return true;
    }

    if (settings.kernel.empty()) {
        return true;
    }

    const std::string configPath = std::string("/boot/config-") + settings.kernel;
    if (!FileCpp::exists(configPath)) {
        return true;
    }

    std::string compressionUpper = settings.compression;
    for (char &c : compressionUpper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    const std::string pattern = std::string("^CONFIG_SQUASHFS_") + compressionUpper + "=y";
    return ProcessRunner::execute("grep", {"-q", pattern, configPath}) == 0;
}
