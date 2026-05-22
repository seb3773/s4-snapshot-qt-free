#pragma once

#include <string>
#include <functional>

class TempDir
{
public:
#ifdef UNIT_TESTS
    struct Hooks {
        std::function<bool(const std::string &path)> removeRecursively;
    };
    static void setHooksForTests(const Hooks *hooks);

    static bool removeRecursivelyForTests(const std::string &path);
#endif

    TempDir();
    explicit TempDir(const std::string &template_path);

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    TempDir(TempDir &&other) noexcept;
    TempDir &operator=(TempDir &&other) noexcept;

    ~TempDir();

    [[nodiscard]] bool isValid() const { return valid; }
    [[nodiscard]] std::string errorString() const { return valid ? std::string() : pathOrError; }
    [[nodiscard]] bool autoRemove() const { return autoRemoveEnabled; }
    void setAutoRemove(bool b) { autoRemoveEnabled = b; }
    [[nodiscard]] const std::string &path() const { return dirPath; }

    [[nodiscard]] std::string filePath(const std::string &fileName) const;

    bool remove();

private:
    std::string dirPath;
    std::string pathOrError;
    bool valid = false;
    bool removed = false;
    bool autoRemoveEnabled = true;
};
