#include "installed_to_live_cpp.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#if __has_include(<crypt.h>)
#include <crypt.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Config {
    std::string userPw = "demo:demo";
    std::string rootPw = "root:root";
    std::string bindRoot = "/.bind-root";
    std::string realRoot = "/";
    std::string workDir = "/tmp/installed-to-live";
    std::string excludesDir = "/usr/local/share/excludes";
    std::string versionFile = "/etc/live/version/linuxfs.ver";
    std::string templateDir = "/usr/local/share/live-files";
    std::string outputFile;
    bool force = false;
    bool pretend = false;
    bool verbose = false;
    std::vector<std::string> rmDirs;
    std::vector<std::string> rmFiles;

    [[nodiscard]] std::string confFile() const { return workDir + "/cleanup.conf"; }
};

struct Result {
    bool ok = true;
    int code = 0;
};

constexpr const char *kMe = "installed-to-live";

void info(const std::string &message)
{
    std::printf("%s\n", message.c_str());
}

void error(const std::string &message)
{
    std::fprintf(stderr, "%s Error: %s\n", kMe, message.c_str());
}

Result fatal(const std::string &message, int code = 2)
{
    std::fprintf(stderr, "%s Fatal Error: %s\n", kMe, message.c_str());
    return {false, code};
}

std::string trim(std::string s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::vector<std::string> splitCommaList(const std::string &text)
{
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

std::string stripTrailingSlash(std::string path)
{
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

std::string joinRoot(const std::string &root, const std::string &absolutePath)
{
    const std::string cleanRoot = stripTrailingSlash(root.empty() ? "/" : root);
    const std::string cleanPath = absolutePath.empty() || absolutePath.front() == '/' ? absolutePath : "/" + absolutePath;
    if (cleanRoot == "/") {
        return cleanPath;
    }
    return cleanRoot + cleanPath;
}

std::string dirnameOf(const std::string &path)
{
    return fs::path(path).parent_path().string();
}

bool ensureDir(const std::string &path)
{
    std::error_code ec;
    if (path.empty()) {
        return true;
    }
    fs::create_directories(path, ec);
    return !ec;
}

bool touchFile(const std::string &path)
{
    if (!ensureDir(dirnameOf(path))) {
        return false;
    }
    std::ofstream f(path, std::ios::app);
    return static_cast<bool>(f);
}

bool writeText(const std::string &path, const std::string &text)
{
    if (!ensureDir(dirnameOf(path))) {
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return static_cast<bool>(f);
}

std::optional<std::string> readText(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool copyFilePreserve(const std::string &source, const std::string &dest)
{
    std::error_code ec;
    ensureDir(dirnameOf(dest));
    fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    fs::permissions(dest, fs::status(source, ec).permissions(), ec);
    return true;
}

bool copyTree(const std::string &source, const std::string &dest)
{
    std::error_code ec;
    fs::remove_all(dest, ec);
    fs::create_directories(dest, ec);
    if (ec) {
        return false;
    }
    if (!fs::exists(source)) {
        return false;
    }
    fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks
                           | fs::copy_options::overwrite_existing, ec);
    return !ec;
}

std::string decodeMountInfoPath(const std::string &path)
{
    std::string out;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '\\' && i + 3 < path.size()) {
            const std::string oct = path.substr(i + 1, 3);
            char *end = nullptr;
            const long v = std::strtol(oct.c_str(), &end, 8);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(v));
                i += 3;
                continue;
            }
        }
        out.push_back(path[i]);
    }
    return out;
}

std::string canonicalExistingOrWeak(const std::string &path)
{
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(path, ec);
    return ec ? stripTrailingSlash(path) : stripTrailingSlash(p.string());
}

bool isMounted(const std::string &path)
{
    const std::string want = canonicalExistingOrWeak(path);
    std::ifstream f("/proc/self/mountinfo");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string field;
        for (int i = 0; i < 5 && ss >> field; ++i) {
        }
        if (field.empty()) {
            continue;
        }
        if (canonicalExistingOrWeak(decodeMountInfoPath(field)) == want) {
            return true;
        }
    }
    return false;
}

bool doMountBind(const std::string &source, const std::string &target)
{
    return ::mount(source.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) == 0;
}

bool doMountBindRoot(const std::string &source, const std::string &target)
{
    if (::mount(source.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) != 0) {
        return false;
    }
    return ::mount(nullptr, target.c_str(), nullptr, MS_REC | MS_SLAVE, nullptr) == 0;
}

bool remountBind(const std::string &target, bool readOnly)
{
    unsigned long flags = MS_REMOUNT | MS_BIND;
    if (readOnly) {
        flags |= MS_RDONLY;
    }
    return ::mount(nullptr, target.c_str(), nullptr, flags, nullptr) == 0;
}

bool umountRecursive(const std::string &target)
{
    return ::umount2(target.c_str(), MNT_DETACH) == 0 || errno == EINVAL || errno == ENOENT;
}

void addUnique(std::vector<std::string> &items, const std::string &item)
{
    if (!item.empty() && std::find(items.begin(), items.end(), item) == items.end()) {
        items.push_back(item);
    }
}

std::string quoteForConf(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string joinComma(const std::vector<std::string> &items)
{
    std::string out;
    for (const std::string &item : items) {
        if (!out.empty()) {
            out.push_back(',');
        }
        out += item;
    }
    return out;
}

bool writeConfFile(const Config &cfg)
{
    std::ostringstream text;
    text << "REAL_ROOT=\"" << quoteForConf(cfg.realRoot) << "\"\n";
    text << "BIND_ROOT=\"" << quoteForConf(cfg.bindRoot) << "\"\n";
    text << "RM_DIRS=\"" << quoteForConf(joinComma(cfg.rmDirs)) << "\"\n\n";
    text << "RM_FILES=\"" << quoteForConf(joinComma(cfg.rmFiles)) << "\"\n";
    return writeText(cfg.confFile(), text.str());
}

std::string unquoteConfValue(std::string value)
{
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    std::string out;
    bool escape = false;
    for (char c : value) {
        if (escape) {
            out.push_back(c);
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool readConfFile(Config &cfg, bool force = false)
{
    const auto content = readText(cfg.confFile());
    if (!content) {
        return force;
    }

    std::istringstream ss(*content);
    std::string line;
    while (std::getline(ss, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = unquoteConfValue(line.substr(eq + 1));
        if (key == "REAL_ROOT") {
            cfg.realRoot = value;
        } else if (key == "BIND_ROOT") {
            cfg.bindRoot = value;
        } else if (key == "RM_DIRS") {
            cfg.rmDirs = splitCommaList(value);
        } else if (key == "RM_FILES") {
            cfg.rmFiles = splitCommaList(value);
        }
    }
    return true;
}

Result doStart(Config &cfg)
{
    std::error_code ec;
    if (!cfg.force && fs::exists(cfg.workDir, ec)) {
        return fatal("Work dir " + cfg.workDir + " exists.  Use --Force to delete it");
    }
    fs::remove_all(cfg.workDir, ec);

    if (!ensureDir(cfg.bindRoot)) {
        return fatal("Could not make bind root directory " + cfg.bindRoot);
    }

    if (isMounted(cfg.bindRoot)) {
        if (!cfg.force) {
            return fatal("bind-root: " + cfg.bindRoot + " is already a mount point");
        }
    } else if (!cfg.pretend && !doMountBindRoot(cfg.realRoot, cfg.bindRoot)) {
        return fatal("Could not bind mount " + cfg.realRoot + " to " + cfg.bindRoot + ": " + std::strerror(errno));
    }

    if (!writeConfFile(cfg)) {
        return fatal("Could not write config file " + cfg.confFile());
    }
    return {};
}

Result doReadOnly(Config &cfg, bool readOnly)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    if (!isMounted(cfg.bindRoot)) {
        return fatal(std::string(readOnly ? "read-only" : "read-write") + ": " + cfg.bindRoot + " is not mounted");
    }
    if (!cfg.pretend && !remountBind(cfg.bindRoot, readOnly)) {
        return fatal("Could not remount bind root: " + std::string(std::strerror(errno)));
    }
    return {};
}

bool makeDirForBind(Config &cfg, const std::string &dir, const std::string &orig)
{
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        return true;
    }
    if (!fs::is_directory(orig, ec)) {
        addUnique(cfg.rmDirs, orig);
    }
    return cfg.pretend || ensureDir(dir);
}

bool touchFileForBind(Config &cfg, const std::string &file, const std::string &orig)
{
    if (!makeDirForBind(cfg, dirnameOf(file), dirnameOf(orig))) {
        return false;
    }
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
        return true;
    }
    if (fs::exists(file, ec)) {
        error("Expected a plain file at " + orig);
        return false;
    }
    if (!fs::exists(orig, ec)) {
        addUnique(cfg.rmFiles, orig);
    }
    return cfg.pretend || touchFile(file);
}

bool bindMountTemplate(Config &cfg, const std::string &templateDir)
{
    std::error_code ec;
    if (!fs::is_directory(templateDir, ec)) {
        return true;
    }

    for (fs::recursive_directory_iterator it(templateDir, fs::directory_options::follow_directory_symlink, ec), end;
         !ec && it != end; it.increment(ec)) {
        const fs::directory_entry &entry = *it;
        if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) {
            continue;
        }

        const std::string file = entry.path().string();
        std::string base = file.substr(templateDir.size());
        if (base.empty() || base.front() != '/') {
            base = "/" + base;
        }
        const std::string target = joinRoot(cfg.bindRoot, base);
        const std::string orig = joinRoot(cfg.realRoot, base);

        if (entry.is_symlink(ec)) {
            if (fs::exists(orig, ec)) {
                continue;
            }
            ensureDir(dirnameOf(orig));
            fs::copy_symlink(file, orig, ec);
            if (!ec && fs::exists(fs::read_symlink(orig, ec), ec)) {
                addUnique(cfg.rmFiles, orig);
            } else {
                fs::remove(orig, ec);
            }
            continue;
        }

        if (fs::is_symlink(orig, ec)) {
            error("Won't bind mount symlink: " + orig);
            continue;
        }
        if (!touchFileForBind(cfg, target, orig)) {
            return false;
        }
        if (isMounted(target)) {
            error("File " + target + " is already mounted");
            continue;
        }
        if (!cfg.pretend && !doMountBind(file, target)) {
            error("Could not bind mount " + file + " to " + target + ": " + std::strerror(errno));
            return false;
        }
    }
    return !ec;
}

Result doFiles(Config &cfg, const std::string &from, const std::string &temp)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    std::error_code ec;
    if (!fs::is_directory(from, ec)) {
        return fatal("Directory " + from + " does not exist");
    }
    if (!copyTree(from, temp)) {
        return fatal("Could not copy template files from " + from);
    }
    if (!bindMountTemplate(cfg, temp)) {
        return fatal("Could not bind mount template files from " + temp);
    }
    writeConfFile(cfg);
    return {};
}

Result doBind(Config &cfg, const std::string &dirs)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    for (std::string dir : splitCommaList(dirs)) {
        if (!dir.empty() && dir.front() == '/') {
            dir.erase(dir.begin());
        }
        if (dir.empty()) {
            continue;
        }
        const std::string realDir = joinRoot(cfg.realRoot, "/" + dir);
        const std::string target = joinRoot(cfg.bindRoot, "/" + dir);
        if (!isMounted(realDir)) {
            continue;
        }
        ensureDir(target);
        if (!cfg.pretend && !doMountBind(realDir, target)) {
            return fatal("Could not bind mount " + realDir + " to " + target + ": " + std::strerror(errno));
        }
    }
    writeConfFile(cfg);
    return {};
}

Result doEmpty(Config &cfg, const std::string &dirs)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string templateDir = cfg.workDir + "/empty";
    for (const std::string &baseRaw : splitCommaList(dirs)) {
        std::string base = baseRaw;
        if (!base.empty() && base.front() == '/') {
            base.erase(base.begin());
        }
        if (base.empty()) {
            continue;
        }
        const std::string target = joinRoot(cfg.bindRoot, "/" + base);
        const std::string orig = joinRoot(cfg.realRoot, "/" + base);
        std::error_code ec;
        if (!fs::is_directory(target, ec)) {
            continue;
        }
        if (!makeDirForBind(cfg, target, orig)) {
            return fatal("Could not prepare empty bind target " + target);
        }
        const std::string emptyDir = templateDir + "/" + base;
        ensureDir(emptyDir);
        if (!cfg.pretend && !doMountBind(emptyDir, target)) {
            return fatal("Could not bind empty directory " + emptyDir + " to " + target);
        }
    }
    writeConfFile(cfg);
    return {};
}

std::string randomHex32()
{
    static constexpr char hex[] = "0123456789abcdef";
    std::random_device rd;
    std::string out(32, '0');
    for (char &c : out) {
        c = hex[rd() % 16];
    }
    return out;
}

std::string nowString()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[128] = {};
    std::strftime(buf, sizeof(buf), "%e %B %Y %T %Z", std::localtime(&t));
    return buf;
}

Result doVersionFile(Config &cfg, const std::string &title)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string vdir = cfg.workDir + "/version-file";
    const std::string vfile = vdir + cfg.versionFile;
    ensureDir(dirnameOf(vfile));
    if (fs::exists(cfg.versionFile)) {
        copyFilePreserve(cfg.versionFile, vfile);
    }
    std::ostringstream text;
    if (const auto old = readText(vfile)) {
        text << *old;
    }
    text << "==== " << randomHex32() << "\n\n";
    text << "title: " << title << "\n";
    text << "creation date: " << nowString() << "\n";
    text << "kernel: ";
    if (const auto v = readText("/proc/sys/kernel/ostype")) {
        text << trim(*v) << " ";
    }
    if (const auto v = readText("/proc/sys/kernel/osrelease")) {
        text << trim(*v);
    }
    text << "\n";
    text << "machine: ";
    if (const auto v = readText("/proc/sys/kernel/arch")) {
        text << trim(*v);
    } else {
        text << "unknown";
    }
    text << "\n";
    writeText(vfile, text.str());
    if (!bindMountTemplate(cfg, vdir)) {
        return fatal("Could not bind mount version file");
    }
    writeConfFile(cfg);
    return {};
}

Result doAdjtime(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string orig = "/etc/adjtime";
    if (!fs::exists(orig)) {
        return {};
    }
    const std::string templateDir = cfg.workDir + "/adjtime";
    const std::string target = templateDir + orig;
    copyFilePreserve(orig, target);
    if (auto text = readText(target)) {
        std::vector<std::string> lines;
        std::stringstream ss(*text);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }
        if (!lines.empty()) {
            lines[0] = "0.0 0 0.0";
        }
        if (lines.size() > 1) {
            lines[1] = "0";
        }
        std::ostringstream out;
        for (const std::string &l : lines) {
            out << l << "\n";
        }
        writeText(target, out.str());
    }
    bindMountTemplate(cfg, templateDir);
    writeConfFile(cfg);
    return {};
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        out.push_back(line);
    }
    return out;
}

std::string joinLines(const std::vector<std::string> &lines)
{
    std::ostringstream out;
    for (const std::string &line : lines) {
        out << line << "\n";
    }
    return out.str();
}

void replaceOrAppendKey(std::vector<std::string> &lines, const std::string &key, const std::string &value)
{
    const std::string prefix = key + "=";
    for (std::string &line : lines) {
        if (line.rfind(prefix, 0) == 0 || line.rfind("#" + prefix, 0) == 0) {
            line = value;
            return;
        }
    }
    lines.push_back(value);
}

Result doTdmNoAutologin(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string orig = "/etc/trinity/tdm/tdmrc";
    if (!fs::exists(orig)) {
        return {};
    }
    const std::string templateDir = cfg.workDir + "/tdmnoautologin";
    const std::string target = templateDir + orig;
    copyFilePreserve(orig, target);
    auto lines = splitLines(readText(target).value_or(""));
    replaceOrAppendKey(lines, "AutoLoginEnable", "#AutoLoginEnable=false");
    replaceOrAppendKey(lines, "AutoLoginUser", "#AutoLoginUser=demo");
    writeText(target, joinLines(lines));
    bindMountTemplate(cfg, templateDir);
    writeConfFile(cfg);
    return {};
}

Result doSddmNoAutologin(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string orig = "/etc/sddm.conf";
    if (!fs::exists(orig)) {
        return {};
    }
    const std::string templateDir = cfg.workDir + "/sddmnoautologin";
    const std::string target = templateDir + orig;
    copyFilePreserve(orig, target);
    auto lines = splitLines(readText(target).value_or(""));
    bool inAutologin = false;
    bool sawUser = false;
    bool sawSession = false;
    for (std::string &line : lines) {
        if (!line.empty() && line.front() == '[') {
            if (inAutologin && !sawUser) {
                line = "User=\n" + line;
                sawUser = true;
            }
            inAutologin = (line == "[Autologin]");
            continue;
        }
        if (!inAutologin) {
            continue;
        }
        if (line.rfind("User=", 0) == 0) {
            line = "User=";
            sawUser = true;
        } else if (line.rfind("Session=", 0) == 0) {
            line = "Session=";
            sawSession = true;
        }
    }
    if (!inAutologin && !sawUser && !sawSession) {
        lines.push_back("[Autologin]");
    }
    if (!sawUser) {
        lines.push_back("User=");
    }
    if (!sawSession) {
        lines.push_back("Session=");
    }
    writeText(target, joinLines(lines));
    bindMountTemplate(cfg, templateDir);
    writeConfFile(cfg);
    return {};
}

Result doResumeDisable(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string orig = "/etc/initramfs-tools/conf.d/resume";
    if (!fs::exists(orig)) {
        return {};
    }
    const std::string templateDir = cfg.workDir + "/resumedisable";
    touchFile(templateDir + orig);
    bindMountTemplate(cfg, templateDir);
    writeConfFile(cfg);
    return {};
}

std::string removeBootOption(std::string text, const std::string &key)
{
    std::vector<std::string> kept;
    std::stringstream ss(text);
    std::string word;
    const std::string prefix = key + "=";
    while (ss >> word) {
        if (word.rfind(prefix, 0) != 0) {
            kept.push_back(word);
        }
    }
    std::ostringstream out;
    for (const std::string &item : kept) {
        if (out.tellp() > 0) {
            out << " ";
        }
        out << item;
    }
    return out.str();
}

Result doGrubDefault(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string orig = "/etc/default/grub";
    if (!fs::exists(orig)) {
        return {};
    }
    const std::string templateDir = cfg.workDir + "/grubdefault";
    const std::string target = templateDir + orig;
    copyFilePreserve(orig, target);
    std::vector<std::string> out;
    for (std::string line : splitLines(readText(target).value_or(""))) {
        if (line.rfind("GRUB_ENABLE_CRYPTODISK", 0) == 0) {
            continue;
        }
        const std::string prefix = "GRUB_CMDLINE_LINUX_DEFAULT=";
        if (line.rfind(prefix, 0) == 0) {
            std::string value = line.substr(prefix.size());
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            value = removeBootOption(removeBootOption(removeBootOption(value, "cryptdevice"), "root"), "resume");
            line = prefix + "\"" + trim(value) + "\"";
        }
        out.push_back(line);
    }
    writeText(target, joinLines(out));
    bindMountTemplate(cfg, templateDir);
    writeConfFile(cfg);
    return {};
}

std::string cryptPassword(const std::string &password)
{
#if __has_include(<crypt.h>)
    const std::string salt = "$6$" + randomHex32().substr(0, 16) + "$";
    if (char *hashed = ::crypt(password.c_str(), salt.c_str())) {
        return hashed;
    }
#endif
    return "!";
}

void removeHomeShellUsers(std::vector<std::string> &lines, const std::string &keepUser)
{
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string &line) {
                    std::vector<std::string> fields;
                    std::stringstream ss(line);
                    std::string field;
                    while (std::getline(ss, field, ':')) {
                        fields.push_back(field);
                    }
                    if (fields.size() < 7 || fields[0] == keepUser) {
                        return false;
                    }
                    const bool homeUser = fields[5].rfind("/home/", 0) == 0;
                    const bool shellUser = fields[6].size() >= 2 && fields[6].substr(fields[6].size() - 2) == "sh";
                    return homeUser && shellUser;
                }),
                lines.end());
}

void upsertColonLine(std::vector<std::string> &lines, const std::string &user, const std::string &line)
{
    const std::string prefix = user + ":";
    for (std::string &existing : lines) {
        if (existing.rfind(prefix, 0) == 0) {
            existing = line;
            return;
        }
    }
    lines.push_back(line);
}

Result doPasswd(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }

    const std::string bindFrom = cfg.workDir + "/bind";
    const std::string user = cfg.userPw.substr(0, cfg.userPw.find(':'));
    const std::string userPass = cfg.userPw.find(':') == std::string::npos ? user : cfg.userPw.substr(cfg.userPw.find(':') + 1);
    const std::string rootPass = cfg.rootPw.find(':') == std::string::npos ? "root" : cfg.rootPw.substr(cfg.rootPw.find(':') + 1);
    const std::string shell = fs::exists("/usr/bin/bash") ? "/usr/bin/bash" : "/bin/bash";
    const std::vector<std::string> files {"/etc/passwd", "/etc/shadow", "/etc/gshadow", "/etc/group", "/etc/subuid",
                                          "/etc/subgid"};

    for (const std::string &file : files) {
        const std::string source = joinRoot(cfg.bindRoot, file);
        if (!fs::exists(source)) {
            continue;
        }
        const std::string dest = bindFrom + file;
        if (!fs::exists(cfg.templateDir + file)) {
            copyFilePreserve(source, dest);
        }
    }

    {
        const std::string path = bindFrom + "/etc/passwd";
        auto lines = splitLines(readText(path).value_or(""));
        removeHomeShellUsers(lines, user);
        upsertColonLine(lines, user, user + ":x:1000:1000:" + user + ":/home/" + user + ":" + shell);
        writeText(path, joinLines(lines));
    }
    {
        const std::string path = bindFrom + "/etc/group";
        auto lines = splitLines(readText(path).value_or(""));
        upsertColonLine(lines, user, user + ":x:1000:");
        writeText(path, joinLines(lines));
    }
    {
        const std::string path = bindFrom + "/etc/shadow";
        if (fs::exists(path)) {
            auto lines = splitLines(readText(path).value_or(""));
            upsertColonLine(lines, user, user + ":" + cryptPassword(userPass) + ":19000:0:99999:7:::");
            for (std::string &line : lines) {
                if (line.rfind("root:", 0) == 0) {
                    const std::size_t first = line.find(':');
                    const std::size_t second = line.find(':', first + 1);
                    line = "root:" + cryptPassword(rootPass)
                           + (second == std::string::npos ? ":19000:0:99999:7:::" : line.substr(second));
                }
            }
            writeText(path, joinLines(lines));
        }
    }
    {
        const std::string path = bindFrom + "/etc/gshadow";
        if (fs::exists(path)) {
            auto lines = splitLines(readText(path).value_or(""));
            upsertColonLine(lines, user, user + ":!::");
            writeText(path, joinLines(lines));
        }
    }

    for (const std::string &file : files) {
        const std::string from = bindFrom + file;
        const std::string to = joinRoot(cfg.bindRoot, file);
        if (!fs::exists(from) || !fs::exists(to)) {
            continue;
        }
        if (!cfg.pretend && !doMountBind(from, to)) {
            return fatal("Could not bind password file " + from + " to " + to);
        }
        const std::string backup = to + "-";
        if (fs::exists(backup) && !cfg.pretend && !doMountBind(from, backup)) {
            return fatal("Could not bind password backup file " + from + " to " + backup);
        }
    }

    writeConfFile(cfg);
    return {};
}

Result doRepo(Config &cfg)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string realDir = "/etc/apt/sources.list.d";
    const std::string copyDir = cfg.workDir + "/repo" + realDir;
    ensureDir(copyDir);
    std::error_code ec;
    if (fs::is_directory(realDir, ec)) {
        for (const auto &entry : fs::directory_iterator(realDir, ec)) {
            if (entry.path().extension() == ".list") {
                copyFilePreserve(entry.path().string(), copyDir + "/" + entry.path().filename().string());
            }
        }
    }
    bindMountTemplate(cfg, cfg.workDir + "/repo");
    writeConfFile(cfg);
    return {};
}

Result doTimezone(Config &cfg, const std::string &timezone)
{
    if (!readConfFile(cfg)) {
        return fatal("Could not find config file " + cfg.confFile());
    }
    const std::string zoneFile = joinRoot(cfg.realRoot, "/usr/share/zoneinfo/" + timezone);
    const std::string localFile = joinRoot(cfg.realRoot, "/etc/localtime");
    const std::string tzFile = joinRoot(cfg.realRoot, "/etc/timezone");
    if (!fs::exists(zoneFile)) {
        error("Invalid timezone: " + timezone);
        return {};
    }
    if (readText(tzFile).value_or("") == timezone) {
        return {};
    }
    const std::string templateDir = cfg.workDir + "/tz";
    writeText(templateDir + "/etc/timezone", timezone + "\n");
    if (fs::exists(localFile) && !fs::is_symlink(localFile)) {
        copyFilePreserve(localFile, templateDir + "/etc/localtime");
    }
    bindMountTemplate(cfg, templateDir);
    writeConfFile(cfg);
    return {};
}

Result doGeneral(Config &cfg)
{
    if (Result r = doEmpty(cfg, "/etc/modprobe.d"); !r.ok) return r;
    if (Result r = doEmpty(cfg, "/etc/grub.d"); !r.ok) return r;
    if (Result r = doEmpty(cfg, "/etc/network/interfaces.d"); !r.ok) return r;
    if (Result r = doFiles(cfg, cfg.templateDir + "/files", cfg.workDir + "/live-files"); !r.ok) return r;
    if (Result r = doFiles(cfg, cfg.templateDir + "/general-files", cfg.workDir + "/general-files"); !r.ok) return r;
    if (Result r = doPasswd(cfg); !r.ok) return r;
    if (Result r = doRepo(cfg); !r.ok) return r;
    if (Result r = doTimezone(cfg, "America/New_York"); !r.ok) return r;
    writeConfFile(cfg);
    return {};
}

Result doCleanup(Config &cfg)
{
    (void)readConfFile(cfg, cfg.force);
    if (isMounted(cfg.bindRoot) && !cfg.pretend) {
        umountRecursive(cfg.bindRoot);
    }
    for (int i = 0; i < 10 && isMounted(cfg.bindRoot); ++i) {
        if (!cfg.pretend) {
            umountRecursive(cfg.bindRoot);
        }
        usleep(100000);
    }
    if (!cfg.pretend && isMounted(cfg.bindRoot)) {
        return fatal("Could not umount " + cfg.bindRoot);
    }
    std::error_code ec;
    fs::remove(cfg.bindRoot, ec);
    for (auto it = cfg.rmFiles.rbegin(); it != cfg.rmFiles.rend(); ++it) {
        fs::remove(*it, ec);
    }
    for (auto it = cfg.rmDirs.rbegin(); it != cfg.rmDirs.rend(); ++it) {
        fs::remove(*it, ec);
    }
    fs::remove(cfg.confFile(), ec);
    fs::remove_all(cfg.workDir, ec);
    return {};
}

Result doExclude(Config &cfg, const std::string &prefix, const std::vector<std::string> &files)
{
    std::set<std::string> lines;
    int missing = 0;
    for (std::string file : files) {
        if (file.empty()) {
            continue;
        }
        if (file.front() != '/' && file.front() != '.') {
            file = cfg.excludesDir + "/" + file;
        }
        const auto text = readText(file);
        if (!text) {
            error("Exclude file " + file + " does not exist");
            ++missing;
            continue;
        }
        std::stringstream ss(*text);
        std::string line;
        while (std::getline(ss, line)) {
            const std::size_t hash = line.find('#');
            if (hash != std::string::npos) {
                line = line.substr(0, hash);
            }
            line = trim(line);
            while (!line.empty() && line.front() == '/') {
                line.erase(line.begin());
            }
            if (!line.empty()) {
                lines.insert(prefix + line);
            }
        }
    }

    std::ostream *out = &std::cout;
    std::ofstream fileOut;
    if (!cfg.outputFile.empty()) {
        fileOut.open(cfg.outputFile, std::ios::trunc);
        out = &fileOut;
    }
    for (const std::string &line : lines) {
        *out << line << "\n";
    }
    if (missing == 0) return {};
    if (missing == 1) return fatal("One missing exclude file", 2);
    return fatal(std::to_string(missing) + " missing exclude files", 2);
}

Result runCommand(Config &cfg, const std::string &cmd, const std::vector<std::string> &remaining)
{
    const std::string value = cmd.find('=') == std::string::npos ? std::string() : cmd.substr(cmd.find('=') + 1);
    if (cmd == "start") return doStart(cfg);
    if (cmd == "read-only") return doReadOnly(cfg, true);
    if (cmd == "read-write") return doReadOnly(cfg, false);
    if (cmd == "cleanup") return doCleanup(cfg);
    if (cmd == "live-files") return doFiles(cfg, cfg.templateDir + "/files", cfg.workDir + "/live-files");
    if (cmd == "general-files") return doFiles(cfg, cfg.templateDir + "/general-files", cfg.workDir + "/general-files");
    if (cmd == "general") return doGeneral(cfg);
    if (cmd == "passwd") return doPasswd(cfg);
    if (cmd.rfind("bind=", 0) == 0) return doBind(cfg, value);
    if (cmd.rfind("empty=", 0) == 0) return doEmpty(cfg, value);
    if (cmd == "version-file") return doVersionFile(cfg, "");
    if (cmd.rfind("version-file=", 0) == 0) return doVersionFile(cfg, value);
    if (cmd == "adjtime") return doAdjtime(cfg);
    if (cmd == "tdmnoautologin") return doTdmNoAutologin(cfg);
    if (cmd == "sddmnoautologin") return doSddmNoAutologin(cfg);
    if (cmd == "resumedisable") return doResumeDisable(cfg);
    if (cmd == "grubdefault") return doGrubDefault(cfg);
    if (cmd == "repo") return doRepo(cfg);
    if (cmd.rfind("repo=", 0) == 0) return doRepo(cfg);
    if (cmd == "timezone") return doTimezone(cfg, "America/New_York");
    if (cmd.rfind("timezone=", 0) == 0) return doTimezone(cfg, value);
    if (cmd == "exclude") return doExclude(cfg, "", remaining);
    if (cmd.rfind("exclude=", 0) == 0) return doExclude(cfg, value, remaining);
    return fatal("Unexpected command: " + cmd + ".");
}

Result parseAndRun(const std::vector<std::string> &args)
{
    if (args.empty()) {
        return fatal("Expected as least one commmand");
    }

    Config cfg;
    std::size_t i = 0;
    while (i < args.size() && !args[i].empty() && args[i][0] == '-') {
        std::string param = args[i++];
        while (!param.empty() && param.front() == '-') {
            param.erase(param.begin());
        }
        std::string value;
        const std::size_t eq = param.find('=');
        if (eq != std::string::npos) {
            value = param.substr(eq + 1);
            param = param.substr(0, eq);
        }

        const auto needValue = [&]() -> std::optional<std::string> {
            if (!value.empty()) {
                return value;
            }
            if (i >= args.size()) {
                return std::nullopt;
            }
            return args[i++];
        };

        if (param == "b" || param == "bind-root") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.bindRoot = *v;
        } else if (param == "e" || param == "excludes-dir") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.excludesDir = *v;
        } else if (param == "f" || param == "from") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.realRoot = *v;
        } else if (param == "F" || param == "Force") {
            cfg.force = true;
        } else if (param == "o" || param == "output") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.outputFile = *v;
        } else if (param == "p" || param == "pretend") {
            cfg.pretend = true;
        } else if (param == "r" || param == "root") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.rootPw = *v;
        } else if (param == "t" || param == "template") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.templateDir = *v;
        } else if (param == "u" || param == "user") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.userPw = *v;
        } else if (param == "v" || param == "verbose") {
            cfg.verbose = true;
        } else if (param == "w" || param == "work") {
            auto v = needValue();
            if (!v) return fatal("Expected a parameter after: -" + param);
            cfg.workDir = *v;
        } else if (param == "h" || param == "help") {
            info("Usage: installed-to-live [options] [commands ...]");
            return {};
        } else {
            return fatal("Unknown argument: -" + param);
        }
    }

    if (i >= args.size()) {
        return fatal("Expected as least one commmand");
    }

    while (i < args.size()) {
        const std::string cmd = args[i++];
        std::vector<std::string> remaining(args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
        Result r = runCommand(cfg, cmd, remaining);
        if (!r.ok || cmd == "exclude" || cmd.rfind("exclude=", 0) == 0) {
            return r;
        }
    }
    return {};
}

} // namespace

int InstalledToLiveCpp::run(const std::vector<std::string> &args)
{
    const Result r = parseAndRun(args);
    return r.ok ? 0 : r.code;
}
