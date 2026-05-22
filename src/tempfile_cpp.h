#pragma once

#include <string>

class TempFileCpp
{
public:
    TempFileCpp();
    explicit TempFileCpp(const std::string &templateName);

    TempFileCpp(const TempFileCpp &) = delete;
    TempFileCpp &operator=(const TempFileCpp &) = delete;

    TempFileCpp(TempFileCpp &&other) noexcept;
    TempFileCpp &operator=(TempFileCpp &&other) noexcept;

    ~TempFileCpp();

    [[nodiscard]] bool open();
    void close();

    [[nodiscard]] bool autoRemove() const { return autoRemoveEnabled; }
    void setAutoRemove(bool b) { autoRemoveEnabled = b; }

    [[nodiscard]] bool isOpen() const { return fd >= 0; }

    [[nodiscard]] const std::string &fileName() const { return filePath; }
    [[nodiscard]] std::string fileTemplate() const { return templ; }

    [[nodiscard]] std::string errorString() const { return lastError; }

    [[nodiscard]] bool writeAll(const void *data, std::size_t n);
    [[nodiscard]] bool flush();

    [[nodiscard]] bool remove();

private:
    [[nodiscard]] static std::string defaultTemplateName();
    [[nodiscard]] static std::string tempPath();

    [[nodiscard]] bool materialize();

    std::string templ;
    std::string filePath;
    std::string lastError;
    int fd = -1;
    bool autoRemoveEnabled = true;
    bool removed = false;
};
