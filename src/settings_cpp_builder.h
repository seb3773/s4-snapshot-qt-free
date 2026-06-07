#pragma once

#include <cstdint>
#include <string>

#include "settings_cpp.h"

#include "command_line_parser_std.h"

struct SettingsArgsCpp
{
    bool monthly = false;
    bool overrideSize = false;
    bool preempt = false;
    std::string fileArg;
    std::uint32_t maxCoresOverride = 0;
};

class SettingsCppBuilder
{
public:
#ifdef UNIT_TESTS
    static void ut_setExcludesSourcePathOverride(const std::string &path);
    static void ut_setFallbackExcludesPathOverride(const std::string &path);
#endif

    [[nodiscard]] static SettingsCpp buildFromArgs(const SettingsArgsCpp &args,
                                                  bool isGuiApp,
                                                  const std::string &appName,
                                                  const std::string &organizationName);

    [[nodiscard]] static SettingsCpp buildFromArgsWithPaths(const SettingsArgsCpp &args,
                                                           bool isGuiApp,
                                                           const std::string &appName,
                                                           const std::string &organizationName,
                                                           const std::string &systemConfigPath,
                                                           const std::string &userConfigBaseDir);

    [[nodiscard]] static SettingsCpp build(const CommandLineParserStd &argParser,
                                          bool isGuiApp,
                                          const std::string &appName,
                                          const std::string &organizationName);
};
