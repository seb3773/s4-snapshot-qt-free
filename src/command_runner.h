#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class CommandRunner
{
public:
    enum class QuietMode { No, Yes };
    enum class Elevation { No, Yes };

    struct Result {
        bool started = false;
        int exitCode = 1;
        bool normalExit = true;
        std::string stdoutText;
        std::string stderrText;
        std::string mergedText;
    };

    struct Hooks {
        std::function<Result(const std::string &cmd, const std::vector<std::string> &args, const std::string &stdinText,
                             QuietMode quiet, Elevation elevation)>
            proc;
        std::function<std::string()> elevationTool;
        std::function<std::string()> loggedInUserName;
    };

    struct GuiElevationHooks {
        std::function<std::string()> askPassword;
        std::function<void()> invalidateCachedPassword;
    };

    static void setHooksForTests(const Hooks *hooks);
    static void setGuiElevationHooks(const GuiElevationHooks *hooks);
    static void clearGuiElevationHooks();

    static std::string elevationTool();
    static bool isCliMode();
    static std::string loggedInUserName();

    [[nodiscard]] static Result proc(const std::string &cmd, const std::vector<std::string> &args,
                                    const std::string &stdinText, QuietMode quiet = QuietMode::No,
                                    Elevation elevation = Elevation::No);

    [[nodiscard]] static Result procAsRoot(const std::string &cmd, const std::vector<std::string> &args,
                                          const std::string &stdinText, QuietMode quiet = QuietMode::No);

    [[nodiscard]] static std::string getOut(const std::string &cmd, QuietMode quiet = QuietMode::No);

    [[nodiscard]] static std::string getOutAsRoot(const std::string &cmd, const std::vector<std::string> &args,
                                                 QuietMode quiet = QuietMode::No);

    [[nodiscard]] static bool run(const std::string &cmd, QuietMode quiet = QuietMode::No);

private:
    static Result helperProc(const std::vector<std::string> &helperArgs, const std::string &stdinText,
                             QuietMode quiet);
    static std::vector<std::string> helperExecArgs(const std::string &cmd, const std::vector<std::string> &args);
    static void handleElevationError();
};
