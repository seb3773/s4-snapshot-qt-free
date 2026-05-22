#pragma once

#include <string>

class SettingsInitializationErrorCpp
{
public:
    static void handleInitializationErrorLikeSettingsQt(const std::string &applicationName,
                                                        const std::string &error);
};
