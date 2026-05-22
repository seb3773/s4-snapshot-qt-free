#include "string_cpp.h"

#include <cstddef>

namespace {

static bool utf8_decode_next(const std::string &s, std::size_t &i, char32_t &out)
{
    if (i >= s.size()) {
        return false;
    }

    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        out = c0;
        ++i;
        return true;
    }

    // Invalid leading byte or truncated sequences are treated as individual bytes.
    auto take_one = [&]() {
        out = c0;
        ++i;
        return true;
    };

    const std::size_t remaining = s.size() - i;
    if ((c0 & 0xE0) == 0xC0) {
        if (remaining < 2) {
            return take_one();
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80) {
            return take_one();
        }
        out = (char32_t(c0 & 0x1F) << 6) | char32_t(c1 & 0x3F);
        i += 2;
        return true;
    }
    if ((c0 & 0xF0) == 0xE0) {
        if (remaining < 3) {
            return take_one();
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) {
            return take_one();
        }
        out = (char32_t(c0 & 0x0F) << 12) | (char32_t(c1 & 0x3F) << 6) | char32_t(c2 & 0x3F);
        i += 3;
        return true;
    }
    if ((c0 & 0xF8) == 0xF0) {
        if (remaining < 4) {
            return take_one();
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) {
            return take_one();
        }
        out = (char32_t(c0 & 0x07) << 18) | (char32_t(c1 & 0x3F) << 12) | (char32_t(c2 & 0x3F) << 6)
              | char32_t(c3 & 0x3F);
        i += 4;
        return true;
    }

    return take_one();
}

static std::size_t utf16_units_for_codepoint(char32_t cp)
{
    if (cp > 0x10FFFF) {
        return 1;
    }
    return (cp >= 0x10000 ? 2 : 1);
}

static std::size_t utf16_length_of_utf8_approx(const std::string &s)
{
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp = 0;
        (void)utf8_decode_next(s, i, cp);
        units += utf16_units_for_codepoint(cp);
    }
    return units;
}

static bool utf8_byte_offset_for_utf16_index(const std::string &s, std::size_t utf16Index, std::size_t &byteOffset)
{
    // Maps a UTF-16 code unit index (QString indexing model) to a byte offset in UTF-8.
    // Returns false if index is beyond end.
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        if (units == utf16Index) {
            byteOffset = i;
            return true;
        }
        char32_t cp = 0;
        const std::size_t before = i;
        (void)utf8_decode_next(s, i, cp);
        const std::size_t add = utf16_units_for_codepoint(cp);
        if (units < utf16Index && units + add > utf16Index) {
            // Index points into the middle of a surrogate pair (Qt index points to a QChar).
            // Treat it as pointing to the surrogate pair boundary.
            byteOffset = before;
            return true;
        }
        units += add;
    }
    if (units == utf16Index) {
        byteOffset = s.size();
        return true;
    }
    return false;
}

static bool is_qchar_space_approx(char32_t cp)
{
    // Unicode whitespace set commonly used for trimming (aligned with Qt docs:
    // "Whitespace means any character for which QChar::isSpace() returns true").
    // This includes ASCII: \t \n \v \f \r and space.
    if (cp == 0x0009 || cp == 0x000A || cp == 0x000B || cp == 0x000C || cp == 0x000D || cp == 0x0020) {
        return true;
    }
    // Additional Unicode space separators and line separators.
    if (cp == 0x0085 || cp == 0x00A0 || cp == 0x1680 || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F
        || cp == 0x3000) {
        return true;
    }
    if (cp >= 0x2000 && cp <= 0x200A) {
        return true;
    }
    return false;
}

} // namespace

std::vector<std::string> StringCpp::splitLikeQString(const std::string &source,
                                                    const std::string &sep,
                                                    SplitBehavior behavior)
{
    std::vector<std::string> out;

    // Matches Qt behavior for QString::split(QString sep, behavior, CaseSensitive) for cs=CaseSensitive.
    // See _ref_src_qt6/qstring.cpp: splitString<ResultList>(source, sep, behavior, cs)
    std::size_t start = 0;

    if (sep.empty()) {
        // Qt behavior: empty string, then each character, then empty string.
        // For std::string we consider bytes as "characters".
        out.reserve(source.size() + 2);
        if (behavior == SplitBehavior::KeepEmptyParts) {
            out.emplace_back(std::string());
        }
        for (const unsigned char ch : source) {
            std::string s;
            s.push_back(static_cast<char>(ch));
            out.emplace_back(std::move(s));
        }
        if (behavior == SplitBehavior::KeepEmptyParts) {
            out.emplace_back(std::string());
        }
        return out;
    }

    while (true) {
        const std::size_t end = source.find(sep, start);
        if (end == std::string::npos) {
            break;
        }

        if (start != end || behavior == SplitBehavior::KeepEmptyParts) {
            out.emplace_back(source.substr(start, end - start));
        }

        start = end + sep.size();
    }

    if (start != source.size() || behavior == SplitBehavior::KeepEmptyParts) {
        out.emplace_back(source.substr(start));
    }

    return out;
}

std::string StringCpp::trimmedLikeQStringUtf8(const std::string &s)
{
    if (s.empty()) {
        return s;
    }

    std::size_t left = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp = 0;
        (void)utf8_decode_next(s, i, cp);
        if (!is_qchar_space_approx(cp)) {
            break;
        }
        left = i;
    }

    if (left >= s.size()) {
        return std::string();
    }

    // Scan all codepoints to find last non-space end boundary.
    std::size_t lastNonSpaceEnd = left;
    std::size_t j = left;
    while (j < s.size()) {
        char32_t cp = 0;
        (void)utf8_decode_next(s, j, cp);
        if (!is_qchar_space_approx(cp)) {
            lastNonSpaceEnd = j;
        }
    }

    return s.substr(left, lastNonSpaceEnd - left);
}

bool StringCpp::startsWithLikeQStringUtf8(const std::string &s, const std::string &prefix)
{
    if (prefix.empty()) {
        return true;
    }
    if (s.size() < prefix.size()) {
        return false;
    }
    return s.compare(0, prefix.size(), prefix) == 0;
}

bool StringCpp::endsWithLikeQStringUtf8(const std::string &s, const std::string &suffix)
{
    if (suffix.empty()) {
        return true;
    }
    if (s.size() < suffix.size()) {
        return false;
    }
    const std::size_t start = s.size() - suffix.size();
    return s.compare(start, suffix.size(), suffix) == 0;
}

bool StringCpp::startsWithLikeQStringUtf8(const std::string &s, char prefix)
{
    if (s.empty()) {
        return false;
    }
    const unsigned char a = static_cast<unsigned char>(s[0]);
    const unsigned char b = static_cast<unsigned char>(prefix);
    return a == b;
}

std::string StringCpp::removeLikeQStringUtf8(const std::string &s, int pos, int len)
{
    if (s.empty()) {
        return s;
    }

    const std::size_t sz16 = utf16_length_of_utf8_approx(s);

    long long p = static_cast<long long>(pos);
    if (p < 0) {
        p += static_cast<long long>(sz16);
    }

    if (p < 0 || static_cast<std::size_t>(p) >= sz16 || len <= 0) {
        return s;
    }

    const std::size_t start16 = static_cast<std::size_t>(p);
    std::size_t l = static_cast<std::size_t>(len);
    if (start16 + l > sz16) {
        l = sz16 - start16;
    }

    std::size_t startByte = 0;
    std::size_t endByte = 0;
    if (!utf8_byte_offset_for_utf16_index(s, start16, startByte)) {
        return s;
    }
    if (!utf8_byte_offset_for_utf16_index(s, start16 + l, endByte)) {
        endByte = s.size();
    }

    std::string out;
    out.reserve(s.size());
    out.append(s.data(), startByte);
    out.append(s.data() + endByte, s.size() - endByte);
    return out;
}

std::string StringCpp::removeAllLikeQStringUtf8(const std::string &s, const std::string &needle)
{
    if (needle.empty() || s.empty()) {
        return s;
    }

    std::string out;
    out.reserve(s.size());

    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(needle, start);
        if (pos == std::string::npos) {
            break;
        }
        out.append(s, start, pos - start);
        start = pos + needle.size();
    }
    out.append(s, start, std::string::npos);
    return out;
}

bool StringCpp::endsWithLikeQStringUtf8(const std::string &s, char suffix)
{
    if (s.empty()) {
        return false;
    }
    const unsigned char a = static_cast<unsigned char>(s[s.size() - 1]);
    const unsigned char b = static_cast<unsigned char>(suffix);
    return a == b;
}
