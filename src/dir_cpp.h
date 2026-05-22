#pragma once

#include <functional>
#include <string>
#include <vector>

class DirCpp
{
public:
    enum class EntryType {
        Files,
        Dirs,
        All,
    };

    enum class Filter : unsigned {
        None = 0,
        Dirs = 1u << 0,
        Files = 1u << 1,
        AllEntries = 1u << 2,
        NoDotAndDotDot = 1u << 3,
        NoSymLinks = 1u << 4,
    };

    struct FileInfo {
        std::string fileName;
        std::string filePath;
        bool isDir = false;
        bool isFile = false;
        bool isSymLink = false;
    };

    DirCpp();
    explicit DirCpp(const std::string &path);

    struct Hooks {
        std::function<bool(const std::string &path)> setCurrent;
        std::function<bool(const std::string &dirPath)> mkpath;
        std::function<bool(const std::string &path)> removeRecursively;
    };

    static void setHooksForTests(const Hooks *hooks);

    void setPath(const std::string &path);
    [[nodiscard]] const std::string &path() const;

    [[nodiscard]] bool exists() const;
    [[nodiscard]] static bool exists(const std::string &path);

    [[nodiscard]] bool isEmpty() const;

    [[nodiscard]] std::string filePath(const std::string &fileName) const;

    [[nodiscard]] bool mkpath(const std::string &dirPath) const;

    [[nodiscard]] std::vector<std::string> entryList(const std::vector<std::string> &nameFilters, EntryType type,
                                                     bool sortIgnoreCase = true) const;

    [[nodiscard]] std::vector<std::string> entryInfoList(const std::vector<std::string> &nameFilters, EntryType type,
                                                         bool sortIgnoreCase = true) const;

    [[nodiscard]] std::vector<FileInfo> entryInfoList(Filter filters) const;

    [[nodiscard]] bool removeRecursively() const;

    [[nodiscard]] static bool setCurrent(const std::string &path);

    [[nodiscard]] static std::string cleanPath(const std::string &path);

    [[nodiscard]] static std::string absolutePath(const std::string &path);
    [[nodiscard]] static std::string absolutePathOfContainingDir(const std::string &filePath);

private:
    std::string p;
};
