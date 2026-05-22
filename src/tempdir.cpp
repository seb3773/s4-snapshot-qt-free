#include "tempdir.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

#ifdef UNIT_TESTS
const TempDir::Hooks *g_hooks = nullptr;
#endif

bool remove_recursively(const char *path)
{
    struct stat st;
    if (::lstat(path, &st) != 0) {
        return errno == ENOENT;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = ::opendir(path);
        if (!d) {
            return false;
        }

        bool ok = true;
        for (;;) {
            errno = 0;
            dirent *ent = ::readdir(d);
            if (!ent) {
                if (errno != 0) {
                    ok = false;
                }
                break;
            }
            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            std::string child(path);
            if (!child.empty() && child.back() != '/') {
                child.push_back('/');
            }
            child.append(ent->d_name);
            if (!remove_recursively(child.c_str())) {
                ok = false;
            }
        }

        ::closedir(d);
        if (::rmdir(path) != 0) {
            ok = false;
        }
        return ok;
    }

    return ::unlink(path) == 0;
}

std::string mkdtemp_from_template(const std::string &template_path)
{
    // NOTE: mkdtemp() creates with 0700 but subject to umask.
    // Qt6 explicitly requests owner-only perms (0700). We'll chmod() after create.
    if (template_path.size() < 6) {
        return {};
    }

    std::string templ = template_path;
    if (templ.compare(templ.size() - 6, 6, "XXXXXX") != 0) {
        templ.append("XXXXXX");
    }

    const std::size_t pos = templ.rfind("XXXXXX");
    if (pos == std::string::npos || pos != templ.size() - 6) {
        return {};
    }

    std::string buf = templ;
    buf.push_back('\0');
    if (!::mkdtemp(buf.data())) {
        return {};
    }
    buf.pop_back();
    // Enforce owner-only permissions regardless of umask.
    (void)::chmod(buf.c_str(), S_IRWXU);
    return buf;
}

std::string qt_error_string_like()
{
    // Minimal equivalent for qt_error_string() used by Qt6.
    const int e = errno;
    if (e == 0) {
        return std::string();
    }
    return std::string(std::strerror(e));
}

std::string tempPath()
{
    // Approximate QDir::tempPath() for our pure C++ backend.
    // On Unix Qt prioritizes TMPDIR; we keep it strict/simple here.
    const char *tmpdir = std::getenv("TMPDIR");
    if (tmpdir && *tmpdir) {
        return std::string(tmpdir);
    }
    return std::string("/tmp");
}

std::string defaultTemplateName()
{
    // Strict Qt6 default: tempPath + "/" + (appName|qt_temp) + "-XXXXXX".
    // Pure C++ backend cannot see QCoreApplication::applicationName() reliably,
    // so we use "qt_temp".
    const std::string baseName("qt_temp");
    std::string p = tempPath();
    if (!p.empty() && p.back() != '/') {
        p.push_back('/');
    }
    p.append(baseName);
    p.append("-XXXXXX");
    return p;
}

} // namespace

#ifdef UNIT_TESTS
void TempDir::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

bool TempDir::removeRecursivelyForTests(const std::string &path)
{
    if (g_hooks && g_hooks->removeRecursively) {
        return g_hooks->removeRecursively(path);
    }
    return true;
}
#endif

TempDir::TempDir()
    : TempDir(defaultTemplateName())
{
}

TempDir::TempDir(const std::string &template_path)
{
    // Qt6: empty template -> default
    const std::string useTemplate = template_path.empty() ? defaultTemplateName() : template_path;

    // Qt6 retries up to 256 times on EEXIST.
    valid = false;
    for (int i = 0; i < 256; ++i) {
        errno = 0;
        dirPath = mkdtemp_from_template(useTemplate);
        if (!dirPath.empty()) {
            valid = true;
            pathOrError.clear();
            return;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    pathOrError = qt_error_string_like();
    dirPath.clear();
}

TempDir::TempDir(TempDir &&other) noexcept
{
    dirPath = std::move(other.dirPath);
    pathOrError = std::move(other.pathOrError);
    valid = other.valid;
    removed = other.removed;
    autoRemoveEnabled = other.autoRemoveEnabled;

    other.valid = false;
    other.removed = true;
    other.autoRemoveEnabled = true;
    other.dirPath.clear();
    other.pathOrError.clear();
}

TempDir &TempDir::operator=(TempDir &&other) noexcept
{
    if (this == &other) {
        return *this;
    }

    if (valid && !removed && autoRemoveEnabled) {
        (void)remove();
    }

    dirPath = std::move(other.dirPath);
    pathOrError = std::move(other.pathOrError);
    valid = other.valid;
    removed = other.removed;
    autoRemoveEnabled = other.autoRemoveEnabled;

    other.valid = false;
    other.removed = true;
    other.autoRemoveEnabled = true;
    other.dirPath.clear();
    other.pathOrError.clear();

    return *this;
}

TempDir::~TempDir()
{
    if (valid && !removed && autoRemoveEnabled) {
        (void)remove();
    }
}

std::string TempDir::filePath(const std::string &fileName) const
{
    // Qt6: absolute paths not allowed; doesn't clean redundant separators or dot segments.
    if (fileName.size() > 0 && fileName[0] == '/') {
        return {};
    }
    if (!valid) {
        return {};
    }
    std::string ret = dirPath;
    if (!fileName.empty()) {
        ret.push_back('/');
        ret.append(fileName);
    }
    return ret;
}

bool TempDir::remove()
{
    // Qt6: if creation failed, remove() returns false
    if (!valid) {
        return false;
    }
    if (removed) {
        return true;
    }

#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->removeRecursively) {
        const bool ok = g_hooks->removeRecursively(dirPath);
        if (ok) {
            removed = true;
        }
        return ok;
    }
#endif

    const bool ok = remove_recursively(dirPath.c_str());
    if (ok) {
        removed = true;
    }
    return ok;
}
