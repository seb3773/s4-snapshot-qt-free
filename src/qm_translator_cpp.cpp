#include "qm_translator_cpp.h"

#include "file_cpp.h"

#include <cstring>

namespace {

constexpr int kMagicLength = 16;
constexpr std::uint8_t kMagic[kMagicLength] = {
    0x3c, 0xb8, 0x64, 0x18, 0xca, 0xef, 0x9c, 0x95,
    0xcd, 0x21, 0x1c, 0xbf, 0x60, 0xa1, 0xbd, 0xdd
};

enum class Tag : std::uint8_t {
    End = 1,
    SourceText16 = 2,
    Translation = 3,
    Context16 = 4,
    Obsolete1 = 5,
    SourceText = 6,
    Context = 7,
    Comment = 8,
    Obsolete2 = 9,
};

constexpr std::uint8_t Q_OP_MASK = 0x07;
constexpr std::uint8_t Q_EQ = 0x00;
constexpr std::uint8_t Q_LT = 0x01;
constexpr std::uint8_t Q_LEQ = 0x02;
constexpr std::uint8_t Q_BETWEEN = 0x03;
constexpr std::uint8_t Q_NOT = 0x08;
constexpr std::uint8_t Q_MOD_10 = 0x10;
constexpr std::uint8_t Q_MOD_100 = 0x20;
constexpr std::uint8_t Q_LEAD_1000 = 0x40;
constexpr std::uint8_t Q_AND = 0x08;
constexpr std::uint8_t Q_OR = 0x09;
constexpr std::uint8_t Q_NEWRULE = 0x0a;

} // namespace

QmTranslatorCpp::QmTranslatorCpp() = default;

std::uint8_t QmTranslatorCpp::read8(const std::uint8_t *p)
{
    return p[0];
}

std::uint16_t QmTranslatorCpp::read16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8)
                                      | static_cast<std::uint16_t>(p[1]));
}

std::uint32_t QmTranslatorCpp::read32(const std::uint8_t *p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24)
           | (static_cast<std::uint32_t>(p[1]) << 16)
           | (static_cast<std::uint32_t>(p[2]) << 8)
           | static_cast<std::uint32_t>(p[3]);
}

bool QmTranslatorCpp::loadFile(const std::string &filePath)
{
    FileCpp f(filePath);
    if (!f.open(FileCpp::OpenMode::ReadOnly)) {
        return false;
    }

    data = f.readAll();
    f.close();

    if (data.size() < static_cast<std::size_t>(kMagicLength)) {
        return false;
    }
    if (std::memcmp(data.data(), kMagic, kMagicLength) != 0) {
        return false;
    }

    messageArray = nullptr;
    offsetArray = nullptr;
    contextArray = nullptr;
    numerusRulesArray = nullptr;
    messageLength = 0;
    offsetLength = 0;
    contextLength = 0;
    numerusRulesLength = 0;

    const std::uint8_t *p = data.data() + kMagicLength;
    const std::uint8_t *end = data.data() + data.size();

    bool ok = true;

    while (p < end - 5) {
        const std::uint8_t tag = read8(p++);
        const std::uint32_t blockLen = read32(p);
        p += 4;

        if (tag == 0 || blockLen == 0) {
            break;
        }
        if (static_cast<std::uint64_t>(end - p) < static_cast<std::uint64_t>(blockLen)) {
            ok = false;
            break;
        }

        // Translator blocks (same numeric tags as Qt)
        // Contexts = 0x2f, Hashes = 0x42, Messages = 0x69, NumerusRules = 0x88.
        if (tag == 0x2f) {
            contextArray = p;
            contextLength = blockLen;
        } else if (tag == 0x42) {
            offsetArray = p;
            offsetLength = blockLen;
        } else if (tag == 0x69) {
            messageArray = p;
            messageLength = blockLen;
        } else if (tag == 0x88) {
            numerusRulesArray = p;
            numerusRulesLength = blockLen;
        }

        p += blockLen;
    }

    if (!ok) {
        messageArray = nullptr;
        offsetArray = nullptr;
        contextArray = nullptr;
        numerusRulesArray = nullptr;
        messageLength = 0;
        offsetLength = 0;
        contextLength = 0;
        numerusRulesLength = 0;
    }

    return ok;
}

std::optional<std::string> QmTranslatorCpp::translate(std::string_view context,
                                                     std::string_view sourceText,
                                                     std::string_view comment,
                                                     int n) const
{
    return doTranslate(context.data(), sourceText.data(), comment.data(), n);
}

void QmTranslatorCpp::elfHash_continue(const char *name, std::uint32_t &h)
{
    const unsigned char *k = reinterpret_cast<const unsigned char *>(name);
    std::uint32_t g = 0;
    while (*k) {
        h = (h << 4) + *k++;
        if ((g = (h & 0xf0000000U)) != 0) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
}

void QmTranslatorCpp::elfHash_finish(std::uint32_t &h)
{
    if (h == 0) {
        h = 1;
    }
}

std::uint32_t QmTranslatorCpp::elfHash(const char *name)
{
    std::uint32_t h = 0;
    elfHash_continue(name, h);
    elfHash_finish(h);
    return h;
}

bool QmTranslatorCpp::match(const std::uint8_t *found, std::uint32_t foundLen, const char *target, std::uint32_t targetLen)
{
    if (foundLen > 0 && found[foundLen - 1] == '\0') {
        --foundLen;
    }
    if (foundLen != targetLen) {
        return false;
    }
    return std::memcmp(found, target, foundLen) == 0;
}

bool QmTranslatorCpp::isValidNumerusRules(const std::uint8_t *rules, std::uint32_t rulesSize)
{
    if (rulesSize == 0) {
        return true;
    }

    std::uint32_t offset = 0;
    do {
        const std::uint8_t opcode = rules[offset];
        const std::uint8_t op = opcode & Q_OP_MASK;
        if (opcode & 0x80) {
            return false;
        }
        if (++offset == rulesSize) {
            return false;
        }
        ++offset;
        switch (op) {
        case Q_EQ:
        case Q_LT:
        case Q_LEQ:
            break;
        case Q_BETWEEN:
            if (offset != rulesSize) {
                ++offset;
                break;
            }
            return false;
        default:
            return false;
        }

        if (offset == rulesSize) {
            return true;
        }
    } while (((rules[offset] == Q_AND) || (rules[offset] == Q_OR) || (rules[offset] == Q_NEWRULE)) && ++offset != rulesSize);

    return false;
}

std::uint32_t QmTranslatorCpp::numerusHelper(int n, const std::uint8_t *rules, std::uint32_t rulesSize)
{
    std::uint32_t result = 0;
    std::uint32_t i = 0;

    if (rulesSize == 0) {
        return 0;
    }

    for (;;) {
        bool orExprTruthValue = false;
        for (;;) {
            bool andExprTruthValue = true;
            for (;;) {
                bool truthValue = true;
                const int opcode = rules[i++];
                int leftOperand = n;
                if (opcode & Q_MOD_10) {
                    leftOperand %= 10;
                } else if (opcode & Q_MOD_100) {
                    leftOperand %= 100;
                } else if (opcode & Q_LEAD_1000) {
                    while (leftOperand >= 1000) {
                        leftOperand /= 1000;
                    }
                }

                const int op = opcode & Q_OP_MASK;
                const int rightOperand = rules[i++];

                switch (op) {
                case Q_EQ:
                    truthValue = (leftOperand == rightOperand);
                    break;
                case Q_LT:
                    truthValue = (leftOperand < rightOperand);
                    break;
                case Q_LEQ:
                    truthValue = (leftOperand <= rightOperand);
                    break;
                case Q_BETWEEN: {
                    const int bottom = rightOperand;
                    const int top = rules[i++];
                    truthValue = (leftOperand >= bottom && leftOperand <= top);
                    break;
                }
                default:
                    truthValue = false;
                    break;
                }

                if (opcode & Q_NOT) {
                    truthValue = !truthValue;
                }

                andExprTruthValue = andExprTruthValue && truthValue;

                if (i == rulesSize || rules[i] != Q_AND) {
                    break;
                }
                ++i;
            }

            orExprTruthValue = orExprTruthValue || andExprTruthValue;

            if (i == rulesSize || rules[i] != Q_OR) {
                break;
            }
            ++i;
        }

        if (orExprTruthValue) {
            return result;
        }

        ++result;
        if (i == rulesSize) {
            return result;
        }

        i++; // Q_NEWRULE
    }
}

std::optional<std::string> QmTranslatorCpp::getMessage(const std::uint8_t *m,
                                                       const std::uint8_t *end,
                                                       const char *context,
                                                       const char *sourceText,
                                                       const char *comment,
                                                       std::uint32_t numerus)
{
    const std::uint8_t *tn = nullptr;
    std::uint32_t tn_length = 0;

    const std::uint32_t sourceTextLen = static_cast<std::uint32_t>(std::strlen(sourceText));
    const std::uint32_t contextLen = static_cast<std::uint32_t>(std::strlen(context));
    const std::uint32_t commentLen = static_cast<std::uint32_t>(std::strlen(comment));

    for (;;) {
        std::uint8_t tag = 0;
        if (m < end) {
            tag = read8(m++);
        }

        switch (static_cast<Tag>(tag)) {
        case Tag::End:
            goto done;
        case Tag::Translation: {
            const std::uint32_t len = read32(m);
            if (len & 1U) {
                return std::nullopt;
            }
            m += 4;
            if (numerus == 0) {
                tn_length = len;
                tn = m;
            } else {
                --numerus;
            }
            m += len;
            break;
        }
        case Tag::Obsolete1:
            m += 4;
            break;
        case Tag::SourceText: {
            const std::uint32_t len = read32(m);
            m += 4;
            if (!match(m, len, sourceText, sourceTextLen)) {
                return std::nullopt;
            }
            m += len;
            break;
        }
        case Tag::Context: {
            const std::uint32_t len = read32(m);
            m += 4;
            if (!match(m, len, context, contextLen)) {
                return std::nullopt;
            }
            m += len;
            break;
        }
        case Tag::Comment: {
            const std::uint32_t len = read32(m);
            m += 4;
            if (*m && !match(m, len, comment, commentLen)) {
                return std::nullopt;
            }
            m += len;
            break;
        }
        default:
            return std::nullopt;
        }
    }

done:
    if (!tn) {
        return std::nullopt;
    }

    // UTF-16BE -> UTF-8
    if ((tn_length % 2U) != 0U) {
        return std::nullopt;
    }

    std::string out;
    out.reserve(tn_length);
    for (std::uint32_t i = 0; i + 1 < tn_length; i += 2) {
        const std::uint16_t u = static_cast<std::uint16_t>((static_cast<std::uint16_t>(tn[i]) << 8)
                                                           | static_cast<std::uint16_t>(tn[i + 1]));
        // Minimal UTF-16 (BMP) encoding; Qt translations here should not contain surrogate pairs.
        if (u < 0x80) {
            out.push_back(static_cast<char>(u));
        } else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }

    return out;
}

std::optional<std::string> QmTranslatorCpp::doTranslate(const char *context,
                                                        const char *sourceText,
                                                        const char *comment,
                                                        int n) const
{
    if (!offsetArray || !messageArray) {
        return std::nullopt;
    }

    if (!context) {
        context = "";
    }
    if (!sourceText) {
        sourceText = "";
    }
    if (!comment) {
        comment = "";
    }

    std::uint32_t numerus = 0;
    if (n >= 0) {
        numerus = numerusHelper(n, numerusRulesArray, numerusRulesLength);
    }

    if (contextArray && contextLength > 0) {
        const std::uint16_t hTableSize = read16(contextArray);
        const std::uint32_t g = elfHash(context) % hTableSize;
        const std::uint8_t *c = contextArray + 2 + (g << 1);
        const std::uint16_t off = read16(c);
        c += 2;
        if (off == 0) {
            return std::nullopt;
        }
        c = contextArray + (2 + (static_cast<std::uint32_t>(hTableSize) << 1) + (static_cast<std::uint32_t>(off) << 1));
        const std::uint32_t contextLen = static_cast<std::uint32_t>(std::strlen(context));
        for (;;) {
            const std::uint8_t len = read8(c++);
            if (len == 0) {
                return std::nullopt;
            }
            if (match(c, len, context, contextLen)) {
                break;
            }
            c += len;
        }
    }

    const std::uint32_t numItems = offsetLength / (2U * sizeof(std::uint32_t));
    if (numItems == 0) {
        return std::nullopt;
    }

    for (;;) {
        std::uint32_t h = 0;
        elfHash_continue(sourceText, h);
        elfHash_continue(comment, h);
        elfHash_finish(h);

        const std::uint8_t *start = offsetArray;
        const std::uint8_t *end = start + ((numItems - 1U) << 3);

        while (start <= end) {
            const std::uint8_t *middle = start + (((end - start) >> 4) << 3);
            const std::uint32_t hash = read32(middle);
            if (h == hash) {
                start = middle;
                break;
            }
            if (hash < h) {
                start = middle + 8;
            } else {
                end = middle - 8;
            }
        }

        if (start <= end) {
            while (start != offsetArray && read32(start) == read32(start - 8)) {
                start -= 8;
            }

            while (start < offsetArray + offsetLength) {
                const std::uint32_t rh = read32(start);
                start += 4;
                if (rh != h) {
                    break;
                }
                const std::uint32_t ro = read32(start);
                start += 4;

                const auto tn = getMessage(messageArray + ro,
                                           messageArray + messageLength,
                                           context,
                                           sourceText,
                                           comment,
                                           numerus);
                if (tn.has_value()) {
                    return tn;
                }
            }
        }

        if (comment[0] == '\0') {
            break;
        }
        comment = "";
    }

    return std::nullopt;
}
