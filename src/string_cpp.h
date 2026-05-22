#pragma once

#include <string>
#include <vector>

class StringCpp
{
public:
    enum class SplitBehavior {
        KeepEmptyParts,
        SkipEmptyParts,
    };

    [[nodiscard]] static std::vector<std::string> splitLikeQString(const std::string &source,
                                                                  const std::string &sep,
                                                                  SplitBehavior behavior);

    [[nodiscard]] static std::string trimmedLikeQStringUtf8(const std::string &s);

    [[nodiscard]] static bool startsWithLikeQStringUtf8(
        const std::string &s,
        const std::string &prefix);

    [[nodiscard]] static bool endsWithLikeQStringUtf8(
        const std::string &s,
        const std::string &suffix);

    [[nodiscard]] static bool startsWithLikeQStringUtf8(
        const std::string &s,
        char prefix);

    [[nodiscard]] static bool endsWithLikeQStringUtf8(
        const std::string &s,
        char suffix);

    // QString::remove(qsizetype pos, qsizetype len) semantics, but for UTF-8 std::string.
    // Indexing is in UTF-16 code units (QString indexing model).
    [[nodiscard]] static std::string removeLikeQStringUtf8(const std::string &s, int pos, int len);

    // QString::remove(const QString &needle, Qt::CaseSensitive) semantics: remove all occurrences.
    [[nodiscard]] static std::string removeAllLikeQStringUtf8(const std::string &s, const std::string &needle);
};
