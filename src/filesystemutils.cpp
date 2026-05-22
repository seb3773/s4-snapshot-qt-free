#include "filesystemutils.h"

#include <QtCore/QDebug>

#include <array>
#include <cerrno>

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <cstdio>

namespace {

constexpr std::array<const char *, 7> kUnsupportedPartitions = {
    "fat",
    "vfat",
    "msdos",
    "exfat",
    "ntfs",
    "ntfs3",
    "ntfs-3g",
};

} // namespace

namespace {

QStringList split_whitespace_skip_empty(const QString &s)
{
    QStringList out;
    QString cur;
    cur.reserve(s.size());

    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (ch.isSpace()) {
            if (!cur.isEmpty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.append(ch);
        }
    }
    if (!cur.isEmpty()) {
        out.push_back(cur);
    }
    return out;
}

bool statvfs_ok(const QString &path, struct statvfs *out)
{
    if (!out) {
        return false;
    }
    errno = 0;
    const QByteArray p = path.toUtf8();
    return ::statvfs(p.constData(), out) == 0;
}

bool stat_ok(const QString &path, struct stat *out)
{
    if (!out) {
        return false;
    }
    errno = 0;
    const QByteArray p = path.toUtf8();
    return ::stat(p.constData(), out) == 0;
}

struct ProcMountsReader
{
    std::FILE *f = nullptr;

    ProcMountsReader() : f(std::fopen("/proc/self/mounts", "r")) {}
    ~ProcMountsReader()
    {
        if (f) {
            std::fclose(f);
        }
    }

    ProcMountsReader(const ProcMountsReader &) = delete;
    ProcMountsReader &operator=(const ProcMountsReader &) = delete;

    bool isOpen() const { return f != nullptr; }

    bool readLine(QByteArray &out)
    {
        out.clear();
        if (!f) {
            return false;
        }
        char buf[16384];
        if (!std::fgets(buf, static_cast<int>(sizeof(buf)), f)) {
            return false;
        }
        out = QByteArray(buf);
        return true;
    }
};

QString normalizeMountPath(QString p)
{
    if (p.isEmpty()) {
        return p;
    }
    p.replace(QStringLiteral("\\040"), QStringLiteral(" "));
    while (p.size() > 1 && p.endsWith('/')) {
        p.chop(1);
    }
    return p;
}

} // namespace

bool FileSystemUtils::isUnsupportedPartitionType(QStringView type)
{
    if (type.isEmpty()) {
        return false;
    }
    for (const char *p : kUnsupportedPartitions) {
        if (type == QLatin1String(p)) {
            return true;
        }
    }
    return false;
}

quint64 FileSystemUtils::bytesTotal(const QString &path)
{
    struct statvfs vfs;
    if (!statvfs_ok(path, &vfs)) {
        return 0;
    }
    return static_cast<quint64>(vfs.f_blocks) * static_cast<quint64>(vfs.f_frsize);
}

quint64 FileSystemUtils::bytesFree(const QString &path)
{
    struct statvfs vfs;
    if (!statvfs_ok(path, &vfs)) {
        return 0;
    }
    return static_cast<quint64>(vfs.f_bfree) * static_cast<quint64>(vfs.f_frsize);
}

quint64 FileSystemUtils::bytesAvailable(const QString &path)
{
    struct statvfs vfs;
    if (!statvfs_ok(path, &vfs)) {
        return 0;
    }
    return static_cast<quint64>(vfs.f_bavail) * static_cast<quint64>(vfs.f_frsize);
}

quint64 FileSystemUtils::deviceId(const QString &path)
{
    struct stat st;
    if (!stat_ok(path, &st)) {
        return 0;
    }
    return static_cast<quint64>(st.st_dev);
}

bool FileSystemUtils::isMountPoint(const QString &path)
{
    QString normalized = normalizeMountPath(path);
    if (normalized.isEmpty()) {
        return false;
    }

    ProcMountsReader mounts;
    if (!mounts.isOpen()) {
        return false;
    }

    for (;;) {
        QByteArray rawLine;
        if (!mounts.readLine(rawLine)) {
            break;
        }
        const QString line = QString::fromLocal8Bit(rawLine).trimmed();
        const QStringList parts = split_whitespace_skip_empty(line);
        if (parts.size() < 3) {
            continue;
        }
        const QString target = normalizeMountPath(parts.at(1));
        if (target == normalized) {
            return true;
        }
    }
    return false;
}

QString FileSystemUtils::fileSystemType(const QString &path)
{
    QString normalized = normalizeMountPath(path);
    if (normalized.isEmpty()) {
        return {};
    }

    ProcMountsReader mounts;
    if (!mounts.isOpen()) {
        return {};
    }

    QString bestType;
    int bestLen = -1;

    for (;;) {
        QByteArray rawLine;
        if (!mounts.readLine(rawLine)) {
            break;
        }
        const QString line = QString::fromLocal8Bit(rawLine).trimmed();
        const QStringList parts = split_whitespace_skip_empty(line);
        if (parts.size() < 3) {
            continue;
        }
        const QString target = normalizeMountPath(parts.at(1));
        const QString type = parts.at(2);

        if (normalized == target || normalized.startsWith(target + '/')) {
            if (target.size() > bestLen) {
                bestLen = target.size();
                bestType = type;
            }
        }
    }
    return bestType;
}

quint64 FileSystemUtils::getFreeSpace(const QString &path)
{
    struct statvfs vfs;
    if (!statvfs_ok(path + "/", &vfs)) {
        qDebug() << "Cannot determine free space for" << path
                 << ": Drive not ready, or does not exist, or is read-only.";
        return 0;
    }
    if ((vfs.f_flag & ST_RDONLY) != 0) {
        qDebug() << "Cannot determine free space for" << path
                 << ": Drive not ready, or does not exist, or is read-only.";
        return 0;
    }
    return bytesAvailable(path + "/") / 1024;
}

QString FileSystemUtils::getFreeSpaceString(const QString &path)
{
    constexpr float factor = 1024 * 1024;
    const quint64 freeSpace = getFreeSpace(path);
    return QString::number(static_cast<double>(freeSpace) / factor, 'f', 2) + "GiB";
}

bool FileSystemUtils::isOnSupportedPartition(const QString &dir)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    const QString partType = fileSystemType(dir + "/");
    const bool isUnsupported = isUnsupportedPartitionType(QStringView(partType));
    const bool isSupported = !isUnsupported;
    qDebug() << "Detected partition:" << partType << "Supported part:" << isSupported;
    return isSupported;
}

QString FileSystemUtils::largerFreeSpace(const QString &dir1, const QString &dir2)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (deviceId(dir1 + "/") == deviceId(dir2 + "/")) {
        return dir1;
    }
    const quint64 dir1Free = getFreeSpace(dir1);
    const quint64 dir2Free = getFreeSpace(dir2);
    return dir1Free >= dir2Free ? dir1 : dir2;
}

QString FileSystemUtils::largerFreeSpace(const QString &dir1, const QString &dir2, const QString &dir3)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    return largerFreeSpace(largerFreeSpace(dir1, dir2), dir3);
}
