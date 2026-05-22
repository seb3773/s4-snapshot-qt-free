#pragma once

#include <QtCore/QString>

class FileSystemUtils
{
public:
    // Get free space in KiB for a given path
    [[nodiscard]] static quint64 getFreeSpace(const QString &path);

    [[nodiscard]] static quint64 bytesTotal(const QString &path);
    [[nodiscard]] static quint64 bytesFree(const QString &path);
    [[nodiscard]] static quint64 bytesAvailable(const QString &path);

    // Stable device id for comparing whether two paths are on same filesystem.
    // Returns 0 on failure.
    [[nodiscard]] static quint64 deviceId(const QString &path);

    // Returns filesystem type (e.g. "ext4") or empty string on failure.
    [[nodiscard]] static QString fileSystemType(const QString &path);

    // True when the provided path is a mount point.
    [[nodiscard]] static bool isMountPoint(const QString &path);

    // Get formatted free space string for display
    [[nodiscard]] static QString getFreeSpaceString(const QString &path);

    // Check if directory is on a supported partition type
    [[nodiscard]] static bool isOnSupportedPartition(const QString &dir);

    // Return directory with larger free space between two options
    [[nodiscard]] static QString largerFreeSpace(const QString &dir1, const QString &dir2);

    // Return directory with largest free space among three options
    [[nodiscard]] static QString largerFreeSpace(const QString &dir1, const QString &dir2, const QString &dir3);

private:
    // Filesystems lacking POSIX permissions/ownership support
    [[nodiscard]] static bool isUnsupportedPartitionType(QStringView type);
};
