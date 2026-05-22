#pragma once

#include <string>

class StandardPathsCpp
{
public:
    [[nodiscard]] static std::string writableConfigLocation();

    [[nodiscard]] static std::string locateApplicationsFile(const std::string &fileName);
};
