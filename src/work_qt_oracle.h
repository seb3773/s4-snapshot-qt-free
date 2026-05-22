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

#include <QElapsedTimer>
#include <QObject>

#include "work.h"
#include "settings.h"
#include "tempdir.h"

class WorkQtOracle : public QObject
{
    Q_OBJECT
public:
    enum HashType { md5, sha512 };
    Q_ENUM(HashType)

    explicit WorkQtOracle(Settings *settings, QObject *parent = nullptr);

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
    [[nodiscard]] qint64 getElapsedTime() const { return e_timer.elapsed(); }
    [[nodiscard]] const Settings& getSettings() const { return *settings; }

#ifdef UNIT_TESTS
    struct UnitTestExit {
        int exitCode = 0;
    };

    void ut_emitMessage(const QString &msg) { emit message(msg); }
    void ut_emitMessageBox(BoxType box_type, const QString &title, const QString &msg) { emit messageBox(box_type, title, msg); }
#endif

    // Timer control
    void startTimer() { started = true; e_timer.start(); }
    void markDone() { done = true; }

    // Utility methods
    [[nodiscard]] static bool checkInstalled(const QString &package);
    [[nodiscard]] bool isEnvironmentReady() const;
    bool installPackage(const QString &package);

signals:
    void message(const QString &msg);
    void messageBox(BoxType box_type, const QString &title, const QString &msg);

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
    void makeChecksum(WorkQtOracle::HashType hash_type, const QString &folder, const QString &file_name);

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
    QElapsedTimer e_timer;
    bool started = false;
    bool done = false;
    TempDir initrd_dir;
    QString bindRootPath = "/.bind-root";
    QString bindRootOverlayBase;
    bool bindRootOverlayActive = false;
};
