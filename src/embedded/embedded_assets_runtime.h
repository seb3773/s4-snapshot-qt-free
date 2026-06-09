#pragma once

#include <functional>
#include <string>

struct SettingsCpp;

class EmbeddedAssetsRuntime {
public:
    struct Result {
        bool ok = false;
        std::string error;
    };

    struct Callbacks {
        std::function<void(const std::string &text)> critical;
    };

    [[nodiscard]] static std::string embeddedRoot(const std::string &workDir);
    [[nodiscard]] static std::string liveFilesDir(const std::string &workDir);
    [[nodiscard]] static std::string runtimeScriptsDir(const std::string &workDir);
    [[nodiscard]] static std::string initrdBuildDir(const std::string &workDir);
    // template-initrd ships usr -> . ; scripts live under share/ on disk but appear as /usr/share/ at runtime.
    [[nodiscard]] static std::string initrdScriptsDir(const std::string &initrdBuildDir);

    [[nodiscard]] static Result extractRuntimeScriptsTo(const std::string &destRoot);

    [[nodiscard]] static Result prepareWorkspace(SettingsCpp &settings, const Callbacks &cb);

    [[nodiscard]] static bool signalStopRequested();
    [[nodiscard]] static std::string signalStopWorkDir();
    static void requestStop();
    [[nodiscard]] static bool handleSignalStopIfRequested();

    static void installSignalCleanup(const std::string &workDir);
    static void clearSignalCleanup();
    static void cleanupEmbeddedTree(const std::string &workDir);
};
