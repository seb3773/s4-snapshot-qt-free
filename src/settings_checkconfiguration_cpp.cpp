#include "settings_checkconfiguration_cpp.h"

#include "file_cpp.h"
#include "process_runner.h"
#include "settings_space_cpp.h"
#include "settings_validation_cpp.h"

#include <cstdint>
#include <vector>

namespace {

static void cb_debug(const SettingsCheckConfigurationCpp::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }
}

static void cb_critical(const SettingsCheckConfigurationCpp::Callbacks &cb, const std::string &text)
{
    if (cb.critical) {
        cb.critical(text);
    }
}

} // namespace

bool SettingsCheckConfigurationCpp::checkConfigurationLikeSettingsQt(const SettingsCpp &settings,
                                                                    bool isCliBuild,
                                                                    bool isGuiApp,
                                                                    const SettingsCheckConfigurationCpp::Callbacks &cb)
{
    cb_debug(cb, std::string("+++ bool Settings::checkConfiguration() const +++"));

    if (!SettingsValidationCpp::checkCompressionLikeSettingsQt(settings)) {
        cb_critical(cb,
                    std::string("Compression format '") + settings.compression
                        + "' is not supported by the current kernel");
        return false;
    }

    if (settings.cores == 0 || settings.cores > settings.maxCores) {
        cb_critical(cb,
                    std::string("Invalid cores setting: ") + std::to_string(settings.cores)
                        + ". Must be between 1 and " + std::to_string(settings.maxCores));
        return false;
    }

    if (settings.throttle > 20) {
        cb_critical(cb,
                    std::string("Invalid throttle setting: ") + std::to_string(settings.throttle)
                        + ". Must be between 0 and 20");
        return false;
    }

    if (settings.snapshotDir.empty()) {
        cb_critical(cb, std::string("Snapshot directory cannot be empty"));
        return false;
    }

    if (settings.snapshotName.empty()) {
        cb_critical(cb, std::string("Snapshot name cannot be empty"));
        return false;
    }

    if (!settings.dataFilesPath.empty()
        && (!FileCpp::isDir(settings.dataFilesPath + "/files")
            || !FileCpp::isDir(settings.dataFilesPath + "/general-files"))) {
        cb_critical(cb, std::string("Live data files directory is invalid: ") + settings.dataFilesPath);
        return false;
    }

    if (!settings.templatesPath.empty()
        && (!FileCpp::exists(settings.templatesPath + "/iso-template.tar.gz")
            || !FileCpp::exists(settings.templatesPath + "/template-initrd.gz"))) {
        cb_critical(cb, std::string("ISO templates directory is invalid: ") + settings.templatesPath);
        return false;
    }

    {
        bool hasInvalid = false;
        for (char ch : settings.snapshotName) {
            switch (ch) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                hasInvalid = true;
                break;
            default:
                break;
            }
            if (hasInvalid) {
                break;
            }
        }
        if (hasInvalid) {
            cb_critical(cb, std::string("Snapshot name contains invalid characters: ") + settings.snapshotName);
            return false;
        }
    }

    if (settings.kernel.empty()) {
        cb_critical(cb, std::string("Kernel version cannot be empty"));
        return false;
    }

    if (!FileCpp::exists(std::string("/boot/vmlinuz-") + settings.kernel)) {
        cb_critical(cb, std::string("Kernel file not found: /boot/vmlinuz-") + settings.kernel);
        return false;
    }

    {
        const std::string configPath = std::string("/boot/config-") + settings.kernel;
        const std::vector<std::string> grepArgs = {"-q", "^CONFIG_SQUASHFS=[ym]", configPath};
        if (ProcessRunner::execute("grep", grepArgs) != 0) {
            cb_critical(cb, std::string("Kernel ") + settings.kernel + " doesn't support Squashfs");
            return false;
        }
    }

    {
        SettingsValidationCpp::Callbacks vcb;
        vcb.debug = cb.debug;
        vcb.critical = cb.critical;
        if (!SettingsValidationCpp::validateExclusionsLikeSettingsQt(settings, vcb)) {
            return false;
        }
    }

    {
        const bool shouldValidateSpace = isCliBuild || (!isGuiApp);
        if (shouldValidateSpace) {
            SettingsSpaceCpp::Callbacks scb;
            scb.debug = cb.debug;
            scb.critical = cb.critical;
            if (!SettingsSpaceCpp::validateSpaceRequirementsLikeSettingsQt(settings, scb)) {
                return false;
            }
        }
    }

    cb_debug(cb, std::string("Configuration validation passed"));
    return true;
}
