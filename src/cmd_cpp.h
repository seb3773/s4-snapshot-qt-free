#pragma once

#include <string>
#include <vector>

class CmdCpp
{
public:
    enum class QuietMode { No, Yes };
    enum class Elevation { No, Yes };

    CmdCpp();

    // Returns the elevation tool path appropriate for CLI mode.
    // Empty if none found.
    [[nodiscard]] static std::string elevationTool();
    [[nodiscard]] static bool isCliMode();
    [[nodiscard]] static std::string loggedInUserName();

    [[nodiscard]] bool proc(const std::string &program,
                            const std::vector<std::string> &args = {},
                            std::string *output = nullptr,
                            const std::string *input = nullptr,
                            QuietMode quiet = QuietMode::No,
                            Elevation elevation = Elevation::No);

    [[nodiscard]] bool procAsRoot(const std::string &program,
                                 const std::vector<std::string> &args = {},
                                 std::string *output = nullptr,
                                 const std::string *input = nullptr,
                                 QuietMode quiet = QuietMode::No);

    [[nodiscard]] std::string getOut(const std::string &cmd, QuietMode quiet = QuietMode::No);

    [[nodiscard]] std::string getOutAsRoot(const std::string &program,
                                          const std::vector<std::string> &args = {},
                                          QuietMode quiet = QuietMode::No);

    [[nodiscard]] std::string readAllOutput() const;

    [[nodiscard]] bool run(const std::string &cmd, QuietMode quiet = QuietMode::No);

private:
    std::string elevationToolPath;
    std::string helperPath;
    std::string outBuffer;
    int lastExitCode = 0;
    bool lastNormalExit = true;

    static constexpr int EXIT_CODE_COMMAND_NOT_FOUND = 127;
    static constexpr int EXIT_CODE_PERMISSION_DENIED = 126;

    [[nodiscard]] bool helperProc(const std::vector<std::string> &helperArgs,
                                 std::string *output = nullptr,
                                 const std::string *input = nullptr,
                                 QuietMode quiet = QuietMode::No);

    [[nodiscard]] static std::vector<std::string> helperExecArgs(const std::string &program,
                                                                 const std::vector<std::string> &args);

    [[nodiscard]] static std::string trimLikeQString(const std::string &s);

    void handleElevationError() const;
};
