#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QmTranslatorCpp
{
public:
    QmTranslatorCpp();

    [[nodiscard]] bool loadFile(const std::string &filePath);

    [[nodiscard]] std::optional<std::string> translate(std::string_view context,
                                                       std::string_view sourceText,
                                                       std::string_view comment = std::string_view(),
                                                       int n = -1) const;

private:
    std::vector<std::uint8_t> data;

    const std::uint8_t *messageArray = nullptr;
    const std::uint8_t *offsetArray = nullptr;
    const std::uint8_t *contextArray = nullptr;
    const std::uint8_t *numerusRulesArray = nullptr;

    std::uint32_t messageLength = 0;
    std::uint32_t offsetLength = 0;
    std::uint32_t contextLength = 0;
    std::uint32_t numerusRulesLength = 0;

    [[nodiscard]] static std::uint8_t read8(const std::uint8_t *p);
    [[nodiscard]] static std::uint16_t read16(const std::uint8_t *p);
    [[nodiscard]] static std::uint32_t read32(const std::uint8_t *p);

    [[nodiscard]] static std::uint32_t elfHash(const char *s);
    static void elfHash_continue(const char *s, std::uint32_t &h);
    static void elfHash_finish(std::uint32_t &h);

    [[nodiscard]] static bool match(const std::uint8_t *found, std::uint32_t foundLen, const char *target, std::uint32_t targetLen);

    [[nodiscard]] static bool isValidNumerusRules(const std::uint8_t *rules, std::uint32_t rulesSize);
    [[nodiscard]] static std::uint32_t numerusHelper(int n, const std::uint8_t *rules, std::uint32_t rulesSize);

    [[nodiscard]] static std::optional<std::string> getMessage(const std::uint8_t *m,
                                                               const std::uint8_t *end,
                                                               const char *context,
                                                               const char *sourceText,
                                                               const char *comment,
                                                               std::uint32_t numerus);

    [[nodiscard]] std::optional<std::string> doTranslate(const char *context,
                                                         const char *sourceText,
                                                         const char *comment,
                                                         int n) const;
};
