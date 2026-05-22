#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "settings_cpp.h"

class BatchprocessingCpp
{
public:
    struct ExcludesPromptResult {
        bool prompted = false;
        bool shouldExit = false;
        int exitCode = 0;
    };

    struct ExcludesDiffResult {
        bool sourceIsNewer = false;
        std::string diffOutput;
    };

    [[nodiscard]] static std::string colorizeDiffAnsi(const std::string &diff);

    [[nodiscard]] static ExcludesDiffResult isSourceExcludesNewer(const SettingsCpp &settings);

    [[nodiscard]] static bool resetCustomExcludesCli(const std::string &configuredPath,
                                                    const std::string &sourcePath,
                                                    std::string *errorOut);

    [[nodiscard]] static bool touchMtimeNowLikeQtCurrentSecs(const std::string &path);

    [[nodiscard]] static ExcludesPromptResult checkUpdatedDefaultExcludesCli(const SettingsCpp &settings,
                                                                            FILE *in,
                                                                            FILE *out);
};
