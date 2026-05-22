#include "file_cpp.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

std::string FileCpp::lastError;

namespace {

static const FileCpp::Hooks *g_hooks = nullptr;

std::string errnoString()
{
    const int e = errno;
    if (e == 0) {
        return std::string();
    }
    return std::string(std::strerror(e));
}

int openFlagsFor(FileCpp::OpenMode mode)
{
    int flags = 0;

    const bool rd = (mode & FileCpp::OpenMode::ReadOnly);
    const bool wr = (mode & FileCpp::OpenMode::WriteOnly);

    if (rd && wr) {
        flags |= O_RDWR;
    } else if (rd) {
        flags |= O_RDONLY;
    } else if (wr) {
        flags |= O_WRONLY;
    } else {
        // invalid: neither read nor write
        flags |= O_RDONLY;
    }

    if (mode & FileCpp::OpenMode::Append) {
        flags |= O_APPEND;
    }
    if (mode & FileCpp::OpenMode::Truncate) {
        flags |= O_TRUNC;
    }

    // Qt's QFile creates file on WriteOnly/ReadWrite.
    if (wr) {
        flags |= O_CREAT;
    }

    return flags;
}

} // namespace

void FileCpp::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

FileCpp::FileCpp() = default;

FileCpp::FileCpp(const std::string &fileName)
    : name(fileName)
{
}

FileCpp::FileCpp(FileCpp &&other) noexcept
    : name(std::move(other.name))
    , fd(other.fd)
    , mode(other.mode)
{
    other.fd = -1;
    other.mode = OpenMode::NotOpen;
    other.name.clear();
}

FileCpp &FileCpp::operator=(FileCpp &&other) noexcept
{
    if (this == &other) {
        return *this;
    }

    close();

    name = std::move(other.name);
    fd = other.fd;
    mode = other.mode;

    other.fd = -1;
    other.mode = OpenMode::NotOpen;
    other.name.clear();
    // lastError is static (global) state.

    return *this;
}

FileCpp::~FileCpp()
{
    close();
}

void FileCpp::setFileName(const std::string &n)
{
    if (isOpen()) {
        close();
    }
    name = n;
}

const std::string &FileCpp::fileName() const
{
    return name;
}

bool FileCpp::isOpen() const
{
    return fd >= 0;
}

bool FileCpp::open(OpenMode m)
{
    lastError.clear();

    if (isOpen()) {
        lastError = "File already open";
        return false;
    }
    if (name.empty()) {
        lastError = "Empty file name";
        return false;
    }

    errno = 0;
    const int flags = openFlagsFor(m);
    const int newFd = ::open(name.c_str(), flags, 0666);
    if (newFd < 0) {
        lastError = errnoString();
        return false;
    }

    fd = newFd;
    mode = m;
    return true;
}

void FileCpp::close()
{
    if (!isOpen()) {
        return;
    }
    (void)::close(fd);
    fd = -1;
    mode = OpenMode::NotOpen;
}

bool FileCpp::exists() const
{
    return exists(name);
}

bool FileCpp::exists(const std::string &fileName)
{
    if (g_hooks && g_hooks->exists) {
        return g_hooks->exists(fileName);
    }
    if (fileName.empty()) {
        return false;
    }
    struct stat st;
    return ::stat(fileName.c_str(), &st) == 0;
}

bool FileCpp::remove(const std::string &fileName)
{
    if (g_hooks && g_hooks->remove) {
        return g_hooks->remove(fileName);
    }
    if (fileName.empty()) {
        return false;
    }
    errno = 0;
    return ::unlink(fileName.c_str()) == 0;
}

bool FileCpp::copy(const std::string &sourceFileName, const std::string &destFileName)
{
    if (g_hooks && g_hooks->copy) {
        return g_hooks->copy(sourceFileName, destFileName);
    }
    if (sourceFileName.empty() || destFileName.empty()) {
        return false;
    }
    if (exists(destFileName)) {
        return false;
    }

    errno = 0;
    const int inFd = ::open(sourceFileName.c_str(), O_RDONLY);
    if (inFd < 0) {
        return false;
    }

    // Qt6 fails if destination exists. Use O_EXCL for atomicity.
    errno = 0;
    const int outFd = ::open(destFileName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0666);
    if (outFd < 0) {
        (void)::close(inFd);
        return false;
    }

    constexpr std::size_t chunk = 4096;
    std::vector<std::uint8_t> buf(chunk);
    bool ok = true;
    for (;;) {
        errno = 0;
        const ssize_t r = ::read(inFd, buf.data(), buf.size());
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (r == 0) {
            break;
        }
        std::size_t off = 0;
        while (off < static_cast<std::size_t>(r)) {
            errno = 0;
            const ssize_t w = ::write(outFd, buf.data() + off, static_cast<std::size_t>(r) - off);
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ok = false;
                break;
            }
            if (w == 0) {
                ok = false;
                break;
            }
            off += static_cast<std::size_t>(w);
        }
        if (!ok) {
            break;
        }
    }

    // Copy permissions (best-effort)
    struct stat st;
    if (::stat(sourceFileName.c_str(), &st) == 0) {
        (void)::fchmod(outFd, st.st_mode & 07777);
    }

    (void)::close(inFd);
    (void)::close(outFd);
    if (!ok) {
        (void)remove(destFileName);
    }
    return ok;
}

bool FileCpp::link(const std::string &sourceFileName, const std::string &linkName)
{
    if (g_hooks && g_hooks->link) {
        return g_hooks->link(sourceFileName, linkName);
    }
    if (sourceFileName.empty()) {
        return false;
    }
    if (linkName.empty()) {
        return false;
    }

    // Qt6: will not overwrite an existing entity.
    errno = 0;
    return ::symlink(sourceFileName.c_str(), linkName.c_str()) == 0;
}

bool FileCpp::isFile(const std::string &fileName)
{
    if (fileName.empty()) {
        return false;
    }
    struct stat st;
    if (::stat(fileName.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

bool FileCpp::isDir(const std::string &fileName)
{
    if (fileName.empty()) {
        return false;
    }
    struct stat st;
    if (::stat(fileName.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool FileCpp::isSymLink(const std::string &fileName)
{
    if (fileName.empty()) {
        return false;
    }
    struct stat st;
    if (::lstat(fileName.c_str(), &st) != 0) {
        return false;
    }
    return S_ISLNK(st.st_mode);
}

std::string FileCpp::baseName(const std::string &fileName)
{
    if (fileName.empty()) {
        return std::string();
    }

    if (fileName.back() == '/') {
        return std::string();
    }

    const std::size_t slash = fileName.find_last_of('/');
    const std::string nameOnly = (slash == std::string::npos) ? fileName : fileName.substr(slash + 1);
    if (nameOnly.empty()) {
        return std::string();
    }

    const std::size_t dot = nameOnly.find('.');
    if (dot == std::string::npos) {
        return nameOnly;
    }
    if (dot == 0) {
        return std::string();
    }
    return nameOnly.substr(0, dot);
}

std::string FileCpp::completeBaseName(const std::string &fileName)
{
    if (fileName.empty()) {
        return std::string();
    }

    if (fileName.back() == '/') {
        return std::string();
    }

    std::string s = fileName;
    const std::size_t slash = s.find_last_of('/');
    const std::string nameOnly = (slash == std::string::npos) ? s : s.substr(slash + 1);
    if (nameOnly.empty()) {
        return std::string();
    }

    const std::size_t dot = nameOnly.find_last_of('.');
    if (dot == std::string::npos) {
        return nameOnly;
    }
    if (dot == 0) {
        return std::string();
    }
    return nameOnly.substr(0, dot);
}

std::string FileCpp::completeSuffix(const std::string &fileName)
{
    if (fileName.empty()) {
        return std::string();
    }
    if (fileName.back() == '/') {
        return std::string();
    }

    const std::size_t slash = fileName.find_last_of('/');
    const std::string nameOnly = (slash == std::string::npos) ? fileName : fileName.substr(slash + 1);
    if (nameOnly.empty()) {
        return std::string();
    }

    const std::size_t dot = nameOnly.find('.');
    if (dot == std::string::npos) {
        return std::string();
    }
    if (dot + 1 >= nameOnly.size()) {
        return std::string();
    }
    return nameOnly.substr(dot + 1);
}

std::string FileCpp::fileNameComponent(const std::string &fileName)
{
    if (fileName.empty()) {
        return std::string();
    }
    if (fileName.back() == '/') {
        return std::string();
    }
    const std::size_t slash = fileName.find_last_of('/');
    if (slash == std::string::npos) {
        return fileName;
    }
    return fileName.substr(slash + 1);
}

std::string FileCpp::qtFileName(const std::string &storedName)
{
    return storedName;
}

std::int64_t FileCpp::size(const std::string &fileName)
{
    lastError.clear();
    if (fileName.empty()) {
        lastError = "Empty filename passed to function";
        return 0;
    }

    struct stat st;
    if (::stat(fileName.c_str(), &st) != 0) {
        lastError = std::strerror(errno);
        return 0;
    }

    return static_cast<std::int64_t>(st.st_size);
}

std::int64_t FileCpp::lastReadSecsSinceEpoch(const std::string &fileName)
{
    lastError.clear();
    if (fileName.empty()) {
        lastError = "Empty filename passed to function";
        return 0;
    }

    struct stat st;
    if (::stat(fileName.c_str(), &st) != 0) {
        lastError = std::strerror(errno);
        return 0;
    }

#if defined(__APPLE__)
    return static_cast<std::int64_t>(st.st_atimespec.tv_sec);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    return static_cast<std::int64_t>(st.st_atim.tv_sec);
#else
    return static_cast<std::int64_t>(st.st_atime);
#endif
}

std::int64_t FileCpp::lastModifiedSecsSinceEpoch(const std::string &fileName)
{
    lastError.clear();
    if (fileName.empty()) {
        lastError = "Empty filename passed to function";
        return 0;
    }

    struct stat st;
    if (::stat(fileName.c_str(), &st) != 0) {
        lastError = std::strerror(errno);
        return 0;
    }

#if defined(__APPLE__)
    return static_cast<std::int64_t>(st.st_mtimespec.tv_sec);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    return static_cast<std::int64_t>(st.st_mtim.tv_sec);
#else
    return static_cast<std::int64_t>(st.st_mtime);
#endif
}

std::vector<std::uint8_t> FileCpp::readAll()
{
    lastError.clear();
    std::vector<std::uint8_t> out;
    if (!isOpen()) {
        lastError = "File not open";
        return out;
    }

    constexpr std::size_t chunk = 64 * 1024;
    std::vector<std::uint8_t> buf(chunk);

    for (;;) {
        errno = 0;
        const ssize_t r = ::read(fd, buf.data(), buf.size());
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            lastError = errnoString();
            out.clear();
            return out;
        }
        if (r == 0) {
            break;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + r);
    }

    (void)applyTextModeInPlace(out);
    return out;
}

std::string FileCpp::readLine()
{
    lastError.clear();
    std::string line;

    if (!isOpen()) {
        lastError = "File not open";
        return line;
    }

    for (;;) {
        unsigned char c = 0;
        errno = 0;
        const ssize_t r = ::read(fd, &c, 1);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            lastError = errnoString();
            line.clear();
            return line;
        }
        if (r == 0) {
            break;
        }

        line.push_back(static_cast<char>(c));
        if (c == '\n') {
            break;
        }
    }

    (void)applyTextModeLineInPlace(line);
    return line;
}

std::int64_t FileCpp::write(const std::uint8_t *data, std::size_t n)
{
    lastError.clear();
    if (!isOpen()) {
        lastError = "File not open";
        return -1;
    }
    if (!data && n != 0) {
        lastError = "Invalid buffer";
        return -1;
    }

    std::size_t total = 0;
    while (total < n) {
        errno = 0;
        const ssize_t w = ::write(fd, data + total, n - total);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            lastError = errnoString();
            return -1;
        }
        total += static_cast<std::size_t>(w);
    }
    return static_cast<std::int64_t>(total);
}

std::int64_t FileCpp::write(const std::string &s)
{
    return write(reinterpret_cast<const std::uint8_t *>(s.data()), s.size());
}

bool FileCpp::flush()
{
    lastError.clear();
    if (!isOpen()) {
        lastError = "File not open";
        return false;
    }
    errno = 0;
    if (::fsync(fd) != 0) {
        lastError = errnoString();
        return false;
    }
    return true;
}

std::string FileCpp::errorString() const
{
    return lastError;
}

bool FileCpp::applyTextModeInPlace(std::vector<std::uint8_t> &buf) const
{
    if (!(mode & OpenMode::Text)) {
        return true;
    }

    // Qt: Text mode converts CRLF into LF on read.
    std::vector<std::uint8_t> out;
    out.reserve(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const std::uint8_t c = buf[i];
        if (c == '\r') {
            if (i + 1 < buf.size() && buf[i + 1] == '\n') {
                continue;
            }
        }
        out.push_back(c);
    }
    buf.swap(out);
    return true;
}

bool FileCpp::applyTextModeLineInPlace(std::string &line) const
{
    if (!(mode & OpenMode::Text)) {
        return true;
    }

    // Remove a single '\r' if it precedes a final '\n'.
    if (line.size() >= 2 && line[line.size() - 2] == '\r' && line.back() == '\n') {
        line.erase(line.size() - 2, 1);
    }
    return true;
}
