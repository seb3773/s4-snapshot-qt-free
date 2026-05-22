#pragma once

#include <string>

class SettingsEditorCpp
{
public:
    [[nodiscard]] static std::string getEditorLikeSettingsQt(const std::string &guiEditor);
};
