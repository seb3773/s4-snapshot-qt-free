#pragma once

#include <cstdint>
#include <functional>
#include <string>

class FileSystemUtilsCpp
{
public:
    [[nodiscard]] static std::uint64_t bytesTotal(const std::string &path);
    [[nodiscard]] static std::uint64_t bytesFree(const std::string &path);
    [[nodiscard]] static std::uint64_t bytesAvailable(const std::string &path);

    [[nodiscard]] static std::uint64_t getFreeSpaceKiB(const std::string &path);

    [[nodiscard]] static std::uint64_t deviceId(const std::string &path);

    [[nodiscard]] static std::string fileSystemType(const std::string &path);

    [[nodiscard]] static bool isMountPoint(const std::string &path);

    [[nodiscard]] static bool isOnSupportedPartition(const std::string &dir);

    [[nodiscard]] static std::string largerFreeSpace(const std::string &dir1, const std::string &dir2);
    [[nodiscard]] static std::string largerFreeSpace(const std::string &dir1, const std::string &dir2, const std::string &dir3);

#ifdef UNIT_TESTS
    struct Hooks {
        std::function<std::uint64_t(const std::string &path)> bytesTotal;
        std::function<std::uint64_t(const std::string &path)> bytesFree;
        std::function<bool(const std::string &path)> isMountPoint;
        std::function<std::uint64_t(const std::string &path)> getFreeSpaceKiB;
    };

    static void setHooksForTests(const Hooks *hooks);

    [[nodiscard]] static std::string _decodeKernelMangledPathForTests(const std::string &path);
#endif
};
