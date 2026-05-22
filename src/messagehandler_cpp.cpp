#include "messagehandler_cpp.h"

#include "log.h"

namespace {
const char *prefixFor(MessageHandlerCpp::MessageType type)
{
    switch (type) {
    case MessageHandlerCpp::Information:
        return "INFO";
    case MessageHandlerCpp::Warning:
        return "WARNING";
    case MessageHandlerCpp::Critical:
        return "ERROR";
    }
    return "INFO";
}
} // namespace

std::string MessageHandlerCpp::formatCliMessage(MessageType type, const std::string &title, const std::string &message)
{
    const std::string p = prefixFor(type);
    if (!title.empty()) {
        return std::string("[") + p + "] " + title + ": " + message;
    }
    return std::string("[") + p + "] " + message;
}

void MessageHandlerCpp::showMessage(MessageType type, const std::string &title, const std::string &message)
{
    Log::writeMessage(LoggerCpp::Level::Debug, formatCliMessage(type, title, message));
}
