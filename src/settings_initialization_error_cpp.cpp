#include "settings_initialization_error_cpp.h"

#include "app_translator_cpp.h"
#include "file_cpp.h"
#include "logger_cpp.h"
#include "messagehandler_cpp.h"
#include "process_runner.h"

void SettingsInitializationErrorCpp::handleInitializationErrorLikeSettingsQt(const std::string &applicationName,
                                                                            const std::string &error)
{
    LoggerCpp::log(LoggerCpp::Level::Debug, "+++ void Settings::handleInitializationError(const QString&) const +++");
    LoggerCpp::log(LoggerCpp::Level::Critical, error);

    if (FileCpp::exists(std::string("/usr/bin/logger"))) {
        (void)ProcessRunner::execute("logger",
                                    {"-t", applicationName, std::string("Settings initialization error: ") + error});
    }

    const std::string title = AppTranslatorCpp::tQt("QObject", "Initialization Error");
    const std::string prefix = AppTranslatorCpp::tQt("QObject", "Failed to initialize application settings:\n\n%1");

    std::string msg = prefix;
    const std::string needle = "%1";
    const std::size_t pos = msg.find(needle);
    if (pos != std::string::npos) {
        msg.replace(pos, needle.size(), error);
    }

    MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical, title, msg);
}
