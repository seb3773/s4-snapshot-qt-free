#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class FileCpp
{
public:
    enum class OpenMode : std::uint32_t {
        NotOpen = 0,
        ReadOnly = 0x0001,
        WriteOnly = 0x0002,
        ReadWrite = ReadOnly | WriteOnly,
        Append = 0x0004,
        Truncate = 0x0008,
        Text = 0x0010,
    };

    FileCpp();
    explicit FileCpp(const std::string &fileName);

    struct Hooks {
        std::function<bool(const std::string &fileName)> exists;
        std::function<bool(const std::string &fileName)> remove;
        std::function<bool(const std::string &sourceFileName, const std::string &destFileName)> copy;
        std::function<bool(const std::string &sourceFileName, const std::string &linkName)> link;
    };

    static void setHooksForTests(const Hooks *hooks);

    FileCpp(const FileCpp &) = delete;
    FileCpp &operator=(const FileCpp &) = delete;

    FileCpp(FileCpp &&other) noexcept;
    FileCpp &operator=(FileCpp &&other) noexcept;

    ~FileCpp();

    void setFileName(const std::string &name);
    [[nodiscard]] const std::string &fileName() const;

    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] bool open(OpenMode mode);
    void close();

    [[nodiscard]] bool exists() const;
    [[nodiscard]] static bool exists(const std::string &fileName);

    [[nodiscard]] static bool remove(const std::string &fileName);
    [[nodiscard]] static bool copy(const std::string &sourceFileName, const std::string &destFileName);
    [[nodiscard]] static bool link(const std::string &sourceFileName, const std::string &linkName);

    [[nodiscard]] static bool isFile(const std::string &fileName);
    [[nodiscard]] static bool isDir(const std::string &fileName);
    [[nodiscard]] static bool isSymLink(const std::string &fileName);

    [[nodiscard]] static std::string baseName(const std::string &fileName);
    [[nodiscard]] static std::string completeBaseName(const std::string &fileName);
    [[nodiscard]] static std::string completeSuffix(const std::string &fileName);

    [[nodiscard]] static std::string fileNameComponent(const std::string &fileName);

    [[nodiscard]] static std::string qtFileName(const std::string &storedName);

    [[nodiscard]] static std::int64_t size(const std::string &fileName);

    [[nodiscard]] static std::int64_t lastReadSecsSinceEpoch(const std::string &fileName);

    [[nodiscard]] static std::int64_t lastModifiedSecsSinceEpoch(const std::string &fileName);

    [[nodiscard]] std::vector<std::uint8_t> readAll();
    [[nodiscard]] std::string readLine();

    [[nodiscard]] std::int64_t write(const std::uint8_t *data, std::size_t n);
    [[nodiscard]] std::int64_t write(const std::string &s);

    [[nodiscard]] bool flush();

    [[nodiscard]] std::string errorString() const;

private:
    [[nodiscard]] bool applyTextModeInPlace(std::vector<std::uint8_t> &buf) const;
    [[nodiscard]] bool applyTextModeLineInPlace(std::string &line) const;

    std::string name;
    int fd = -1;
    OpenMode mode = OpenMode::NotOpen;
    static std::string lastError;
};

inline FileCpp::OpenMode operator|(FileCpp::OpenMode a, FileCpp::OpenMode b)
{
    return static_cast<FileCpp::OpenMode>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline bool operator&(FileCpp::OpenMode a, FileCpp::OpenMode b)
{
    return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}
