#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class WorkCppUtils
{
public:
    enum class ReplaceStringError { None, OpenFailed, WriteFailed };

    struct Hooks {
        std::function<bool(const std::string &filePath, const std::string &text)> writeTextFileUtf8NoBomTruncate;
        std::function<ReplaceStringError(const std::string &filePath, const std::string &oldText, const std::string &newText)>
            replaceStringInFileUtf8NoBom;
        std::function<bool(const std::string &filePath, const std::string &key, const std::string &value)>
            writeQSettingsNativeGeneralString;
        std::function<std::string(const std::string &executableName, const std::vector<std::string> &paths)> findExecutable;
    };

    static void setHooksForTests(const Hooks *hooks);

    [[nodiscard]] static bool checkInstalled(const std::string &package);
    [[nodiscard]] static std::string findExecutable(const std::string &executableName,
                                                   const std::vector<std::string> &paths = {});
    [[nodiscard]] static bool isEnvironmentReady(const std::string &workDir, const std::string &snapshotDir);
    [[nodiscard]] static std::uint64_t parseDuKilobytes(const std::string &output, bool *ok);
    [[nodiscard]] static bool writeTextFileUtf8NoBomTruncate(const std::string &filePath, const std::string &text);

    [[nodiscard]] static ReplaceStringError replaceStringInFileUtf8NoBom(
        const std::string &filePath,
        const std::string &oldText,
        const std::string &newText);

    [[nodiscard]] static bool writeQSettingsNativeGeneralString(const std::string &filePath,
                                                               const std::string &key,
                                                               const std::string &value);

    [[nodiscard]] static std::string buildLsbReleaseContent(const std::string &projectName,
                                                           const std::string &distroVersion,
                                                           const std::string &codename);

    [[nodiscard]] static bool writeLinuxfsInfoFromMksquashfsOutput(const std::string &mksquashfsOutput,
                                                                   const std::string &linuxfsInfoPath);

    [[nodiscard]] static bool replaceMenuStrings(const std::string &workDir,
                                                const std::string &projectName,
                                                const std::string &distroVersion,
                                                const std::string &fullDistroName,
                                                const std::string &releaseDate,
                                                const std::string &codename,
                                                const std::string &bootOptions);
};
