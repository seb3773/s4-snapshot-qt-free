/**********************************************************************
 *  mainwindow.cpp
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

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtConcurrent>
#include <QPointer>
#include <iostream>
#include <QCoreApplication>
#include <QCalendarWidget>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QTextStream>
#include <QTime>

#include "about.h"
#include "settings.h"
#include "work.h"
#include "cmd_cpp.h"
#include "process_runner.h"
#include "settings_cpp_builder.h"
#include "work_cpp_executor.h"
#include "work_cpp_planner.h"
#include <chrono>
#include <utime.h>

using namespace std::chrono_literals;

MainWindow::MainWindow(Settings *settings, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      settings(settings)
{
    ui->setupUi(this);
    
    // Initialize future watcher for async backend execution
    m_futureWatcher = new QFutureWatcher<BatchprocessingCppRunner::Result>(this);
    connect(m_futureWatcher, &QFutureWatcher<BatchprocessingCppRunner::Result>::finished,
            this, &MainWindow::onBackgroundProcessingFinished);
    
    setConnections();
    setup();
    loadSettings();
    listFreeSpace();
    setExclusions();
    setOtherOptions();
    if (settings->monthly) {
        ui->btnNext->click();
        ui->btnNext->click();
    } else {
        listUsedSpace();
    }
    watchExcludesFile();
    applyStartupGeometry();
    show();

    const QByteArray savedGeometry = qt_settings.value("geometry").toByteArray();
    if (!savedGeometry.isEmpty()) {
        restoreGeometry(savedGeometry);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::loadSettings()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->labelTitleSummary->clear();
    ui->labelSummary->clear();
    ui->labelSnapshotDir->setText(settings->snapshotDir);
    if (settings->snapshotName.isEmpty()) {
        ui->lineEditName->setText(settings->getFilename());
    } else {
        ui->lineEditName->setText(settings->snapshotName);
    }
    ui->textCodename->setText(settings->codename);
    ui->textDistroVersion->setText(settings->distroVersion);
    ui->textProjectName->setText(settings->projectName);
    ui->textOptions->setText(settings->bootOptions);
    ui->pushReleaseDate->setText(settings->releaseDate);
    QDir bootDir("/boot");
    QStringList kernelFiles = bootDir.entryList({"vmlinuz-*"}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    std::transform(kernelFiles.begin(), kernelFiles.end(), kernelFiles.begin(),
                   [](const QString &file) { return file.mid(QStringLiteral("vmlinuz-").length()); });
    ui->comboLiveKernel->addItems(kernelFiles);
    ui->comboLiveKernel->setCurrentText(settings->kernel);

    QString dataFilesPath = settings->dataFilesPathArg;
    if (dataFilesPath.isEmpty()) {
        dataFilesPath = qt_settings.value("datafiles_path").toString();
    }
    QString templatesPath = settings->templatesPathArg;
    if (templatesPath.isEmpty()) {
        templatesPath = qt_settings.value("templates_path").toString();
    }
    if (dataFilesPath.isEmpty()) {
        SettingsArgsCpp args;
        const SettingsCpp built = SettingsCppBuilder::buildFromArgs(
            args,
            true,
            QCoreApplication::applicationName().toStdString(),
            QCoreApplication::organizationName().toStdString());
        dataFilesPath = QString::fromStdString(built.dataFilesPath);
        if (templatesPath.isEmpty()) {
            templatesPath = QString::fromStdString(built.templatesPath);
        }
    }
    ui->lineEditDataFilesPath->setText(dataFilesPath);
    if (templatesPath.isEmpty()) {
        SettingsArgsCpp args;
        const SettingsCpp built = SettingsCppBuilder::buildFromArgs(
            args,
            true,
            QCoreApplication::applicationName().toStdString(),
            QCoreApplication::organizationName().toStdString());
        templatesPath = QString::fromStdString(built.templatesPath);
    }
    ui->lineEditTemplatesPath->setText(templatesPath);

    updateCustomExcludesButton();
    watchExcludesFile();
}

bool MainWindow::hasCustomExcludes() const
{
    const QString configuredPath = settings->snapshotExcludesPath;
    const QString sourcePath = settings->getExcludesSourcePath();

    if (sourcePath.isEmpty() || configuredPath.isEmpty()) {
        return false;
    }

    if (!QFileInfo::exists(sourcePath)) {
        return false;
    }

    if (!QFileInfo::exists(configuredPath)) {
        return true;
    }

    const int diffResult = ProcessRunner::execute(
        "diff",
        {"--brief", configuredPath.toStdString(), sourcePath.toStdString()});
    if (diffResult == 1) {
        return true;
    }
    if (diffResult != 0) {
        qWarning() << "Unable to compare excludes files with diff:" << configuredPath << sourcePath;
    }
    return false;
}

bool MainWindow::isSourceExcludesNewer(QString &diffOutput) const
{
    const QString configuredPath = settings->snapshotExcludesPath;
    const QString sourcePath = settings->getExcludesSourcePath();

    if (sourcePath.isEmpty() || configuredPath.isEmpty()) {
        return false;
    }

    const QFileInfo configuredInfo(configuredPath);
    const QFileInfo sourceInfo(sourcePath);

    if (!configuredInfo.exists() || !sourceInfo.exists()) {
        return false;
    }

    if (sourceInfo.lastModified() <= configuredInfo.lastModified()) {
        return false;
    }

    const ProcessRunner::Result r = ProcessRunner::run(
        "diff",
        {"--unified", configuredPath.toStdString(), sourcePath.toStdString()},
        std::string(),
        30000);
    if (!r.started || r.exitStatus != ProcessRunner::ExitStatus::NormalExit) {
        qWarning() << "Unable to compare excludes files with diff:" << configuredPath << sourcePath;
        return false;
    }

    const int diffResult = r.exitCode;
    if (diffResult == 0) {
        return false;
    }
    if (diffResult != 1) {
        qWarning() << "Unable to compare excludes files with diff:" << configuredPath << sourcePath;
        return false;
    }

    diffOutput = QString::fromUtf8(r.stdoutText.c_str());
    if (diffOutput.isEmpty()) {
        diffOutput = QString::fromUtf8(r.stderrText.c_str());
    }
    if (diffOutput.isEmpty()) {
        diffOutput = tr("No diff output available.");
    }
    return true;
}

void MainWindow::updateCustomExcludesButton()
{
    ui->btnRemoveCustomExclude->setVisible(hasCustomExcludes());
}

void MainWindow::watchExcludesFile()
{
    const QString path = settings->snapshotExcludesPath;
    if (path.isEmpty()) {
        return;
    }

    const QStringList watched = excludesWatcher.files();
    for (const QString &existing : watched) {
        if (existing != path) {
            excludesWatcher.removePath(existing);
        }
    }

    if (!excludesWatcher.files().contains(path) && QFileInfo::exists(path)) {
        excludesWatcher.addPath(path);
    }
}

MainWindow::ExcludesChoice MainWindow::showUpdatedExcludesPrompt(const QString &configuredPath,
                                                                 const QString &sourcePath) const
{
    QMessageBox msgBox(const_cast<MainWindow *>(this));
    msgBox.setWindowTitle(tr("Updated Exclusion List"));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(
        tr("The exclusion file at %1 is newer than your configured file at %2.").arg(sourcePath, configuredPath));
    msgBox.setInformativeText(
        tr("Review the changes below. Keep your custom file or replace it with the updated default."));

    ExcludesChoice choice = ExcludesChoice::None;

    msgBox.addButton(QMessageBox::Close);

    QPushButton *showDiffButton = msgBox.addButton(tr("Show Differences"), QMessageBox::ActionRole);
    QObject::connect(showDiffButton, &QPushButton::clicked, &msgBox, [&choice, &msgBox] {
        choice = ExcludesChoice::ShowDiff;
        msgBox.accept();
    });

    QPushButton *keepCustomButton = msgBox.addButton(tr("Keep Custom"), QMessageBox::ActionRole);
    QObject::connect(keepCustomButton, &QPushButton::clicked, &msgBox, [&choice, &msgBox] {
        choice = ExcludesChoice::KeepCustom;
        msgBox.accept();
    });

    QPushButton *useUpdatedDefaultButton = msgBox.addButton(tr("Use Updated Default"), QMessageBox::ActionRole);
    QObject::connect(useUpdatedDefaultButton, &QPushButton::clicked, &msgBox, [&choice, &msgBox] {
        choice = ExcludesChoice::UseUpdatedDefault;
        msgBox.accept();
    });

    msgBox.exec();
    return choice;
}

void MainWindow::showUpdatedExcludesDialog(const QString &diffOutput) const
{
    QDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(tr("Updated Exclusion List"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *textEdit = new QPlainTextEdit(&dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(diffOutput);
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    textEdit->setFont(font);
    layout->addWidget(textEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, &dialog);
    layout->addWidget(buttons);

    // Highlight diff lines similar to standard diff coloring
    QTextCharFormat addedFormat;
    addedFormat.setForeground(Qt::darkGreen);
    addedFormat.setFontWeight(QFont::Bold);

    QTextCharFormat locationFormat;
    locationFormat.setForeground(QColor(35, 140, 216));
    locationFormat.setFontWeight(QFont::Bold);

    QTextCharFormat removedFormat;
    removedFormat.setForeground(QColor(187, 15, 30));
    removedFormat.setFontWeight(QFont::Bold);

    QTextCharFormat headerFormat;
    headerFormat.setForeground(QColor(102, 102, 102));
    headerFormat.setFontWeight(QFont::Bold);

    QTextCursor cursor(textEdit->document());
    cursor.setPosition(0);
    while (!cursor.atEnd()) {
        const QString lineText = cursor.block().text();
        if (lineText.startsWith("@@")) {
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(locationFormat);
        } else if (lineText.startsWith("+++ ") || lineText.startsWith("--- ")) {
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(headerFormat);
        } else if (lineText.startsWith('+')) {
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(addedFormat);
        } else if (lineText.startsWith('-')) {
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(removedFormat);
        }
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
    }
    cursor.setPosition(0);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(900, 700);
    dialog.exec();
}

bool MainWindow::resetCustomExcludes()
{
    const QString configuredPath = settings->snapshotExcludesPath;
    const QString sourcePath = settings->getExcludesSourcePath();

    if (sourcePath.isEmpty() || configuredPath.isEmpty()) {
        return false;
    }

    if (!QFileInfo::exists(sourcePath)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Default exclusion file not found at %1.").arg(sourcePath));
        return false;
    }

    const QString targetDir = QFileInfo(configuredPath).absolutePath();
    if (!targetDir.isEmpty()) {
        QDir().mkpath(targetDir);
    }

    if (QFileInfo::exists(configuredPath)) {
        const QString backupPath = configuredPath + "." + QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
        if (!QFile::copy(configuredPath, backupPath)) {
            QMessageBox::warning(this, tr("Warning"),
                                 tr("Could not backup existing exclusion file to %1.").arg(backupPath));
        }
        if (!QFile::remove(configuredPath)) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Could not remove existing exclusion file at %1.").arg(configuredPath));
            return false;
        }
    }

    if (!QFile::copy(sourcePath, configuredPath)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not copy default exclusion file from %1 to %2.")
                                 .arg(sourcePath, configuredPath));
        return false;
    }

    watchExcludesFile();
    return true;
}

void MainWindow::setOtherOptions()
{
    ui->cbCompression->setCurrentIndex(ui->cbCompression->findText(settings->compression, Qt::MatchStartsWith));
    ui->checkMd5->setChecked(settings->makeMd5sum);
    ui->checkSha512->setChecked(settings->makeSha512sum);
    ui->radioRespin->setChecked(settings->resetAccounts);
    ui->spinCPU->setMaximum(static_cast<int>(settings->maxCores));
    ui->spinCPU->setValue(static_cast<int>(settings->cores));
    ui->spinThrottle->setValue(static_cast<int>(settings->throttle));
}

void MainWindow::setConnections()
{
    connect(&timer, &QTimer::timeout, this, &MainWindow::progress);
    // Note: Callbacks are now passed directly to BatchprocessingCppRunner::runFromSettings()
    // in prepareForOutput() method. The backend calls handleBackendMessage() and handleBackendLog()
    // which in turn call processMsg() and processMsgBox() as needed.
    connect(QApplication::instance(), &QApplication::aboutToQuit, this, [this] { saveWindowGeometry(); });
    connect(ui->btnAbout, &QPushButton::clicked, this, &MainWindow::btnAbout_clicked);
    connect(ui->btnBack, &QPushButton::clicked, this, &MainWindow::btnBack_clicked);
    connect(ui->btnCancel, &QPushButton::clicked, this, &MainWindow::btnCancel_clicked);
    connect(ui->btnEditExclude, &QPushButton::clicked, this, &MainWindow::btnEditExclude_clicked);
    connect(ui->btnRemoveCustomExclude, &QPushButton::clicked, this, &MainWindow::btnRemoveCustomExclude_clicked);
    connect(ui->btnHelp, &QPushButton::clicked, this, &MainWindow::btnHelp_clicked);
    connect(ui->btnNext, &QPushButton::clicked, this, &MainWindow::btnNext_clicked);
    connect(ui->btnSelectSnapshot, &QPushButton::clicked, this, &MainWindow::btnSelectSnapshot_clicked);
    connect(ui->btnSelectDataFiles, &QPushButton::clicked, this, &MainWindow::btnSelectDataFiles_clicked);
    connect(ui->lineEditDataFilesPath, &QLineEdit::editingFinished, this,
            &MainWindow::lineEditDataFilesPath_editingFinished);
    connect(ui->btnSelectTemplates, &QPushButton::clicked, this, &MainWindow::btnSelectTemplates_clicked);
    connect(ui->lineEditTemplatesPath, &QLineEdit::editingFinished, this,
            &MainWindow::lineEditTemplatesPath_editingFinished);
    connect(ui->cbCompression, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::cbCompression_currentIndexChanged);
    connect(ui->checkMd5, &QCheckBox::toggled, this, &MainWindow::checkMd5_toggled);
    connect(ui->checkSha512, &QCheckBox::toggled, this, &MainWindow::checkSha512_toggled);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeDesktop, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeDocuments, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeDownloads, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeFlatpaks, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeMusic, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeNetworks, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludePictures, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeSteam, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeVideos, &QCheckBox::setChecked);
    connect(ui->excludeAll, &QCheckBox::clicked, ui->excludeVirtualBox, &QCheckBox::setChecked);
    connect(ui->excludeDesktop, &QCheckBox::toggled, this, &MainWindow::excludeDesktop_toggled);
    connect(ui->excludeDocuments, &QCheckBox::toggled, this, &MainWindow::excludeDocuments_toggled);
    connect(ui->excludeDownloads, &QCheckBox::toggled, this, &MainWindow::excludeDownloads_toggled);
    connect(ui->excludeFlatpaks, &QCheckBox::toggled, this, &MainWindow::excludeFlatpaks_toggled);
    connect(ui->excludeMusic, &QCheckBox::toggled, this, &MainWindow::excludeMusic_toggled);
    connect(ui->excludeNetworks, &QCheckBox::toggled, this, &MainWindow::excludeNetworks_toggled);
    connect(ui->excludePictures, &QCheckBox::toggled, this, &MainWindow::excludePictures_toggled);
    connect(ui->excludeSteam, &QCheckBox::toggled, this, &MainWindow::excludeSteam_toggled);
    connect(ui->excludeVideos, &QCheckBox::toggled, this, &MainWindow::excludeVideos_toggled);
    connect(ui->excludeVirtualBox, &QCheckBox::toggled, this, &MainWindow::excludeVirtualBox_toggled);
    connect(&excludesWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        Q_UNUSED(path);
        updateCustomExcludesButton();
        checkUpdatedDefaultExcludes();
        watchExcludesFile(); // re-add in case the file was recreated
    });
    connect(ui->pushReleaseDate, &QPushButton::clicked, this, [this] {
        QCalendarWidget *calendarWidget = new QCalendarWidget(this);
        calendarWidget->setWindowTitle(tr("Select Release Date"));
        calendarWidget->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint);
        connect(calendarWidget, &QCalendarWidget::activated, this, [this, calendarWidget](const QDate date) {
            ui->pushReleaseDate->setText(date.toString("MMMM dd, yyyy"));
            calendarWidget->deleteLater();
        });
        calendarWidget->show();
    });
    connect(ui->radioPersonal, &QRadioButton::clicked, this, &MainWindow::radioPersonal_clicked);
    connect(ui->radioRespin, &QRadioButton::toggled, this, &MainWindow::radioRespin_toggled);
    connect(ui->spinCPU, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::spinCPU_valueChanged);
    connect(ui->spinThrottle, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &MainWindow::spinThrottle_valueChanged);
}

void MainWindow::setExclusions()
{
    QVector<QPair<QCheckBox *, Settings::Exclude>> exclusionPairs
        = {{ui->excludeDesktop, Settings::Exclude::Desktop},     {ui->excludeDocuments, Settings::Exclude::Documents},
           {ui->excludeDownloads, Settings::Exclude::Downloads}, {ui->excludeFlatpaks, Settings::Exclude::Flatpaks},
           {ui->excludeMusic, Settings::Exclude::Music},         {ui->excludeNetworks, Settings::Exclude::Networks},
           {ui->excludePictures, Settings::Exclude::Pictures},   {ui->excludeSteam, Settings::Exclude::Steam},
           {ui->excludeVideos, Settings::Exclude::Videos},       {ui->excludeVirtualBox, Settings::Exclude::VirtualBox}};

    for (const auto &pair : exclusionPairs) {
        pair.first->setChecked(settings->exclusions.testFlag(pair.second));
    }
}

void MainWindow::setup()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    ui->outputBox->setFont(font);
    ui->outputBox->setReadOnly(true);

    setWindowTitle(tr("S4 Snapshot"));
    ui->btnBack->setHidden(true);
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnCancel->setEnabled(true);
    ui->btnNext->setEnabled(true);
    ui->cbCompression->blockSignals(true);
    ui->cbCompression->addItems({"lz4 - " + tr("fastest, worst compression"), "lzo - " + tr("fast, worse compression"),
                                 "gzip - " + tr("slow, better compression"), "zstd - " + tr("best compromise"),
                                 "xz - " + tr("slowest, best compression")});
    ui->cbCompression->blockSignals(false);
    if (Settings::getDebianVerNum() < Release::Bookworm) {
        ui->labelThrottle->hide();
        ui->spinThrottle->hide();
    }
}

void MainWindow::applyStartupGeometry()
{
    static constexpr int kDefaultWidth = 984;
    static constexpr int kDefaultHeight = 650;

    setMinimumHeight(kDefaultHeight);

    if (qt_settings.value("geometry").toByteArray().isEmpty()) {
        resize(kDefaultWidth, kDefaultHeight);
    }
}

void MainWindow::saveWindowGeometry()
{
    qt_settings.setValue("geometry", saveGeometry());
    qt_settings.sync();
}

void MainWindow::listUsedSpace()
{
    ui->btnNext->setDisabled(true);
    ui->btnCancel->setDisabled(true);
    ui->btnSelectSnapshot->setDisabled(true);
    ui->btnNext->setEnabled(true);
    ui->btnCancel->setEnabled(true);
    ui->btnSelectSnapshot->setEnabled(true);
    ui->labelUsedSpace->setText(settings->getUsedSpace());
}

void MainWindow::listFreeSpace()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QString path = settings->snapshotDir;
    path.remove(QRegularExpression("/snapshot$"));
    const QString freeSpace = settings->getFreeSpaceStrings(path);
    ui->labelFreeSpace->clear();
    ui->labelFreeSpace->setText("- " + tr("Free space on %1, where snapshot folder is placed: ").arg(path) + freeSpace
                                + "\n");
    ui->labelDiskSpaceHelp->setText(
        tr("The free space should be sufficient to hold the compressed data from / and /home\n\n"
           "      If necessary, you can create more available space by removing previous snapshots and saved copies: "
           "%1 snapshots are taking up %2 of disk space.")
            //        tr("The free space should be sufficient to hold the compressed data from / and /home\n\n"
            //           "      If necessary, you can create more available space\n"
            //           "      by removing previous snapshots and saved copies:\n"
            //           "      %1 snapshots are taking up %2 of disk space.\n")
            .arg(QString::number(settings->getSnapshotCount()), settings->getSnapshotSize()));
}

bool MainWindow::installPackage(const QString &package)
{
    setWindowTitle(tr("Installing ") + package);
    ui->outputLabel->setText(tr("Installing ") + package);
    ui->outputBox->clear();
    ui->btnNext->setDisabled(true);
    ui->btnBack->setDisabled(true);
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    procStart();
    
    // Call Work::installPackage directly - this is a standalone utility method
    // that doesn't require the full backend workflow
    Work tempWork(settings);
    if (!tempWork.installPackage(package)) {
        procDone();
        return false;
    }
    procDone();
    return true;
}

void MainWindow::cleanUp()
{
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    // Call Work::cleanUp directly - this is a cleanup utility that exits the application
    Work tempWork(settings);
    tempWork.cleanUp();
}

void MainWindow::procStart()
{
    timer.start(500ms);
    setCursor(QCursor(Qt::BusyCursor));
}

void MainWindow::processMsgBox(BoxType box_type, const QString &title, const QString &msg)
{
    qDebug().noquote() << title << msg;

    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setText(msg);

    static const QMap<BoxType, QMessageBox::Icon> iconMap = {{BoxType::warning, QMessageBox::Warning},
                                                             {BoxType::critical, QMessageBox::Critical},
                                                             {BoxType::question, QMessageBox::Question},
                                                             {BoxType::information, QMessageBox::Information}};

    msgBox.setIcon(iconMap.value(box_type, QMessageBox::NoIcon));
    msgBox.exec();
}
void MainWindow::processMsg(const QString &msg)
{
    qDebug().noquote() << msg;
    
    // Update the label with current message
    ui->outputLabel->setText(msg);
    
    // Also append to outputBox for full history (like original version)
    // This provides a scrollable log of all operations
    ui->outputBox->moveCursor(QTextCursor::End);
    ui->outputBox->insertPlainText(msg + "\n");
    ui->outputBox->verticalScrollBar()->setValue(ui->outputBox->verticalScrollBar()->maximum());
}

void MainWindow::procDone()
{
    timer.stop();
    ui->progressBar->setValue(ui->progressBar->maximum());
    setCursor(QCursor(Qt::ArrowCursor));
}

void MainWindow::displayOutput()
{
    // Cmd/QProcess-based output streaming removed: GUI now consumes backend CmdCpp directly.
}

void MainWindow::disableOutput()
{
    // Cmd/QProcess-based output streaming removed.
}

void MainWindow::outputAvailable(const QString &out)
{
    ui->outputBox->moveCursor(QTextCursor::End);
    if (out.startsWith("\r")) {
        ui->outputBox->moveCursor(QTextCursor::Up, QTextCursor::KeepAnchor);
        ui->outputBox->moveCursor(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }
    ui->outputBox->insertPlainText(out);
    ui->outputBox->verticalScrollBar()->setValue(ui->outputBox->verticalScrollBar()->maximum());
}

void MainWindow::progress()
{
    ui->progressBar->setValue((ui->progressBar->value() + 1) % ui->progressBar->maximum() + 1);

    // In live environment and first page, blink text while calculating used disk space
    if (settings->live && (ui->stackedWidget->currentIndex() == 0)) {
        if (ui->progressBar->value() % 4 == 0) {
            ui->labelUsedSpace->setText("\n " + tr("Please wait."));
        } else {
            ui->labelUsedSpace->setText("\n " + tr("Please wait. Calculating used disk space..."));
        }
    }
}

void MainWindow::btnNext_clicked()
{
    QString file_name = ui->lineEditName->text();
    appendIsoExtension(file_name);

    if (QFile::exists(settings->snapshotDir + "/" + file_name)) {
        showErrorMessageBox(settings->snapshotDir + "/" + file_name);
        return;
    }

    if (ui->stackedWidget->currentWidget() == ui->selectionPage) {
        handleSelectionPage(file_name);
    } else if (ui->stackedWidget->currentWidget() == ui->settingsPage) {
        handleSettingsPage(file_name);
    } else {
        QApplication::quit();
    }
}

void MainWindow::appendIsoExtension(QString &file_name) const
{
    if (!file_name.endsWith(".iso")) {
        file_name += ".iso";
    }
}

void MainWindow::showErrorMessageBox(const QString &file_path)
{
    QMessageBox::critical(
        this, tr("Error"),
        tr("Output file %1 already exists. Please use another file name, or delete the existent file.").arg(file_path));
}

bool MainWindow::isTemplatesPathValid(const QString &path) const
{
    const QString trimmed = path.trimmed();
    return QFileInfo::exists(trimmed + "/iso-template.tar.gz")
        && QFileInfo::exists(trimmed + "/template-initrd.gz");
}

void MainWindow::handleSelectionPage(const QString &file_name)
{
    if (!settings->validateSpaceRequirements()) {
        processMsgBox(BoxType::critical, tr("Error"),
                      tr("Insufficient free space. Please select a different snapshot directory or free up space."));
        return;
    }

    if (!isTemplatesPathValid(ui->lineEditTemplatesPath->text())) {
        processMsgBox(BoxType::critical, tr("Error"),
                      tr("ISO templates directory is invalid. It must contain iso-template.tar.gz and template-initrd.gz."));
        return;
    }

    setWindowTitle(tr("Settings"));
    ui->stackedWidget->setCurrentWidget(ui->settingsPage);
    ui->btnBack->setHidden(false);
    ui->btnBack->setEnabled(true);
    settings->kernel = ui->comboLiveKernel->currentText();
    settings->selectKernel();
    ui->labelTitleSummary->setText(tr("Snapshot will use the following settings:"));

    ui->labelSummary->setText("\n" + tr("- Snapshot directory:") + " " + settings->snapshotDir + "\n" + "- "
                              + tr("Snapshot name:") + " " + file_name + "\n" + tr("- Kernel to be used:") + " "
                              + settings->kernel + "\n" + tr("- Live files data:") + " "
                              + ui->lineEditDataFilesPath->text().trimmed() + "\n" + tr("- ISO templates:") + " "
                              + ui->lineEditTemplatesPath->text().trimmed() + "\n");
    settings->codename = ui->textCodename->text();
    settings->distroVersion = ui->textDistroVersion->text();
    settings->projectName = ui->textProjectName->text();
    settings->fullDistroName = settings->projectName + "-" + settings->distroVersion + "_" + QString(settings->x86 ? "386" : "x64");
    settings->bootOptions = ui->textOptions->text();
    settings->releaseDate = ui->pushReleaseDate->text();
    checkNvidiaGraphicsCard();
    settings->bootOptions = ui->textOptions->text();
    checkUpdatedDefaultExcludes();
}

void MainWindow::checkNvidiaGraphicsCard()
{
    static bool hasRun = false;
    QString currentOptions = ui->textOptions->text();

    if (hasRun || currentOptions.contains("xorg=nvidia")) {
        return;
    }

    procStart();
    const bool hasNvidia = CmdCpp().run("glxinfo | grep -q NVIDIA");
    procDone();
    if (hasNvidia) {
        if (QMessageBox::Yes
            == QMessageBox::question(this, tr("NVIDIA Detected"),
                                     tr("This computer uses an NVIDIA graphics card. Are you planning to use the "
                                        "resulting ISO on the same computer or another computer with an NVIDIA card?"),
                                     QMessageBox::Yes | QMessageBox::No)) {
            ui->textOptions->setText(currentOptions.isEmpty() ? "xorg=nvidia" : currentOptions + " xorg=nvidia");
            QMessageBox::information(this, tr("NVIDIA Selected"),
                                     tr("Note: If you use the resulting ISO on a computer without an NVIDIA card, "
                                        "you will likely need to remove 'xorg=nvidia' from the boot options."));
        } else {
            QMessageBox::information(this, tr("NVIDIA Detected"),
                                     tr("Note: If you use the resulting ISO on a computer with an NVIDIA card, "
                                        "you may need to add 'xorg=nvidia' to the boot options."));
        }
    }
    hasRun = true;
}

void MainWindow::checkUpdatedDefaultExcludes()
{
    QString diffOutput;
    if (!isSourceExcludesNewer(diffOutput)) {
        return;
    }

    const QString configuredPath = settings->snapshotExcludesPath;
    const QString sourcePath = settings->getExcludesSourcePath();
    ExcludesChoice choice = ExcludesChoice::None;

    while (true) {
        choice = showUpdatedExcludesPrompt(configuredPath, sourcePath);
        if (choice == ExcludesChoice::ShowDiff) {
            showUpdatedExcludesDialog(diffOutput);
            continue; // re-prompt after showing diff
        }
        if (choice == ExcludesChoice::None) {
            return; // user closed the prompt; do nothing
        }
        break;
    }

    if (choice == ExcludesChoice::UseUpdatedDefault) {
        if (resetCustomExcludes()) {
            updateCustomExcludesButton();
        } else {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Could not replace the exclusion file with the updated default."));
        }
    } else if (choice == ExcludesChoice::KeepCustom) {
        utimbuf times {};
        times.actime = QFileInfo(configuredPath).lastRead().toSecsSinceEpoch();
        times.modtime = QDateTime::currentSecsSinceEpoch();
        const int utimeResult = utime(configuredPath.toLocal8Bit().constData(), &times);
        if (utimeResult == 0) {
            qDebug() << "Updated modification time for custom excludes file via utime" << configuredPath;
        } else {
            qWarning() << "Failed to update modification time for custom excludes file" << configuredPath;
        }
    }
}

void MainWindow::handleSettingsPage(const QString &file_name)
{
    if (!settings->checkCompression()) {
        processMsgBox(BoxType::critical, tr("Error"),
                      tr("Current kernel doesn't support selected compression algorithm, please edit the "
                         "configuration file and select a different algorithm."));
        return;
    }

    if (!confirmStart()) {
        return;
    }

    // Timer is now tracked by m_operationInProgress flag and backend handles timing internally
    m_operationInProgress.store(true);
    if (!settings->checkSnapshotDir() || !settings->checkTempDir()) {
        cleanUp();
        return;
    }

    applyExclusions();
    prepareForOutput(file_name);
}

bool MainWindow::confirmStart()
{
    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Question);
    messageBox.setWindowTitle(tr("Final chance"));
    messageBox.setText(
        tr("Snapshot now has all the information it needs to create an ISO from your running system.") + "\n\n"
        + tr("It will take some time to finish, depending on the size of the installed system and the capacity of "
             "your computer.")
        + "\n\n" + tr("OK to start?"));
    messageBox.addButton(QMessageBox::Ok);
    auto *pushCancel = messageBox.addButton(QMessageBox::Cancel);
    auto *checkShutdown = new QCheckBox(this);
    checkShutdown->setText(tr("Shutdown computer when done."));
    if (settings->shutdown) {
        checkShutdown->setCheckState(Qt::Checked);
    }
    messageBox.setCheckBox(checkShutdown);
    messageBox.exec();
    if (messageBox.clickedButton() == pushCancel) {
        return false;
    }
    settings->shutdown = checkShutdown->isChecked();
    return true;
}

void MainWindow::applyExclusions()
{
    qDebug() << "=== DEBUG SESSION EXCLUDES - applyExclusions() START ===";
    qDebug() << "BEFORE applyExclusions - sessionExcludes:" << settings->sessionExcludes;
    qDebug() << "BEFORE applyExclusions - length:" << settings->sessionExcludes.length();
    
    settings->excludeDesktop(ui->excludeDesktop->isChecked());
    settings->excludeDocuments(ui->excludeDocuments->isChecked());
    settings->excludeDownloads(ui->excludeDownloads->isChecked());
    settings->excludeFlatpaks(ui->excludeFlatpaks->isChecked());
    settings->excludeMusic(ui->excludeMusic->isChecked());
    settings->excludeNetworks(ui->excludeNetworks->isChecked());
    settings->excludePictures(ui->excludePictures->isChecked());
    settings->excludeSteam(ui->excludeSteam->isChecked());
    settings->excludeVideos(ui->excludeVideos->isChecked());
    settings->excludeVirtualBox(ui->excludeVirtualBox->isChecked());
    settings->otherExclusions();
    
    qDebug() << "AFTER applyExclusions - sessionExcludes:" << settings->sessionExcludes;
    qDebug() << "AFTER applyExclusions - length:" << settings->sessionExcludes.length();
    qDebug() << "=== DEBUG SESSION EXCLUDES - applyExclusions() END ===";
}

void MainWindow::prepareForOutput(const QString &/*file_name*/)
{
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    setWindowTitle(tr("Output"));
    ui->outputBox->clear();
    
    // ========================================================================
    // Phase 3.2: Async Backend Execution with QtConcurrent
    // ========================================================================
    
    // Set operation in progress
    m_operationInProgress.store(true);
    m_abortRequested.store(false);
    
    std::cerr << "=== prepareForOutput() - Starting ASYNC backend execution ===" << std::endl;
    
    // DISABLE all controls except Cancel button (execution starting)
    enableControls(false);
    
    // Show progress bar immediately
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    ui->btnCancel->setEnabled(true);
    ui->btnCancel->setText(tr("Cancel"));
    
    // Start timer for progress updates
    timer.start(100);
    
    // Create thread-safe callbacks for backend
    // Use QPointer to safely capture 'this' - protects against object destruction
    QPointer<MainWindow> self(this);
    
    // Use QMetaObject::invokeMethod to ensure GUI updates happen in GUI thread
    auto messageCallback = [self](const std::string& msg) {
        if (!self) return;  // Object destroyed, abort safely
        QMetaObject::invokeMethod(self.data(), [self, msg]() {
            if (!self) return;  // Double-check before accessing
            self->handleBackendMessage(msg);
        }, Qt::QueuedConnection);
    };
    
    auto logCallback = [self](const std::string& log) {
        if (!self) return;  // Object destroyed, abort safely
        QMetaObject::invokeMethod(self.data(), [self, log]() {
            if (!self) return;  // Double-check before accessing
            self->handleBackendLog(log);
        }, Qt::QueuedConnection);
    };
    
    // Convert Settings to SettingsCpp using buildFromArgs
    SettingsArgsCpp args;
    args.monthly = settings->monthly;
    args.overrideSize = settings->overrideSize;
    args.preempt = settings->preempt;
    args.fileArg = settings->snapshotName.toStdString();
    args.maxCoresOverride = static_cast<std::uint32_t>(settings->cores);
    const QString dataFilesPath = ui->lineEditDataFilesPath->text().trimmed();
    if (!dataFilesPath.isEmpty()) {
        args.dataFilesPathArg = dataFilesPath.toStdString();
    }
    const QString templatesPath = ui->lineEditTemplatesPath->text().trimmed();
    if (!templatesPath.isEmpty()) {
        args.templatesPathArg = templatesPath.toStdString();
    }

    // CRITICAL: Save sessionExcludes BEFORE buildFromArgs (which clears it)
    // applyExclusions() on line 776 built settings->sessionExcludes from checkboxes
    // buildFromArgs() clears sessionExcludes on line 198 of settings_cpp_builder.cpp
    // So we must save it first and restore it after
    
    qDebug() << "=== DEBUG SESSION EXCLUDES - SAVE/RESTORE START ===";
    qDebug() << "BEFORE save - settings->sessionExcludes:" << settings->sessionExcludes;
    qDebug() << "BEFORE save - length:" << settings->sessionExcludes.length();
    
    const std::string savedSessionExcludes = settings->sessionExcludes.toStdString();
    
    qDebug() << "SAVED sessionExcludes - length:" << savedSessionExcludes.length();
    qDebug() << "SAVED sessionExcludes - content:" << QString::fromStdString(savedSessionExcludes);
    qDebug() << "DEBUG: sessionExcludes BEFORE buildFromArgs:" << settings->sessionExcludes;
    qDebug() << "DEBUG: sessionExcludes length:" << settings->sessionExcludes.length();
    
    SettingsCpp cppSettings = SettingsCppBuilder::buildFromArgs(
        args,
        true, // isGuiApp
        QCoreApplication::applicationName().toStdString(),
        QCoreApplication::organizationName().toStdString()
    );
    
    qDebug() << "BEFORE restore - cppSettings.sessionExcludes:" << QString::fromStdString(cppSettings.sessionExcludes);
    qDebug() << "BEFORE restore - length:" << cppSettings.sessionExcludes.length();
    
    // CRITICAL FIX: Restore sessionExcludes from GUI to backend
    // The GUI allows users to add/remove exclusions at runtime via checkboxes
    // These are stored in settings->sessionExcludes and MUST be passed to the backend
    // Otherwise, the space calculation will ignore user-selected exclusions
    cppSettings.sessionExcludes = savedSessionExcludes;
    
    qDebug() << "AFTER restore - cppSettings.sessionExcludes:" << QString::fromStdString(cppSettings.sessionExcludes);
    qDebug() << "AFTER restore - length:" << cppSettings.sessionExcludes.length();
    qDebug() << "=== DEBUG SESSION EXCLUDES - SAVE/RESTORE END ===";
    
    // Setup BatchprocessingCppRunner callbacks
    BatchprocessingCppRunner::Callbacks cb;
    cb.debug = messageCallback;
    cb.critical = logCallback;
    
    // Setup dependencies
    // CRITICAL FIX: Capture cb by VALUE, not by reference (&cb)
    // The local variable cb goes out of scope, causing segfault
    BatchprocessingCppRunner::Dependencies deps;
    deps.runWork = [cb](const WorkCppPlan &plan, const WorkCppExecutor::Callbacks &/*wcb*/) -> WorkCppExecutor::Result {
        WorkCppExecutor::Callbacks executorCb;
        executorCb.message = cb.debug;
        executorCb.messageBox = [cb](BoxType /*type*/, const std::string &title, const std::string &text) {
            cb.debug(title + " " + text);
        };
        return WorkCppExecutor::run(plan, executorCb);
    };
    
    std::cerr << "DEBUG: Launching backend in background thread via QtConcurrent" << std::endl;
    
    // Launch backend execution asynchronously
    startBackgroundProcessing(cppSettings, QCoreApplication::applicationName().toStdString(), cb, deps);
    
    // ========================================================================
    // End Phase 3.2 - GUI remains responsive, result handled in onBackgroundProcessingFinished()
    // ========================================================================
    
    ui->outputLabel->clear();
    
    // Note: savePackageList is now handled by the backend workflow
    // Note: editBootMenu will be handled in a later phase
    if (settings->editBootMenu) {
        editBootMenu();
    }

    displayOutput();
    
    // Note: createIso is now handled by the backend workflow above
    ui->btnCancel->setText(tr("Close"));
}

void MainWindow::editBootMenu()
{
    if (QMessageBox::Yes
        == QMessageBox::question(
            this, tr("Edit Boot Menu"),
            tr("The program will now pause to allow you to edit any files in the work directory. "
               "Select Yes to edit the boot menu or select No to bypass this step and continue creating the "
               "snapshot."),
            QMessageBox::Yes | QMessageBox::No)) {
        hide();
        QString cmd = settings->getEditor() + " \"" + settings->workDir + "/iso-template/boot/grub/grub.cfg\" \""
                      + settings->workDir + "/iso-template/boot/syslinux/syslinux.cfg\" \"" + settings->workDir
                      + "/iso-template/boot/isolinux/isolinux.cfg\"";
        procStart();
        (void)CmdCpp().run(cmd.toStdString());
        procDone();
        show();
    }
}

void MainWindow::btnBack_clicked()
{
    setWindowTitle(tr("S4 Snapshot"));
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnNext->setEnabled(true);
    ui->btnBack->setHidden(true);
    ui->outputBox->clear();
}

void MainWindow::btnEditExclude_clicked()
{
    hide();
    const std::string cmd = (settings->getEditor() + " " + settings->snapshotExcludesPath).toStdString();
    procStart();
    (void)CmdCpp().run(cmd);
    procDone();
    updateCustomExcludesButton();
    checkUpdatedDefaultExcludes();
    show();
}

void MainWindow::btnRemoveCustomExclude_clicked()
{
    const QMessageBox::StandardButton response = QMessageBox::question(
        this, tr("Remove Custom Exclusion File"),
        tr("Revert the exclusion list to the default file? This will overwrite your current exclusions."),
        QMessageBox::Yes | QMessageBox::No);
    if (response != QMessageBox::Yes) {
        return;
    }

    if (resetCustomExcludes()) {
        updateCustomExcludesButton();
    }
}

void MainWindow::excludeDocuments_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludeDownloads_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludeFlatpaks_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludePictures_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludeMusic_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludeVideos_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludeDesktop_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::radioRespin_toggled(bool checked)
{
    settings->resetAccounts = checked;
    if (checked && !ui->excludeAll->isChecked()) {
        ui->excludeAll->click();
    }
}

void MainWindow::radioPersonal_clicked(bool checked)
{
    settings->resetAccounts = !checked;
    if (checked && ui->excludeAll->isChecked()) {
        ui->excludeAll->click();
    }
}

void MainWindow::btnAbout_clicked()
{
    hide();
    displayAboutMsgBox(
        tr("About %1").arg(QApplication::applicationDisplayName()),
        "<p align=\"center\"><b><h2>" + QApplication::applicationDisplayName() + "</h2></b></p><p align=\"center\">"
            + tr("Version: ") + QApplication::applicationVersion() + "</p><p align=\"center\"><h3>"
            + tr("Program for creating a live-CD from the running system for Debian")
            + R"(</h3></p><p align="center"><a href="http://debian.org">http://debian.org</a><br /></p><p align="center">)"
            + tr("Copyright (c) Debian") + "<br /><br /></p>",
        QStringLiteral("/usr/share/doc/s4-snapshot/license.html"),
        tr("%1 License").arg(QApplication::applicationDisplayName()));
    show();
}

void MainWindow::btnHelp_clicked()
{
    QLocale locale;
    QString path {"/usr/share/doc/s4-snapshot/s4-snapshot.html"};

    if (locale.bcp47Name().startsWith("fr")) {
        path = "/usr/share/doc/s4-snapshot/s4-snapshot-fr.html";
    }

    displayHelpDoc(path, tr("%1 Help").arg(windowTitle()));
}

void MainWindow::btnSelectSnapshot_clicked()
{
    QString selected = QFileDialog::getExistingDirectory(this, tr("Select Snapshot Directory"), QString(),
                                                         QFileDialog::ShowDirsOnly);
    if (!selected.isEmpty()) {
        const QString previousSnapshotDir = settings->snapshotDir;
        settings->snapshotDir = selected + "/snapshot";
        if (!settings->validateSpaceRequirements()) {
            settings->snapshotDir = previousSnapshotDir;
            QMessageBox::critical(this, tr("Error"),
                                  tr("Insufficient free space in the selected directory. Please choose a different location."));
            return;
        }
        ui->labelSnapshotDir->setText(settings->snapshotDir);
        listFreeSpace();
    }
}

void MainWindow::btnSelectDataFiles_clicked()
{
    const QString current = ui->lineEditDataFilesPath->text().trimmed();
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Select live-files data directory"), current, QFileDialog::ShowDirsOnly);
    if (!selected.isEmpty()) {
        ui->lineEditDataFilesPath->setText(selected);
        qt_settings.setValue("datafiles_path", selected);
    }
}

void MainWindow::lineEditDataFilesPath_editingFinished()
{
    qt_settings.setValue("datafiles_path", ui->lineEditDataFilesPath->text().trimmed());
}

void MainWindow::btnSelectTemplates_clicked()
{
    const QString current = ui->lineEditTemplatesPath->text().trimmed();
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Select ISO templates directory"), current, QFileDialog::ShowDirsOnly);
    if (selected.isEmpty()) {
        return;
    }
    if (!isTemplatesPathValid(selected)) {
        QMessageBox::critical(this,
                              tr("Error"),
                              tr("ISO templates directory is invalid. It must contain iso-template.tar.gz and template-initrd.gz."));
        return;
    }
    ui->lineEditTemplatesPath->setText(selected);
    qt_settings.setValue("templates_path", selected);
}

void MainWindow::lineEditTemplatesPath_editingFinished()
{
    const QString path = ui->lineEditTemplatesPath->text().trimmed();
    if (!isTemplatesPathValid(path)) {
        QMessageBox::critical(this,
                              tr("Error"),
                              tr("ISO templates directory is invalid. It must contain iso-template.tar.gz and template-initrd.gz."));
        ui->lineEditTemplatesPath->setText(qt_settings.value("templates_path").toString());
        return;
    }
    qt_settings.setValue("templates_path", path);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        closeApp();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (ui->stackedWidget->currentWidget() == ui->outputPage && m_operationInProgress.load()) {
        if (QMessageBox::Yes
            != QMessageBox::question(this, tr("Confirmation"), tr("Are you sure you want to quit the application?"),
                                     QMessageBox::Yes | QMessageBox::No)) {
            event->ignore();
            return;
        }
    }

    saveWindowGeometry();
    event->accept();
}

void MainWindow::closeApp()
{
    // Ask for confirmation when on outputPage and not done
    if (ui->stackedWidget->currentWidget() == ui->outputPage && m_operationInProgress.load()) {
        if (QMessageBox::Yes
            != QMessageBox::question(this, tr("Confirmation"), tr("Are you sure you want to quit the application?"),
                                     QMessageBox::Yes | QMessageBox::No)) {
            return;
        }
    }
    saveWindowGeometry();
    cleanUp();
}

void MainWindow::btnCancel_clicked()
{
    // If operation is in progress, request cancellation
    if (m_operationInProgress.load()) {
        if (ui->btnCancel->text() == tr("Cancel")) {
            // SECURITY: Ask for confirmation before cancelling
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Confirm Cancellation"),
                tr("Are you sure you want to cancel the ISO creation?\n\n"
                   "Current progress will be lost and the following will happen:\n"
                   "• The ISO file will be incomplete or missing\n"
                   "• Temporary files will need to be cleaned up\n"
                   "• You will need to restart the process from the beginning\n\n"
                   "Do you really want to cancel?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No  // Default button: No (safety)
            );
            
            // Only cancel if user confirms with "Yes"
            if (reply == QMessageBox::Yes) {
                // Request abort
                m_abortRequested.store(true);
                ui->btnCancel->setEnabled(false);
                ui->btnCancel->setText(tr("Cancelling..."));
                processMsg(tr("Cancellation requested, please wait..."));
                
                // The backend will check m_abortRequested via shouldAbortBackend()
                // and stop gracefully
            }
            // If user clicked "No", do nothing and continue the process
            return;
        }
    }
    
    // Otherwise, close the application (no confirmation needed after completion)
    closeApp();
}

void MainWindow::cbCompression_currentIndexChanged()
{
    QString comp = ui->cbCompression->currentText().section(" ", 0, 0);
    qt_settings.setValue("compression", comp);
    settings->compression = comp;
}

void MainWindow::excludeNetworks_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::checkMd5_toggled(bool checked)
{
    qt_settings.setValue("make_md5sum", checked ? "yes" : "no");
    settings->makeMd5sum = checked;
}

void MainWindow::checkSha512_toggled(bool checked)
{
    qt_settings.setValue("make_sha512sum", checked ? "yes" : "no");
    settings->makeSha512sum = checked;
}

void MainWindow::excludeSteam_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::excludeVirtualBox_toggled(bool checked)
{
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void MainWindow::spinCPU_valueChanged(int arg1)
{
    qt_settings.setValue("cores", arg1);
    settings->cores = arg1;
}

void MainWindow::spinThrottle_valueChanged(int arg1)
{
    qt_settings.setValue("throttle", arg1);
    settings->throttle = arg1;
}

// ============================================================================
// Backend Callback Implementations (Phase 3.1)
// ============================================================================

void MainWindow::handleBackendMessage(const std::string& msg)
{
    std::cerr << "DEBUG: handleBackendMessage() called with: " << msg << std::endl;
    
    // Thread-safe conversion and routing to existing processMsg()
    QString qmsg = QString::fromStdString(msg);
    
    std::cerr << "DEBUG: About to invoke processMsg() via QMetaObject" << std::endl;
    
    // Use Qt's thread-safe mechanism to update GUI
    QMetaObject::invokeMethod(this, [this, qmsg]() {
        std::cerr << "DEBUG: processMsg() lambda executing in GUI thread" << std::endl;
        processMsg(qmsg);
    }, Qt::QueuedConnection);
    
    std::cerr << "DEBUG: QMetaObject::invokeMethod() returned" << std::endl;
}

void MainWindow::handleBackendLog(const std::string& log)
{
    std::cerr << "DEBUG: handleBackendLog() called with: " << log << std::endl;
    
    // Thread-safe log handling
    QString qlog = QString::fromStdString(log);
    
    QMetaObject::invokeMethod(this, [this, qlog]() {
        std::cerr << "DEBUG: processMsg() lambda (from log) executing in GUI thread" << std::endl;
        // Route to existing log display mechanism
        // Logs are displayed through processMsg in the original implementation
        processMsg(qlog);
    }, Qt::QueuedConnection);
}

bool MainWindow::shouldAbortBackend()
{
    // Atomic read - thread-safe by design
    return m_abortRequested.load();
}


// ============================================================================
// Async Backend Execution Methods
// ============================================================================

void MainWindow::startBackgroundProcessing(const SettingsCpp &cppSettings,
                                          const std::string &appName,
                                          const BatchprocessingCppRunner::Callbacks &cb,
                                          const BatchprocessingCppRunner::Dependencies &deps)
{
    // Launch backend execution in a separate thread using QtConcurrent
    QFuture<BatchprocessingCppRunner::Result> future = QtConcurrent::run([=]() {
        std::cerr << "DEBUG: Backend thread started" << std::endl;
        
        // Run the complete ISO creation workflow
        // This replaces: setupEnv(), checkEnoughSpace(), copyNewIso(), savePackageList(), createIso()
        BatchprocessingCppRunner::Result result = BatchprocessingCppRunner::runFromSettings(
            cppSettings,
            appName,
            cb,
            deps
        );
        
        std::cerr << "DEBUG: Backend thread completed - aborted: " << result.aborted << std::endl;
        return result;
    });
    
    // Monitor the future for completion
    m_futureWatcher->setFuture(future);
}

void MainWindow::onBackgroundProcessingFinished()
{
    std::cerr << "=== onBackgroundProcessingFinished() called ===" << std::endl;
    
    // Stop the progress timer
    timer.stop();
    
    // Get the result from the background thread
    BatchprocessingCppRunner::Result result = m_futureWatcher->result();
    
    std::cerr << "DEBUG: Result - aborted: " << result.aborted << std::endl;
    
    // Handle result
    if (result.aborted) {
        processMsg(tr("Error: %1").arg(QString::fromStdString(result.abortReason)));
        ui->progressBar->setValue(0);
        ui->progressBar->setVisible(false);
    } else {
        // Update settings with any changes made by backend
        settings->workDir = QString::fromStdString(result.settings.workDir);
        settings->tempDirParent = QString::fromStdString(result.settings.tempDirParent);
        settings->sessionExcludes = QString::fromStdString(result.settings.sessionExcludes);
        
        // Show completion
        ui->progressBar->setValue(100);
        processMsg(tr("ISO creation completed successfully!"));
    }
    
    // Clear operation state
    m_operationInProgress.store(false);
    
    // RE-ENABLE all controls (execution finished)
    enableControls(true);
    
    // Update button state
    ui->btnCancel->setText(tr("Close"));
    ui->btnCancel->setEnabled(true);
    
    std::cerr << "=== Background processing finished ===" << std::endl;
}

// ============================================================================
// Control State Management - Disable/Enable UI during execution
// ============================================================================
void MainWindow::enableControls(bool enable)
{
    // Main action buttons
    ui->btnNext->setEnabled(enable);
    ui->btnBack->setEnabled(enable);
    ui->btnAbout->setEnabled(enable);
    ui->btnHelp->setEnabled(enable);
    ui->btnSelectSnapshot->setEnabled(enable);
    ui->btnSelectDataFiles->setEnabled(enable);
    ui->btnSelectTemplates->setEnabled(enable);
    ui->btnEditExclude->setEnabled(enable);
    ui->btnRemoveCustomExclude->setEnabled(enable);
    
    // Selection page controls
    ui->textProjectName->setEnabled(enable);
    ui->textDistroVersion->setEnabled(enable);
    ui->textCodename->setEnabled(enable);
    ui->textOptions->setEnabled(enable);
    ui->comboLiveKernel->setEnabled(enable);
    ui->pushReleaseDate->setEnabled(enable);
    ui->lineEditName->setEnabled(enable);
    ui->lineEditDataFilesPath->setEnabled(enable);
    ui->lineEditTemplatesPath->setEnabled(enable);

    // Settings page - Exclusion checkboxes
    ui->excludeAll->setEnabled(enable);
    ui->excludeDesktop->setEnabled(enable);
    ui->excludeDocuments->setEnabled(enable);
    ui->excludeDownloads->setEnabled(enable);
    ui->excludeMusic->setEnabled(enable);
    ui->excludePictures->setEnabled(enable);
    ui->excludeVideos->setEnabled(enable);
    ui->excludeVirtualBox->setEnabled(enable);
    ui->excludeSteam->setEnabled(enable);
    ui->excludeFlatpaks->setEnabled(enable);
    ui->excludeNetworks->setEnabled(enable);
    
    // Settings page - Other options
    ui->cbCompression->setEnabled(enable);
    ui->checkMd5->setEnabled(enable);
    ui->checkSha512->setEnabled(enable);
    ui->radioPersonal->setEnabled(enable);
    ui->radioRespin->setEnabled(enable);
    ui->spinCPU->setEnabled(enable);
    ui->spinThrottle->setEnabled(enable);
    
    // Cancel button: INVERSE logic - active when controls disabled
    ui->btnCancel->setEnabled(!enable);
}
