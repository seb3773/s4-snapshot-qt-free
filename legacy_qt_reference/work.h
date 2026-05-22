/**********************************************************************
 *  work.h
 **********************************************************************
 * Copyright (C) 2020-2025 MX Authors
 *
 * Authors: Adrian
 *          Debian <http://debian.org>
 *
 * This file is part of S4 Snapshot.
 *
 * S4 Snapshot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * S4 Snapshot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with S4 Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#pragma once

#include <QString>

#include <chrono>
#include <functional>

#include "settings.h"
#include "tempdir.h"

#include "box_type.h"

class Work
{
public:
    enum HashType { md5, sha512 };

    explicit Work(Settings *settings);

    // Main workflow methods
    [[nodiscard]] quint64 getRequiredSpace();
    [[noreturn]] void cleanUp();
    bool createIso(const QString &filename);
    void checkEnoughSpace();
    void setupEnv();
    void copyNewIso();
    void savePackageList(const QString &file_name);

    // Status accessors
    [[nodiscard]] bool isStarted() const { return started; }
    [[nodiscard]] bool isDone() const { return done; }
    [[nodiscard]] qint64 getElapsedTime() const;
    [[nodiscard]] const Settings& getSettings() const { return *settings; }

    void setMessageCallback(std::function<void(const QString &)> cb) { m_onMessage = std::move(cb); }
    void setMessageBoxCallback(std::function<void(BoxType, const QString &, const QString &)> cb)
    {
        m_onMessageBox = std::move(cb);
    }

#ifdef UNIT_TESTS
    void ut_emitMessage(const QString &msg) { notifyMessage(msg); }
    void ut_emitMessageBox(BoxType box_type, const QString &title, const QString &msg) { notifyMessageBox(box_type, title, msg); }
#endif

    // Timer control
    void startTimer() { started = true; m_timerStart = std::chrono::steady_clock::now(); }
    void markDone() { done = true; }

    // Utility methods
    [[nodiscard]] static bool checkInstalled(const QString &package);
    [[nodiscard]] bool isEnvironmentReady() const;
    bool installPackage(const QString &package);
    [[nodiscard]] static std::string hashTypeToString(HashType type);

private:
    // Space and environment management
    [[nodiscard]] bool checkAndMoveWorkDir(const QString &dir, quint64 req_size);
    void checkNoSpaceAndExit(quint64 needed_space, quint64 free_space, const QString &dir);

    // Bind-root overlay management
    [[nodiscard]] bool setupBindRootOverlay();
    void cleanupBindRootOverlay();

    // File operations
    bool replaceStringInFile(const QString &old_text, const QString &new_text, const QString &file_path);

    // ISO creation helpers
    void makeChecksum(Work::HashType hash_type, const QString &folder, const QString &file_name);

    // Initrd operations
    void closeInitrd(const QString &initrd_dir, const QString &file);
    void openInitrd(const QString &file, const QString &initrd_dir);
    void copyModules(const QString &to, const QString &kernel);

    // Configuration file generation
    void replaceMenuStrings();
    void writeLsbRelease();
    void writeSnapshotInfo();
    void writeUnsquashfsSize(const QString &text);
    void writeVersionFile();

    // Member variables
    Settings *settings;
    std::chrono::steady_clock::time_point m_timerStart {};
    std::function<void(const QString &)> m_onMessage;
    std::function<void(BoxType, const QString &, const QString &)> m_onMessageBox;
    bool started = false;
    bool done = false;
    TempDir initrd_dir;
    QString bindRootPath = "/.bind-root";
    QString bindRootOverlayBase;
    bool bindRootOverlayActive = false;

    void notifyMessage(const QString &msg);
    void notifyMessageBox(BoxType box_type, const QString &title, const QString &msg);
};
