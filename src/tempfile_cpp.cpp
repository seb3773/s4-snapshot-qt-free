#include "tempfile_cpp.h"

#include <cstdint>
#include <cerrno>
#include <cstring>
#include <cstdlib>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string errnoString()
{
    const int e = errno;
    if (e == 0) {
        return std::string();
    }
    return std::string(std::strerror(e));
}

bool unlinkIfExists(const char *path)
{
    errno = 0;
    if (::unlink(path) == 0) {
        return true;
    }
    return errno == ENOENT;
}

std::string mkstempFromTemplate(const std::string &templateName, int *outFd)
{
    if (!outFd) {
        return std::string();
    }
    *outFd = -1;

    std::string t = templateName;
    const std::size_t lastSlash = t.find_last_of('/');
    const std::size_t xPos = t.rfind("XXXXXX");
    if (xPos == std::string::npos || xPos + 6 != t.size() || (lastSlash != std::string::npos && xPos < lastSlash + 1)) {
        t.append("XXXXXX");
    }

    const std::size_t lastXPos = t.rfind("XXXXXX");
    if (lastXPos == std::string::npos || lastXPos + 6 != t.size()) {
        return std::string();
    }

    std::string buf = t;
    buf.push_back('\0');
    errno = 0;
    const int fd = ::mkstemp(buf.data());
    if (fd < 0) {
        return std::string();
    }
    buf.pop_back();
    *outFd = fd;
    return buf;
}

} // namespace

TempFileCpp::TempFileCpp()
    : TempFileCpp(std::string())
{
}

TempFileCpp::TempFileCpp(const std::string &templateName)
{
    templ = templateName.empty() ? defaultTemplateName() : templateName;
}

TempFileCpp::TempFileCpp(TempFileCpp &&other) noexcept
{
    templ = std::move(other.templ);
    filePath = std::move(other.filePath);
    lastError = std::move(other.lastError);
    fd = other.fd;
    autoRemoveEnabled = other.autoRemoveEnabled;
    removed = other.removed;

    other.fd = -1;
    other.autoRemoveEnabled = true;
    other.removed = true;
    other.templ.clear();
    other.filePath.clear();
    other.lastError.clear();
}

TempFileCpp &TempFileCpp::operator=(TempFileCpp &&other) noexcept
{
    if (this == &other) {
        return *this;
    }

    close();
    if (!filePath.empty() && autoRemoveEnabled) {
        (void)remove();
    }

    templ = std::move(other.templ);
    filePath = std::move(other.filePath);
    lastError = std::move(other.lastError);
    fd = other.fd;
    autoRemoveEnabled = other.autoRemoveEnabled;
    removed = other.removed;

    other.fd = -1;
    other.autoRemoveEnabled = true;
    other.removed = true;
    other.templ.clear();
    other.filePath.clear();
    other.lastError.clear();

    return *this;
}

TempFileCpp::~TempFileCpp()
{
    close();
    if (!filePath.empty() && autoRemoveEnabled) {
        (void)remove();
    }
}

std::string TempFileCpp::tempPath()
{
    const char *tmpdir = std::getenv("TMPDIR");
    if (tmpdir && *tmpdir) {
        return std::string(tmpdir);
    }
    return std::string("/tmp");
}

std::string TempFileCpp::defaultTemplateName()
{
    const std::string baseName("qt_temp");
    std::string p = tempPath();
    if (!p.empty() && p.back() != '/') {
        p.push_back('/');
    }
    p.append(baseName);
    p.append(".XXXXXX");
    return p;
}

bool TempFileCpp::materialize()
{
    if (isOpen()) {
        return true;
    }
    if (!filePath.empty()) {
        lastError = "File already materialized";
        return false;
    }

    lastError.clear();

    for (int i = 0; i < 256; ++i) {
        int newFd = -1;
        errno = 0;
        const std::string p = mkstempFromTemplate(templ, &newFd);
        if (!p.empty() && newFd >= 0) {
            fd = newFd;
            filePath = p;
            removed = false;
            return true;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    lastError = errnoString();
    return false;
}

bool TempFileCpp::open()
{
    if (isOpen()) {
        return true;
    }
    if (!filePath.empty()) {
        errno = 0;
        const int newFd = ::open(filePath.c_str(), O_RDWR);
        if (newFd < 0) {
            lastError = errnoString();
            return false;
        }
        fd = newFd;
        return true;
    }
    return materialize();
}

void TempFileCpp::close()
{
    if (fd >= 0) {
        (void)::close(fd);
        fd = -1;
    }
}

bool TempFileCpp::flush()
{
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

bool TempFileCpp::writeAll(const void *data, std::size_t n)
{
    if (!isOpen()) {
        lastError = "File not open";
        return false;
    }
    if (!data && n != 0) {
        lastError = "Invalid buffer";
        return false;
    }

    const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
    std::size_t off = 0;
    while (off < n) {
        errno = 0;
        const ssize_t w = ::write(fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            lastError = errnoString();
            return false;
        }
        if (w == 0) {
            lastError = "Short write";
            return false;
        }
        off += static_cast<std::size_t>(w);
    }
    return true;
}

bool TempFileCpp::remove()
{
    if (filePath.empty()) {
        return false;
    }
    if (removed) {
        return true;
    }

    close();

    const bool ok = unlinkIfExists(filePath.c_str());
    if (ok) {
        removed = true;
    } else {
        lastError = errnoString();
    }
    return ok;
}
