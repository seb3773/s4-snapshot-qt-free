#pragma once

#include <string>

class MessageHandlerCpp
{
public:
    enum MessageType {
        Information,
        Warning,
        Critical
    };

    static void showMessage(MessageType type, const std::string &title, const std::string &message);

    [[nodiscard]] static std::string formatCliMessage(MessageType type, const std::string &title,
                                                     const std::string &message);
};
