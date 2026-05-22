#include "app_translator_cpp.h"

#include "file_cpp.h"
#include "qm_translator_cpp.h"

namespace {

static QmTranslatorCpp g_tr;
static bool g_loaded = false;

static std::optional<std::string> try_load_file(const std::string &path)
{
    if (!FileCpp::exists(path)) {
        return std::nullopt;
    }
    if (!g_tr.loadFile(path)) {
        return std::nullopt;
    }
    g_loaded = true;
    return path;
}

static bool load_like_qtranslator(const std::string &dir, const std::string &baseName, std::string locale)
{
    while (true) {
        const std::string path = dir + "/" + baseName + "_" + locale + ".qm";
        if (try_load_file(path).has_value()) {
            return true;
        }

        const std::size_t pos = locale.rfind('_');
        if (pos == std::string::npos) {
            break;
        }
        locale = locale.substr(0, pos);
    }

    const std::string fallbackPath = dir + "/" + baseName + ".qm";
    if (try_load_file(fallbackPath).has_value()) {
        return true;
    }

    return false;
}

}

bool AppTranslatorCpp::loadFromDir(const std::string &dir,
                                  const std::string &baseName,
                                  const std::string &localeName)
{
    g_loaded = false;
    return load_like_qtranslator(dir, baseName, localeName);
}

std::optional<std::string> AppTranslatorCpp::translate(const std::string &context,
                                                       const std::string &sourceText,
                                                       const std::string &comment,
                                                       int n)
{
    if (!g_loaded) {
        return std::nullopt;
    }
    return g_tr.translate(context, sourceText, comment, n);
}

std::string AppTranslatorCpp::tQt(const std::string &context,
                                 const std::string &sourceText,
                                 const std::string &comment,
                                 int n)
{
    const auto t = translate(context, sourceText, comment, n);
    if (!t.has_value()) {
        return sourceText;
    }
    return *t;
}
