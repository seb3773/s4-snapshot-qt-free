/**********************************************************************
 *  mainwindow.h
 **********************************************************************
 * Copyright (C) 2015-2025 MX Authors
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

#include <QMessageBox>
#include <QScopedPointer>
#include <QSettings>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QDialog>
#include <QFutureWatcher>

#include "settings.h"
#include "batchprocessing_cpp_runner.h"
#include <atomic>
#include <functional>

class QCommandLineParser;

namespace Ui
{
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(Settings *settings, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

public slots:
    void disableOutput();
    void displayOutput();
    void outputAvailable(const QString &out);
    void procDone();
    void procStart();
    void processMsg(const QString &msg);
    void processMsgBox(BoxType box_type, const QString &title, const QString &msg);
    void progress();

private slots:
    void btnAbout_clicked();
    void btnBack_clicked();
    void btnCancel_clicked();
    void btnEditExclude_clicked();
    void btnRemoveCustomExclude_clicked();
    void btnHelp_clicked();
    void btnNext_clicked();
    void btnSelectSnapshot_clicked();
    void btnSelectDataFiles_clicked();
    void lineEditDataFilesPath_editingFinished();
    void cbCompression_currentIndexChanged();
    void checkMd5_toggled(bool checked);
    void checkSha512_toggled(bool checked);
    void excludeDesktop_toggled(bool checked);
    void excludeDocuments_toggled(bool checked);
    void excludeDownloads_toggled(bool checked);
    void excludeFlatpaks_toggled(bool checked);
    void excludeMusic_toggled(bool checked);
    void excludeNetworks_toggled(bool checked);
    void excludePictures_toggled(bool checked);
    void excludeSteam_toggled(bool checked);
    void excludeVideos_toggled(bool checked);
    void excludeVirtualBox_toggled(bool checked);
    void radioPersonal_clicked(bool checked);
    void radioRespin_toggled(bool checked);
    void spinCPU_valueChanged(int arg1);
    void spinThrottle_valueChanged(int arg1);

private:
    QScopedPointer<Ui::MainWindow> ui;
    QTimer timer;
    Settings *settings;
    QSettings qt_settings;
    QFileSystemWatcher excludesWatcher;

    // Backend integration - C++ callbacks
    std::atomic<bool> m_abortRequested{false};
    std::atomic<bool> m_operationInProgress{false};
    
    // Async execution support
    QFutureWatcher<BatchprocessingCppRunner::Result> *m_futureWatcher{nullptr};
    
    // Callback handlers for C++ backend
    void handleBackendMessage(const std::string& msg);
    void handleBackendLog(const std::string& log);
    bool shouldAbortBackend();
    
    // Async execution handlers
    void startBackgroundProcessing(const SettingsCpp &cppSettings,
                                   const std::string &appName,
                                   const BatchprocessingCppRunner::Callbacks &cb,
                                   const BatchprocessingCppRunner::Dependencies &deps);
    void onBackgroundProcessingFinished();

    [[nodiscard]] bool confirmStart();
    [[noreturn]] void cleanUp();
    bool installPackage(const QString &package);
    void appendIsoExtension(QString &file_name) const;
    void applyExclusions();
    void checkNvidiaGraphicsCard();
    void checkSaveWork();
    void checkUpdatedDefaultExcludes();
    void closeApp();
    void editBootMenu();
    void handleSelectionPage(const QString &file_name);
    void handleSettingsPage(const QString &file_name);
    void listFreeSpace();
    void listUsedSpace();
    void loadSettings();
    void prepareForOutput(const QString &file_name);
    bool hasCustomExcludes() const;
    enum class ExcludesChoice { None, ShowDiff, KeepCustom, UseUpdatedDefault };
    [[nodiscard]] bool isSourceExcludesNewer(QString &diffOutput) const;
    [[nodiscard]] ExcludesChoice showUpdatedExcludesPrompt(const QString &configuredPath,
                                                          const QString &sourcePath) const;
    void showUpdatedExcludesDialog(const QString &diffOutput) const;
    void updateCustomExcludesButton();
    bool resetCustomExcludes();
    void setConnections();
    void setExclusions();
    void setOtherOptions();
    void setup();
    void showErrorMessageBox(const QString &file_path);
    void watchExcludesFile();
    
    // Control state management during execution
    void enableControls(bool enable);
    void applyStartupGeometry();
    void saveWindowGeometry();
};
