#include "dir_cpp.h"

#include <cerrno>
#include <cstring>

#include <algorithm>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

namespace {

static const DirCpp::Hooks *g_hooks = nullptr;

bool isAbsolutePathLinux(const std::string &path)
{
    return !path.empty() && path[0] == '/';
}

bool statIsDir(const std::string &path)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool mkdirOne(const std::string &path)
{
    errno = 0;
    if (::mkdir(path.c_str(), 0777) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        return statIsDir(path);
    }
    return false;
}

bool isDirEntryOfType(const std::string &fullPath, DirCpp::EntryType type)
{
    if (type == DirCpp::EntryType::All) {
        return true;
    }
    struct stat st;
    if (::lstat(fullPath.c_str(), &st) != 0) {
        return false;
    }
    if (type == DirCpp::EntryType::Files) {
        return S_ISREG(st.st_mode);
    }
    return S_ISDIR(st.st_mode);
}

bool globMatchOne(const char *pattern, const char *str)
{
    if (!pattern || !str) {
        return false;
    }

    if (*pattern == '\0') {
        return *str == '\0';
    }

    if (*pattern == '*') {
        while (*pattern == '*') {
            ++pattern;
        }
        if (*pattern == '\0') {
            return true;
        }
        for (const char *s = str; *s; ++s) {
            if (globMatchOne(pattern, s)) {
                return true;
            }
        }
        return globMatchOne(pattern, str + std::strlen(str));
    }

    if (*pattern == '?') {
        return *str != '\0' && globMatchOne(pattern + 1, str + 1);
    }

    return *pattern == *str && globMatchOne(pattern + 1, str + 1);
}

bool globMatchAny(const std::vector<std::string> &patterns, const std::string &fileName)
{
    if (patterns.empty()) {
        return true;
    }
    for (const std::string &p : patterns) {
        if (globMatchOne(p.c_str(), fileName.c_str())) {
            return true;
        }
    }
    return false;
}

std::string toLowerAsciiCopy(const std::string &s)
{
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool hasFlag(DirCpp::Filter flags, DirCpp::Filter f)
{
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(f)) != 0u;
}

bool statType(const std::string &path, bool *isDir, bool *isReg, bool *isSym)
{
    if (isDir) {
        *isDir = false;
    }
    if (isReg) {
        *isReg = false;
    }
    if (isSym) {
        *isSym = false;
    }

    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        return false;
    }
    if (isDir) {
        *isDir = S_ISDIR(st.st_mode);
    }
    if (isReg) {
        *isReg = S_ISREG(st.st_mode);
    }
    if (isSym) {
        *isSym = S_ISLNK(st.st_mode);
    }
    return true;
}

bool removeRecursivelyImpl(const std::string &path)
{
    bool isDir = false;
    bool isReg = false;
    bool isSym = false;
    if (!statType(path, &isDir, &isReg, &isSym)) {
        return false;
    }

    if (isSym || isReg) {
        return ::unlink(path.c_str()) == 0;
    }

    if (!isDir) {
        return ::unlink(path.c_str()) == 0;
    }

    DIR *d = ::opendir(path.c_str());
    if (!d) {
        return false;
    }

    bool ok = true;
    for (;;) {
        errno = 0;
        dirent *e = ::readdir(d);
        if (!e) {
            break;
        }
        const char *n = e->d_name;
        if (!n || std::strcmp(n, ".") == 0 || std::strcmp(n, "..") == 0) {
            continue;
        }
        const std::string child = path + "/" + n;
        if (!removeRecursivelyImpl(child)) {
            ok = false;
        }
    }
    (void)::closedir(d);

    if (::rmdir(path.c_str()) != 0) {
        ok = false;
    }
    return ok;
}

} // namespace

void DirCpp::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

DirCpp::DirCpp() = default;

DirCpp::DirCpp(const std::string &path)
    : p(path)
{
}

void DirCpp::setPath(const std::string &path)
{
    p = cleanPath(path.empty() ? std::string(".") : path);
}

const std::string &DirCpp::path() const
{
    return p;
}

bool DirCpp::exists() const
{
    return exists(p);
}

bool DirCpp::exists(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    return statIsDir(path);
}

bool DirCpp::isEmpty() const
{
    if (!exists()) {
        return false;
    }

    DIR *d = ::opendir(p.c_str());
    if (!d) {
        return false;
    }

    bool empty = true;
    for (;;) {
        errno = 0;
        dirent *e = ::readdir(d);
        if (!e) {
            break;
        }
        const char *name = e->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
            continue;
        }
        empty = false;
        break;
    }

    (void)::closedir(d);
    return empty;
}

std::string DirCpp::filePath(const std::string &fileName) const
{
    if (isAbsolutePathLinux(fileName)) {
        return fileName;
    }

    if (fileName.empty()) {
        return p;
    }

    if (p.empty() || p.back() == '/') {
        return p + fileName;
    }
    return p + "/" + fileName;
}

std::vector<std::string> DirCpp::entryList(const std::vector<std::string> &nameFilters, EntryType type,
                                           bool sortIgnoreCase) const
{
    std::vector<std::string> names;
    if (!exists()) {
        return names;
    }

    DIR *d = ::opendir(p.c_str());
    if (!d) {
        return names;
    }

    for (;;) {
        errno = 0;
        dirent *e = ::readdir(d);
        if (!e) {
            break;
        }

        const char *n = e->d_name;
        if (!n || std::strcmp(n, ".") == 0 || std::strcmp(n, "..") == 0) {
            continue;
        }

        const std::string fileName(n);
        if (!globMatchAny(nameFilters, fileName)) {
            continue;
        }

        const std::string fullPath = filePath(fileName);
        if (!isDirEntryOfType(fullPath, type)) {
            continue;
        }

        names.push_back(fileName);
    }

    (void)::closedir(d);

    if (sortIgnoreCase) {
        std::stable_sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
            const std::string al = toLowerAsciiCopy(a);
            const std::string bl = toLowerAsciiCopy(b);
            if (al == bl) {
                return a < b;
            }
            return al < bl;
        });
    } else {
        std::stable_sort(names.begin(), names.end());
    }

    return names;
}

std::vector<std::string> DirCpp::entryInfoList(const std::vector<std::string> &nameFilters, EntryType type,
                                               bool sortIgnoreCase) const
{
    std::vector<std::string> out;
    const std::vector<std::string> names = entryList(nameFilters, type, sortIgnoreCase);
    out.reserve(names.size());
    for (const std::string &n : names) {
        out.push_back(filePath(n));
    }
    return out;
}

std::vector<DirCpp::FileInfo> DirCpp::entryInfoList(Filter filters) const
{
    std::vector<FileInfo> out;
    if (!exists()) {
        return out;
    }

    DIR *d = ::opendir(p.c_str());
    if (!d) {
        return out;
    }

    const bool wantNoDots = hasFlag(filters, Filter::NoDotAndDotDot);
    const bool wantNoSyms = hasFlag(filters, Filter::NoSymLinks);

    const bool wantAllEntries = hasFlag(filters, Filter::AllEntries);
    const bool wantDirs = hasFlag(filters, Filter::Dirs);
    const bool wantFiles = hasFlag(filters, Filter::Files);

    for (;;) {
        errno = 0;
        dirent *e = ::readdir(d);
        if (!e) {
            break;
        }
        const char *n = e->d_name;
        if (!n) {
            continue;
        }
        if (wantNoDots && (std::strcmp(n, ".") == 0 || std::strcmp(n, "..") == 0)) {
            continue;
        }

        const std::string name(n);
        const std::string full = filePath(name);

        bool isDir = false;
        bool isReg = false;
        bool isSym = false;
        if (!statType(full, &isDir, &isReg, &isSym)) {
            continue;
        }

        if (wantNoSyms && isSym) {
            continue;
        }

        bool accept = false;
        if (wantAllEntries) {
            accept = isDir || isReg || isSym;
        } else {
            if (wantDirs && isDir) {
                accept = true;
            }
            if (wantFiles && isReg) {
                accept = true;
            }
        }
        if (!accept) {
            continue;
        }

        FileInfo fi;
        fi.fileName = name;
        fi.filePath = full;
        fi.isDir = isDir;
        fi.isFile = isReg;
        fi.isSymLink = isSym;
        out.push_back(fi);
    }

    (void)::closedir(d);

    std::stable_sort(out.begin(), out.end(), [](const FileInfo &a, const FileInfo &b) {
        const std::string al = toLowerAsciiCopy(a.fileName);
        const std::string bl = toLowerAsciiCopy(b.fileName);
        if (al == bl) {
            return a.fileName < b.fileName;
        }
        return al < bl;
    });

    return out;
}

bool DirCpp::removeRecursively() const
{
    if (g_hooks && g_hooks->removeRecursively) {
        return g_hooks->removeRecursively(p);
    }
    if (p.empty()) {
        return false;
    }
    return removeRecursivelyImpl(p);
}

bool DirCpp::mkpath(const std::string &dirPath) const
{
    if (g_hooks && g_hooks->mkpath) {
        return g_hooks->mkpath(dirPath);
    }
    if (dirPath.empty()) {
        return false;
    }

    const std::string cleaned = cleanPath(dirPath);

    std::string cur;
    std::size_t pos = 0;
    if (!cleaned.empty() && cleaned[0] == '/') {
        cur = "/";
        pos = 1;
    }

    while (pos <= cleaned.size()) {
        const std::size_t next = cleaned.find('/', pos);
        const std::string part = cleaned.substr(pos, (next == std::string::npos) ? std::string::npos : next - pos);

        if (!part.empty() && part != ".") {
            if (cur.empty() || cur.back() == '/') {
                cur += part;
            } else {
                cur += "/" + part;
            }
            if (!mkdirOne(cur)) {
                return false;
            }
        }

        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }

    return true;
}

bool DirCpp::setCurrent(const std::string &path)
{
    if (g_hooks && g_hooks->setCurrent) {
        return g_hooks->setCurrent(path);
    }
    errno = 0;
    return ::chdir(path.c_str()) == 0;
}

std::string DirCpp::cleanPath(const std::string &path)
{
    if (path.empty()) {
        return std::string();
    }

    const bool absolute = isAbsolutePathLinux(path);

    // Normalize: treat multiple slashes as one (local path mode), resolve "." and "..".
    std::vector<std::string> stack;
    stack.reserve(16);

    std::size_t i = 0;
    if (absolute) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
    }

    while (i <= path.size()) {
        const std::size_t j = (i < path.size()) ? path.find('/', i) : std::string::npos;
        const std::size_t end = (j == std::string::npos) ? path.size() : j;
        const std::string_view seg(path.data() + i, end - i);

        if (seg.empty() || seg == ".") {
            // skip
        } else if (seg == "..") {
            if (!stack.empty() && stack.back() != "..") {
                stack.pop_back();
            } else {
                stack.emplace_back("..");
            }
        } else {
            stack.emplace_back(seg);
        }

        if (j == std::string::npos) {
            break;
        }
        i = j + 1;
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
    }

    std::string out;
    out.reserve(path.size());

    if (absolute) {
        out.push_back('/');
    }

    for (std::size_t k = 0; k < stack.size(); ++k) {
        if (!out.empty() && out.back() != '/') {
            out.push_back('/');
        }
        out.append(stack[k]);
    }

    if (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }

    if (out.empty() && !absolute) {
        return std::string(".");
    }
    return out;
}

std::string DirCpp::absolutePath(const std::string &path)
{
    if (path.empty()) {
        return std::string();
    }

    const std::string cleaned = cleanPath(path);
    if (!cleaned.empty() && cleaned[0] == '/') {
        return cleaned;
    }

    char buf[PATH_MAX];
    errno = 0;
    const char *cwd = ::getcwd(buf, sizeof(buf));
    if (!cwd) {
        return cleaned;
    }
    std::string combined = std::string(cwd);
    if (!combined.empty() && combined.back() != '/') {
        combined.push_back('/');
    }
    combined.append(cleaned);
    return cleanPath(combined);
}

std::string DirCpp::absolutePathOfContainingDir(const std::string &filePath)
{
    if (filePath.empty()) {
        return std::string();
    }
    std::string cleaned = cleanPath(filePath);
    std::string dirPart;
    const std::size_t pos = cleaned.find_last_of('/');
    if (pos == std::string::npos) {
        dirPart = ".";
    } else if (pos == 0) {
        dirPart = "/";
    } else {
        dirPart = cleaned.substr(0, pos);
    }
    return absolutePath(dirPart);
}
