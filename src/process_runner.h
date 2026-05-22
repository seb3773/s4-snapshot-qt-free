#pragma once

#include <functional>
#include <string>
#include <vector>

class ProcessRunner
{
public:
    enum class ExitStatus { NormalExit, CrashExit, FailedToStart };

    struct Result {
        bool started = false;
        ExitStatus exitStatus = ExitStatus::FailedToStart;
        int exitCode = -1;
        std::string stdoutText;
        std::string stderrText;
    };

    struct Hooks {
        std::function<Result(const std::string &program, const std::vector<std::string> &args, const std::string &stdinText,
                             int timeout_ms)>
            run;
        std::function<Result(const std::string &program, const std::vector<std::string> &args, const std::string &stdinText,
                             const std::function<void(const char *, size_t)> &onStdout,
                             const std::function<void(const char *, size_t)> &onStderr, int timeout_ms)>
            runStreaming;
        std::function<int(const std::string &program, const std::vector<std::string> &args, int timeout_ms)> execute;
    };

    static void setHooksForTests(const Hooks *hooks);

    // timeout_ms: -1 means no timeout
    [[nodiscard]] static Result run(const std::string &program, const std::vector<std::string> &args,
                                   const std::string &stdinText = std::string(), int timeout_ms = -1);

    // Like run(), but streams stdout/stderr chunks as they are read.
    // The callbacks may be empty.
    [[nodiscard]] static Result runStreaming(const std::string &program, const std::vector<std::string> &args,
                                            const std::string &stdinText,
                                            const std::function<void(const char *, size_t)> &onStdout,
                                            const std::function<void(const char *, size_t)> &onStderr,
                                            int timeout_ms = -1);

    [[nodiscard]] static int execute(const std::string &program, const std::vector<std::string> &args,
                                    int timeout_ms = -1);
};
