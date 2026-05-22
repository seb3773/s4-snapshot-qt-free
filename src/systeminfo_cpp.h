#pragma once

#include <functional>
#include <string>
#include <vector>

class SystemInfoCpp
{
public:
#ifdef UNIT_TESTS
    struct Hooks {
        std::function<bool()> isLive;
    };

    static void setHooksForTests(const Hooks *hooks);
#endif

    [[nodiscard]] static bool is386();
    [[nodiscard]] static bool isLive();

    // List of users with home directories.
    // Semantics match the existing Qt-based implementation in SystemInfo.
    [[nodiscard]] static std::vector<std::string> listUsers();

    // Read kernel boot options from script.
    // Semantics match the existing Qt-based implementation in SystemInfo.
    [[nodiscard]] static std::string readKernelOpts();
};
