#pragma once

#include <cstdint>

#include <QString>

#include "work_cpp_plan.h"

class WorkCopyNewIsoQtPlanOracle
{
public:
    struct SettingsFields {
        QString workDir;
        QString kernel;
        QString projectName;
        QString distroVersion;
        QString fullDistroName;
        QString releaseDate;
        QString codename;
        QString bootOptions;
    };

    struct Env {
        bool isoTemplateMultiExists = false;
        bool sysvinitInitExists = false;
        bool systemdSystemdExists = false;

        bool initrdReleaseExists = false;
        bool initrdReleaseIsFile = false;
        bool initrdReleaseDestExists = false;

        bool initrd_releaseExists = false;
        bool initrd_releaseIsFile = false;
        bool initrd_releaseDestExists = false;

        bool initrdTempDirValid = true;
        QString initrdTempDirPath;
        QString loggedInUserName;

        QString applicationName;
    };

    [[nodiscard]] static WorkCppPlan planCopyNewIso(const SettingsFields &settings, const Env &env);
};
