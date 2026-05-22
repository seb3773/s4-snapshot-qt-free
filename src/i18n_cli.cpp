#include "i18n_cli.h"

#include "file_cpp.h"

#include <i18n_keyval/i18n.hpp>
#include <i18n_keyval/translators/basic.hpp>

#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

static i18n::translations g_translations;
static bool g_initialized = false;

static std::string trim_copy(const std::string &s)
{
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

static std::string unescape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());

    for (std::size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }

        if (i + 1 >= s.size()) {
            out.push_back('\\');
            break;
        }

        const char n = s[++i];
        switch (n) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '\\': out.push_back('\\'); break;
        case '|': out.push_back('|'); break;
        case '=': out.push_back('='); break;
        default:
            out.push_back(n);
            break;
        }
    }

    return out;
}

static std::string escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '|': out += "\\|"; break;
        case '=': out += "\\="; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

static bool parse_kv_file(const std::string &kvFilePath, i18n::translation_table &out)
{
    FileCpp f(kvFilePath);
    if (!f.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
        return false;
    }

    while (true) {
        const std::string lineRaw = f.readLine();
        if (lineRaw.empty()) {
            break;
        }

        std::string line = trim_copy(lineRaw);
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        // split at first unescaped '='
        std::size_t eq = std::string::npos;
        bool esc = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char ch = line[i];
            if (esc) {
                esc = false;
                continue;
            }
            if (ch == '\\') {
                esc = true;
                continue;
            }
            if (ch == '=') {
                eq = i;
                break;
            }
        }
        if (eq == std::string::npos) {
            continue;
        }

        const std::string k = unescape(trim_copy(line.substr(0, eq)));
        const std::string v = unescape(trim_copy(line.substr(eq + 1)));
        if (!k.empty()) {
            out[k] = v;
        }
    }

    f.close();
    return true;
}

static void ensure_initialized()
{
    if (g_initialized) {
        return;
    }

    if (g_translations.empty()) {
        return;
    }

    // i18n_keyval initializes the translator by immediately applying the current
    // registry locale (default is ""). Ensure it matches an existing locale.
    const std::string firstLocale = g_translations.begin()->first;
    i18n::set_locale(firstLocale);
    i18n::initialize_translator<i18n::translators::basic>(g_translations);
    g_initialized = true;
}

} // namespace

namespace I18nCli
{

std::string makeQtKey(const std::string &context,
                      const std::string &sourceText,
                      const std::string &comment)
{
    return std::string("qt|") + escape(context) + "|" + escape(sourceText) + "|" + escape(comment);
}

QtKeyParts parseQtKey(const std::string &key)
{
    QtKeyParts parts;

    const std::string prefix = "qt|";
    if (key.rfind(prefix, 0) != 0) {
        return parts;
    }

    const std::string rest = key.substr(prefix.size());

    std::vector<std::string> fields;
    fields.reserve(3);

    std::string cur;
    bool esc = false;
    for (const char ch : rest) {
        if (esc) {
            cur.push_back(ch);
            esc = false;
            continue;
        }
        if (ch == '\\') {
            cur.push_back(ch);
            esc = true;
            continue;
        }
        if (ch == '|' && fields.size() < 2) {
            fields.push_back(unescape(cur));
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    fields.push_back(unescape(cur));

    if (fields.size() != 3) {
        return parts;
    }

    parts.context = fields[0];
    parts.sourceText = fields[1];
    parts.comment = fields[2];
    return parts;
}

bool loadCliParserLocaleKv(const std::string &locale, const std::string &kvFilePath)
{
    i18n::translation_table table;
    if (!parse_kv_file(kvFilePath, table)) {
        return false;
    }

    g_translations[locale] = std::move(table);

    // Re-initialize to pick up new translations deterministically.
    // Set locale first, otherwise i18n_keyval will try to set locale "" and throw.
    i18n::set_locale(locale);
    i18n::initialize_translator<i18n::translators::basic>(g_translations);
    g_initialized = true;

    return true;
}

bool setLocale(const std::string &locale)
{
    ensure_initialized();

    if (!g_initialized) {
        return false;
    }

    // i18n_keyval may throw on missing locale depending on build flags.
    // We keep a deterministic boolean return.
    try {
        i18n::set_locale(locale);
        return true;
    } catch (...) {
        return false;
    }
}

std::string tQt(const std::string &context,
                const std::string &sourceText,
                const std::string &comment)
{
    ensure_initialized();
    const std::string key = makeQtKey(context, sourceText, comment);
    const std::string tr = i18n::t(key);
    if (tr == key) {
        return sourceText;
    }
    return tr;
}

} // namespace I18nCli
