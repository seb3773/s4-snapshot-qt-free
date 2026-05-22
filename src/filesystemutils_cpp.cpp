#include "filesystemutils_cpp.h"

#include <cerrno>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <array>

#include <fcntl.h>
#include <linux/stat.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/statvfs.h>

namespace {

#ifdef UNIT_TESTS
static const FileSystemUtilsCpp::Hooks *g_hooks = nullptr;
#endif

constexpr std::array<const char *, 7> kUnsupportedPartitions = {
    "fat",
    "vfat",
    "msdos",
    "exfat",
    "ntfs",
    "ntfs3",
    "ntfs-3g",
};

bool isUnsupportedPartitionType(const std::string &type)
{
    if (type.empty()) {
        return false;
    }
    for (const char *p : kUnsupportedPartitions) {
        if (type == p) {
            return true;
        }
    }
    return false;
}

bool statvfs_ok(const std::string &path, struct statvfs *out)
{
    if (!out) {
        return false;
    }
    errno = 0;
    return ::statvfs(path.c_str(), out) == 0;
}

bool stat_ok(const std::string &path, struct stat *out)
{
    if (!out) {
        return false;
    }
    errno = 0;
    return ::stat(path.c_str(), out) == 0;
}

std::string normalizeMountPath(std::string p)
{
    if (p.empty()) {
        return p;
    }

    while (p.size() > 1 && !p.empty() && p.back() == '/') {
        p.pop_back();
    }
    return p;
}

inline bool isDigit(char c)
{
    return (c >= '0' && c <= '9');
}

inline int octalDigit(char c)
{
    if (c < '0' || c > '7') {
        return -1;
    }
    return (c - '0');
}

std::string decodeKernelMangledPath(const std::string &path)
{
    // Equivalent intent to Qt6 parseMangledPath(): the kernel escapes characters as \XYZ (octal).
    // In mountinfo this covers: space, tab, backslash, newline.
    // We decode any valid sequence \[0-7][0-7][0-7].
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 3 >= path.size()) {
            return {};
        }
        const int d1 = octalDigit(path[i + 1]);
        const int d2 = octalDigit(path[i + 2]);
        const int d3 = octalDigit(path[i + 3]);
        if (d1 < 0 || d2 < 0 || d3 < 0) {
            return {};
        }
        const unsigned char decoded = static_cast<unsigned char>((d1 << 6) | (d2 << 3) | d3);
        out.push_back(static_cast<char>(decoded));
        i += 3;
    }
    return out;
}

bool isParentOf(const std::string &parent, const std::string &child)
{
    if (parent.empty()) {
        return false;
    }
    if (parent == "/") {
        return !child.empty() && child[0] == '/';
    }
    if (child.size() < parent.size()) {
        return false;
    }
    if (child.compare(0, parent.size(), parent) != 0) {
        return false;
    }
    if (child.size() == parent.size()) {
        return true;
    }
    return child[parent.size()] == '/';
}

struct MountInfo
{
    std::uint64_t mntId = 0;
    dev_t stDev = 0;
    std::string mountPoint;
    std::string fsType;
};

std::vector<MountInfo> parseMountInfo()
{
    std::FILE *f = std::fopen("/proc/self/mountinfo", "r");
    if (!f) {
        return {};
    }

    std::vector<MountInfo> infos;
    char line[16384];
    while (std::fgets(line, static_cast<int>(sizeof(line)), f)) {
        std::string raw(line);
        while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) {
            raw.pop_back();
        }

        // mountinfo format (simplified):
        // mntid parentid major:minor root mount_point options... - fstype source superoptions...
        // We need: mntid, major:minor, mount_point, fstype
        std::istringstream iss(raw);
        std::string mntIdStr;
        std::string parentIdStr;
        std::string devno;
        std::string fsRoot;
        std::string mountPoint;
        if (!(iss >> mntIdStr >> parentIdStr >> devno >> fsRoot >> mountPoint)) {
            continue;
        }

        // Skip until " - " separator, then read fstype
        std::string tok;
        bool foundSep = false;
        while (iss >> tok) {
            if (tok == "-") {
                foundSep = true;
                break;
            }
        }
        if (!foundSep) {
            continue;
        }
        std::string fsType;
        if (!(iss >> fsType)) {
            continue;
        }

        const std::string decodedMountPoint = decodeKernelMangledPath(mountPoint);
        if (decodedMountPoint.empty()) {
            continue;
        }

        const std::string decodedDevNo = devno;
        const std::size_t colon = decodedDevNo.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string majorStr = decodedDevNo.substr(0, colon);
        const std::string minorStr = decodedDevNo.substr(colon + 1);
        if (majorStr.empty() || minorStr.empty()) {
            continue;
        }
        if (!std::all_of(majorStr.begin(), majorStr.end(), isDigit)
            || !std::all_of(minorStr.begin(), minorStr.end(), isDigit)) {
            continue;
        }
        const unsigned long majorNo = std::strtoul(majorStr.c_str(), nullptr, 10);
        const unsigned long minorNo = std::strtoul(minorStr.c_str(), nullptr, 10);

        MountInfo info;
        if (mntIdStr.empty() || !std::all_of(mntIdStr.begin(), mntIdStr.end(), isDigit)) {
            continue;
        }
        info.mntId = static_cast<std::uint64_t>(std::strtoull(mntIdStr.c_str(), nullptr, 10));
        info.stDev = makedev(static_cast<unsigned>(majorNo), static_cast<unsigned>(minorNo));
        info.mountPoint = normalizeMountPath(decodedMountPoint);
        info.fsType = fsType;
        infos.push_back(std::move(info));
    }

    std::fclose(f);
    return infos;
}

std::uint64_t mountIdForPath(const std::string &path)
{
    // Qt6 uses statx(fd, "", AT_EMPTY_PATH|..., STATX_MNT_ID, ...) if available.
    // We use statx(path, ..., STATX_MNT_ID, ...) when possible; otherwise return 0.
#if defined(STATX_BASIC_STATS) && defined(STATX_MNT_ID)
    struct statx stx;
    if (::statx(AT_FDCWD, path.c_str(), AT_NO_AUTOMOUNT, STATX_MNT_ID, &stx) == 0) {
        if (stx.stx_mask & STATX_MNT_ID) {
            return static_cast<std::uint64_t>(stx.stx_mnt_id);
        }
    }
#endif
    return 0;
}

const MountInfo *bestMountInfoForPath(const std::string &path)
{
    const std::string canonical = normalizeMountPath(path);
    if (canonical.empty()) {
        return nullptr;
    }

    static thread_local std::vector<MountInfo> cached;
    cached = parseMountInfo();
    if (cached.empty()) {
        return nullptr;
    }

    if (const std::uint64_t mntId = mountIdForPath(canonical); mntId != 0) {
        for (const auto &mi : cached) {
            if (mi.mntId == mntId) {
                return &mi;
            }
        }
    }

    // Fallback: reverse scan for parent mountpoint; prefer matching device ID if possible.
    struct stat st;
    const dev_t devId = stat_ok(canonical, &st) ? st.st_dev : 0;

    const MountInfo *best = nullptr;
    for (auto it = cached.rbegin(); it != cached.rend(); ++it) {
        if (!isParentOf(it->mountPoint, canonical)) {
            continue;
        }
        if (devId != 0 && it->stDev == devId) {
            return &*it;
        }
        if (!best) {
            best = &*it;
        }
    }
    return best;
}

} // namespace

std::uint64_t FileSystemUtilsCpp::bytesTotal(const std::string &path)
{
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->bytesTotal) {
        return g_hooks->bytesTotal(path);
    }
#endif
    struct statvfs vfs;
    if (!statvfs_ok(path, &vfs)) {
        return 0;
    }
    return static_cast<std::uint64_t>(vfs.f_blocks) * static_cast<std::uint64_t>(vfs.f_frsize);
}

std::uint64_t FileSystemUtilsCpp::bytesFree(const std::string &path)
{
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->bytesFree) {
        return g_hooks->bytesFree(path);
    }
#endif
    struct statvfs vfs;
    if (!statvfs_ok(path, &vfs)) {
        return 0;
    }
    return static_cast<std::uint64_t>(vfs.f_bfree) * static_cast<std::uint64_t>(vfs.f_frsize);
}

std::uint64_t FileSystemUtilsCpp::bytesAvailable(const std::string &path)
{
    struct statvfs vfs;
    if (!statvfs_ok(path, &vfs)) {
        return 0;
    }
    return static_cast<std::uint64_t>(vfs.f_bavail) * static_cast<std::uint64_t>(vfs.f_frsize);
}

std::uint64_t FileSystemUtilsCpp::getFreeSpaceKiB(const std::string &path)
{
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->getFreeSpaceKiB) {
        return g_hooks->getFreeSpaceKiB(path);
    }
#endif
    struct statvfs vfs;
    const std::string p = path + "/";
    if (!statvfs_ok(p, &vfs)) {
        return 0;
    }
    if ((vfs.f_flag & ST_RDONLY) != 0) {
        return 0;
    }
    return bytesAvailable(p) / 1024;
}

#ifdef UNIT_TESTS
void FileSystemUtilsCpp::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}
#endif

std::uint64_t FileSystemUtilsCpp::deviceId(const std::string &path)
{
    struct stat st;
    if (!stat_ok(path, &st)) {
        return 0;
    }
    return static_cast<std::uint64_t>(st.st_dev);
}

std::string FileSystemUtilsCpp::fileSystemType(const std::string &path)
{
    const MountInfo *mi = bestMountInfoForPath(path);
    if (!mi) {
        return {};
    }
    return mi->fsType;
}

bool FileSystemUtilsCpp::isMountPoint(const std::string &path)
{
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->isMountPoint) {
        return g_hooks->isMountPoint(path);
    }
#endif
    const std::string normalized = normalizeMountPath(path);
    if (normalized.empty()) {
        return false;
    }
    const MountInfo *mi = bestMountInfoForPath(path);
    if (!mi) {
        return false;
    }
    return mi->mountPoint == normalized;
}

bool FileSystemUtilsCpp::isOnSupportedPartition(const std::string &dir)
{
    const std::string partType = fileSystemType(dir + "/");
    return !isUnsupportedPartitionType(partType);
}

std::string FileSystemUtilsCpp::largerFreeSpace(const std::string &dir1, const std::string &dir2)
{
    if (deviceId(dir1 + "/") == deviceId(dir2 + "/")) {
        return dir1;
    }
    const std::uint64_t dir1Free = getFreeSpaceKiB(dir1);
    const std::uint64_t dir2Free = getFreeSpaceKiB(dir2);
    return dir1Free >= dir2Free ? dir1 : dir2;
}

std::string FileSystemUtilsCpp::largerFreeSpace(const std::string &dir1, const std::string &dir2, const std::string &dir3)
{
    return largerFreeSpace(largerFreeSpace(dir1, dir2), dir3);
}

#ifdef UNIT_TESTS
std::string FileSystemUtilsCpp::_decodeKernelMangledPathForTests(const std::string &path)
{
    return decodeKernelMangledPath(path);
}
#endif
