#include "settings_filename_cpp.h"

#include <string>

#include "datetime_cpp.h"
#include "file_cpp.h"

namespace {

static void cb_debug(const SettingsFilenameCpp::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }
}

} // namespace

std::string SettingsFilenameCpp::getFilenameLikeSettingsQt(const std::string &snapshotDir,
                                                          const std::string &snapshotBasename,
                                                          const std::string &stamp,
                                                          const Callbacks &cb)
{
    cb_debug(cb, std::string("+++ QString Settings::getFilename() const +++\n"));

    if (stamp == "datetime") {
        return snapshotBasename + "-" + DateTimeCpp::nowLocalYmdHm() + ".iso";
    }

    std::string name;
    int n = 1;
    do {
        name = snapshotBasename + std::to_string(n) + ".iso";
        ++n;
    } while (FileCpp::exists(snapshotDir + "/" + name));

    return name;
}
