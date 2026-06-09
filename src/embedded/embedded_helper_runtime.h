#pragma once

#include <string>

class EmbeddedHelperRuntime {
public:
    struct Result {
        bool ok = false;
        std::string path;
        std::string error;
    };

    static void setPreferredHelperPath(const std::string &path);

    [[nodiscard]] static std::string defaultHelperPathForWorkDir(const std::string &workDir);
    // Exec-capable state dir ($XDG_RUNTIME_DIR/s4-snapshot or /tmp/s4-snapshot-<uid>).
    [[nodiscard]] static std::string helperStateBaseDir();
    [[nodiscard]] static std::string runtimeScriptsDir();
    [[nodiscard]] static Result ensureHelperAvailable();
    [[nodiscard]] static Result ensureHelperAvailableAt(const std::string &path);
};
