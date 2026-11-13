// src/ui/AiPreferencesDialog.cpp
#include "AiPreferencesDialog.h"
#include "../ai/AiProvider.h"
#include <QTabWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QLabel>
#include <QLocale>
#include <QSet>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace {

struct PromptActionMeta {
    const char* key;
    const char* menuLabel;
    const char* hint;
};

constexpr PromptActionMeta kPromptActions[] = {
    {"ExplainCode",         "Explain Code",                             "Triggered by 'AI -> Current File -> Explain Code'. Output is rendered as Markdown in a dialog."},
    {"RefactorCode",        "Refactor & Clean Up",                      "Triggered by 'AI -> Current File -> Refactor & Clean Up'. Replaces selected or full code in editor."},
    {"FindBugs",            "Find Bugs & Edge Cases",                   "Triggered by 'AI -> Current File -> Find Bugs & Edge Cases'. Replaces selected or full code in editor."},
    {"OptimizePerformance", "Optimize Performance",                     "Triggered by 'AI -> Current File -> Optimize Performance'. Replaces selected or full code in editor."},
    {"AddDocumentation",    "Add Documentation / Comments",             "Triggered by 'AI -> Current File -> Add Documentation / Comments'. Replaces selected or full code in editor."},
    {"GenerateUnitTests",   "Generate Unit Tests",                      "Triggered by 'AI -> Current File -> Generate Unit Tests'. Replaces or appends unit test code in editor."},
    {"CompleteCode",        "Complete / Continue Code",                 "Triggered by 'AI -> Current File -> Complete / Continue Code'. Completes implementation in editor."},
    {"CustomPrompt",        "Custom Prompt... (Document Instruction)",  "Appended after user's custom instruction in 'AI -> Current File -> Custom Prompt...' for a document."},
    {"ProjectCustomPrompt", "Custom Prompt for Project... (Markdown)",  "Appended after user's custom instruction in 'AI -> Project -> Custom Prompt...'. Requests Markdown output."},
    {"FileSummary",         "Rebuild File Summary",                     "Used when indexing a source file into Markdown summary (background or manual)."},
    {"ProjectSummary",      "Rebuild Project Summary",                  "Used when synthesizing the global project architecture summary from file summaries."}
};

} // namespace

// ============================================================================
// LocalLlmProcessManager
// ============================================================================

LocalLlmProcessManager& LocalLlmProcessManager::instance() {
    // ВАЖНО: parent должен быть nullptr, чтобы QApplication::~QApplication()
    // не пытался вызвать operator delete для статического объекта!
    static LocalLlmProcessManager inst(nullptr);
    return inst;
}

LocalLlmProcessManager::LocalLlmProcessManager(QObject* parent)
    : QObject(parent),
    m_process(new QProcess(this)),
    m_killFallbackTimer(new QTimer(this)) {
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    m_killFallbackTimer->setSingleShot(true);
    m_killFallbackTimer->setInterval(1500);
    connect(m_killFallbackTimer, &QTimer::timeout, this, [this]() {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            emit debugLog("LOCAL_LLM",
                          "Force killing local LLM process (timeout waiting for graceful exit)",
                          m_launchedExecPath);
            m_process->kill();
        }
    });

    connect(m_process, &QProcess::started, this, [this]() {
        if (!m_process) return;
        const QString exeName = QFileInfo(m_launchedExecPath).fileName();
        const qint64 pid = m_process->processId();
        emit debugLog("LOCAL_LLM",
                      QString("Process started: %1 (PID: %2)").arg(exeName).arg(pid),
                      QString("Executable: %1\nArguments: %2")
                          .arg(m_launchedExecPath, m_launchedCliArgs.isEmpty() ? "(none)" : m_launchedCliArgs));

        QTimer::singleShot(450, this, [this]() {
            if (m_transitionState == TransitionState::Starting ||
                m_transitionState == TransitionState::Restarting) {
                finishTransitionToIdle();
            }
        });
    });

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process) return;
        const QString out = QString::fromLocal8Bit(m_process->readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            const QString exeName = QFileInfo(m_launchedExecPath).fileName();
            emit debugLog("LLM_STDOUT",
                          QString("%1 [stdout] (PID: %2)").arg(exeName).arg(m_process->processId()),
                          out);
        }
    });

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process) return;
        const QString err = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
        if (!err.isEmpty()) {
            const QString exeName = QFileInfo(m_launchedExecPath).fileName();
            emit debugLog("LLM_STDERR",
                          QString("%1 [stderr] (PID: %2)").arg(exeName).arg(m_process->processId()),
                          err);
        }
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (!m_process) return;
        if (err == QProcess::Crashed &&
            (m_transitionState == TransitionState::Stopping || m_transitionState == TransitionState::Restarting)) {
            return;
        }
        m_lastError = m_process->errorString();
        emit debugLog("LOCAL_LLM",
                      "Process error: " + m_lastError,
                      m_launchedExecPath);
        if (err == QProcess::FailedToStart) {
            if (m_killFallbackTimer) m_killFallbackTimer->stop();
            finishTransitionToIdle();
            emit processFailure("Failed to start local LLM executable:\n" + m_lastError);
        }
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_killFallbackTimer) m_killFallbackTimer->stop();
                const QString exeName = QFileInfo(m_launchedExecPath).fileName();
                emit debugLog("LOCAL_LLM",
                              QString("Process finished: %1 (exit code: %2, status: %3)")
                                  .arg(exeName)
                                  .arg(exitCode)
                                  .arg(exitStatus == QProcess::NormalExit ? "Normal" : "Terminated"),
                              {});

                if (m_transitionState == TransitionState::Restarting) {
                    const QString nextExec = m_pendingExecPath;
                    const QString nextArgs = m_pendingCliArgs;
                    m_pendingExecPath.clear();
                    m_pendingCliArgs.clear();
                    QTimer::singleShot(300, this, [this, nextExec, nextArgs]() {
                        beginStartInternal(nextExec, nextArgs);
                    });
                    return;
                }

                QTimer::singleShot(350, this, [this]() {
                    finishTransitionToIdle();
                });
            });

    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &LocalLlmProcessManager::shutdown);
    }
}

LocalLlmProcessManager::~LocalLlmProcessManager() {
    shutdown();
}

void LocalLlmProcessManager::shutdown() {
    if (m_killFallbackTimer) {
        m_killFallbackTimer->stop();
        delete m_killFallbackTimer;
        m_killFallbackTimer = nullptr;
    }
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(800)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        delete m_process;
        m_process = nullptr;
    }
    m_transitionState = TransitionState::Idle;
}

void LocalLlmProcessManager::finishTransitionToIdle() {
    m_transitionState = TransitionState::Idle;
    emit processStateChanged();
}

QString LocalLlmProcessManager::normalizeExecPath(const QString& path) {
    QString trimmed = path.trimmed();
    if (trimmed.startsWith('"') && trimmed.endsWith('"') && trimmed.length() >= 2) {
        trimmed = trimmed.mid(1, trimmed.length() - 2).trimmed();
    }
    if (trimmed.isEmpty()) return {};
    QFileInfo fi(trimmed);
    QString canonical = fi.canonicalFilePath();
    QString resolved = canonical.isEmpty() ? fi.absoluteFilePath() : canonical;
    return QDir::cleanPath(resolved);
}

bool LocalLlmProcessManager::isManagedRunning() const {
    return m_process && (m_process->state() == QProcess::Running || m_process->state() == QProcess::Starting);
}

qint64 LocalLlmProcessManager::managedPid() const {
    return isManagedRunning() ? m_process->processId() : 0;
}

qint64 LocalLlmProcessManager::findExternalProcessPid(const QString& execPath, qint64 excludePid) {
    const QString normTarget = normalizeExecPath(execPath);
    if (normTarget.isEmpty()) return 0;

    const QFileInfo targetFi(normTarget);
    const QString targetFileName = targetFi.fileName();
    if (targetFileName.isEmpty()) return 0;

#ifdef Q_OS_WIN
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    const DWORD selfPid = GetCurrentProcessId();
    qint64 matchedPid = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            const qint64 pid = static_cast<qint64>(pe.th32ProcessID);
            if (pid <= 0 || pid == static_cast<qint64>(selfPid) || (excludePid > 0 && pid == excludePid)) {
                continue;
            }
            const QString exeName = QString::fromWCharArray(pe.szExeFile);
            if (exeName.compare(targetFileName, Qt::CaseInsensitive) != 0) {
                continue;
            }

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (hProc) {
                wchar_t fullPathBuf[MAX_PATH * 4]{};
                DWORD bufLen = static_cast<DWORD>(std::size(fullPathBuf));
                if (QueryFullProcessImageNameW(hProc, 0, fullPathBuf, &bufLen)) {
                    const QString procNorm = normalizeExecPath(QString::fromWCharArray(fullPathBuf, bufLen));
                    CloseHandle(hProc);
                    if (procNorm.compare(normTarget, Qt::CaseInsensitive) == 0) {
                        matchedPid = pid;
                        break;
                    }
                    continue;
                }
                CloseHandle(hProc);
            }
            matchedPid = pid;
            break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return matchedPid;
#else
    QDir procDir("/proc");
    if (!procDir.exists()) return 0;

    const qint64 selfPid = QCoreApplication::applicationPid();
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        bool ok = false;
        qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0 || pid == selfPid || (excludePid > 0 && pid == excludePid)) {
            continue;
        }
        const QString exeLink = QFile::symLinkTarget(QString("/proc/%1/exe").arg(pid));
        if (exeLink.isEmpty()) continue;
        if (normalizeExecPath(exeLink) == normTarget) {
            return pid;
        }
    }
    return 0;
#endif
}

bool LocalLlmProcessManager::terminateExternalProcessPid(qint64 pid) {
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!hProc) return false;
    BOOL ok = TerminateProcess(hProc, 0);
    if (ok) {
        WaitForSingleObject(hProc, 1500);
    }
    CloseHandle(hProc);
    return ok != FALSE;
#else
    if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
        for (int i = 0; i < 10; ++i) {
            if (::kill(static_cast<pid_t>(pid), 0) != 0) return true;
            QThread::msleep(50);
        }
        ::kill(static_cast<pid_t>(pid), SIGKILL);
        return true;
    }
    return false;
#endif
}

bool LocalLlmProcessManager::isExecutableRunning(const QString& execPath,
                                                 qint64* outPid,
                                                 bool* outIsExternal) const {
    if (isManagedRunning()) {
        if (outPid) *outPid = m_process->processId();
        if (outIsExternal) *outIsExternal = false;
        return true;
    }
    const qint64 extPid = findExternalProcessPid(execPath, 0);
    if (extPid > 0) {
        if (outPid) *outPid = extPid;
        if (outIsExternal) *outIsExternal = true;
        return true;
    }
    if (outPid) *outPid = 0;
    if (outIsExternal) *outIsExternal = false;
    return false;
}

bool LocalLlmProcessManager::isRunningConfigModified(const QString& currentExecPath,
                                                     const QString& currentCliArgs) const {
    if (!isManagedRunning()) return false;
    const QString normCurrent = normalizeExecPath(currentExecPath);
    const QString normLaunched = normalizeExecPath(m_launchedExecPath);
#ifdef Q_OS_WIN
    const bool execChanged = (normCurrent.compare(normLaunched, Qt::CaseInsensitive) != 0);
#else
    const bool execChanged = (normCurrent != normLaunched);
#endif
    const bool argsChanged = (currentCliArgs.trimmed() != m_launchedCliArgs.trimmed());
    return execChanged || argsChanged;
}

void LocalLlmProcessManager::beginStartInternal(const QString& execPath, const QString& cliArgs) {
    if (!m_process) return;
    const QString normExec = normalizeExecPath(execPath);
    if (normExec.isEmpty() || !QFileInfo::exists(normExec) || !QFileInfo(normExec).isFile()) {
        m_lastError = "Executable file does not exist: " + execPath;
        emit debugLog("LOCAL_LLM", m_lastError, {});
        finishTransitionToIdle();
        emit processFailure(m_lastError);
        return;
    }

    qint64 activePid = 0;
    bool isExt = false;
    if (isExecutableRunning(normExec, &activePid, &isExt)) {
        m_lastError = QString("Executable is already running (PID: %1%2).")
        .arg(activePid)
            .arg(isExt ? ", external OS process" : "");
        emit debugLog("LOCAL_LLM", m_lastError, normExec);
        finishTransitionToIdle();
        return;
    }

    const QStringList argsList = QProcess::splitCommand(cliArgs.trimmed());
    m_launchedExecPath = normExec;
    m_launchedCliArgs = cliArgs.trimmed();
    m_lastError.clear();

    m_process->setWorkingDirectory(QFileInfo(normExec).absolutePath());
    m_process->start(normExec, argsList);
}

void LocalLlmProcessManager::startProcess(const QString& execPath, const QString& cliArgs) {
    if (isTransitioning()) return;
    m_transitionState = TransitionState::Starting;
    emit processStateChanged();

    QTimer::singleShot(150, this, [this, execPath, cliArgs]() {
        beginStartInternal(execPath, cliArgs);
    });
}

void LocalLlmProcessManager::stopProcess(const QString& execPath) {
    if (isTransitioning()) return;

    if (isManagedRunning()) {
        m_transitionState = TransitionState::Stopping;
        emit processStateChanged();
        emit debugLog("LOCAL_LLM",
                      QString("Stopping local LLM process (PID: %1)...").arg(m_process->processId()),
                      m_launchedExecPath);
        m_process->terminate();
        if (m_killFallbackTimer) m_killFallbackTimer->start();
        return;
    }

    const QString targetExec = execPath.isEmpty() ? m_launchedExecPath : execPath;
    const qint64 extPid = findExternalProcessPid(targetExec, 0);
    if (extPid > 0) {
        m_transitionState = TransitionState::Stopping;
        emit processStateChanged();
        emit debugLog("LOCAL_LLM",
                      QString("Stopping external OS process (PID: %1)...").arg(extPid),
                      targetExec);

        QTimer::singleShot(300, this, [this, extPid]() {
            bool ok = terminateExternalProcessPid(extPid);
            if (!ok) {
                m_lastError = QString("Could not terminate external process (PID: %1).").arg(extPid);
                emit debugLog("LOCAL_LLM", m_lastError, {});
                emit processFailure(m_lastError);
            } else {
                emit debugLog("LOCAL_LLM", QString("External process (PID: %1) terminated.").arg(extPid), {});
            }
            finishTransitionToIdle();
        });
    }
}

void LocalLlmProcessManager::restartProcess(const QString& execPath, const QString& cliArgs) {
    if (isTransitioning()) return;

    m_pendingExecPath = execPath;
    m_pendingCliArgs = cliArgs;
    m_transitionState = TransitionState::Restarting;
    emit processStateChanged();

    if (isManagedRunning()) {
        emit debugLog("LOCAL_LLM",
                      QString("Restarting local LLM process (stopping PID: %1)...").arg(m_process->processId()),
                      execPath);
        m_process->terminate();
        if (m_killFallbackTimer) m_killFallbackTimer->start();
        return;
    }

    const qint64 extPid = findExternalProcessPid(execPath, 0);
    if (extPid > 0) {
        emit debugLog("LOCAL_LLM",
                      QString("Restarting external OS process (stopping PID: %1)...").arg(extPid),
                      execPath);
        QTimer::singleShot(250, this, [this, extPid]() {
            terminateExternalProcessPid(extPid);
            const QString nextExec = m_pendingExecPath;
            const QString nextArgs = m_pendingCliArgs;
            m_pendingExecPath.clear();
            m_pendingCliArgs.clear();
            beginStartInternal(nextExec, nextArgs);
        });
        return;
    }

    QTimer::singleShot(200, this, [this]() {
        const QString nextExec = m_pendingExecPath;
        const QString nextArgs = m_pendingCliArgs;
        m_pendingExecPath.clear();
        m_pendingCliArgs.clear();
        beginStartInternal(nextExec, nextArgs);
    });
}

void LocalLlmProcessManager::syncWithProfile(const AiConnectionConfig& cfg, bool autoStartEnabled) {
    if (!autoStartEnabled) return;

    const bool wantsLocal = cfg.useLocalExecutable && !cfg.localExecutablePath.trimmed().isEmpty();
    if (wantsLocal) {
        const QString normExec = normalizeExecPath(cfg.localExecutablePath);
        if (normExec.isEmpty() || !QFileInfo::exists(normExec)) {
            emit debugLog("LOCAL_LLM",
                          QString("Auto-start skipped for profile '%1': executable not found").arg(cfg.profileName),
                          cfg.localExecutablePath);
            return;
        }

        if (isManagedRunning()) {
            if (isRunningConfigModified(cfg.localExecutablePath, cfg.localCommandLineArgs)) {
                emit debugLog("LOCAL_LLM",
                              QString("Profile switched to '%1': restarting local LLM process with new parameters")
                                  .arg(cfg.profileName),
                              normExec);
                restartProcess(cfg.localExecutablePath, cfg.localCommandLineArgs);
            }
        } else {
            qint64 extPid = 0;
            if (!isExecutableRunning(cfg.localExecutablePath, &extPid, nullptr)) {
                emit debugLog("LOCAL_LLM",
                              QString("Auto-starting local LLM executable for profile '%1'").arg(cfg.profileName),
                              normExec);
                startProcess(cfg.localExecutablePath, cfg.localCommandLineArgs);
            }
        }
    } else {
        // Если переключились на профиль без локального исполняемого файла — останавливаем прежний запущенный процесс
        if (isManagedRunning()) {
            emit debugLog("LOCAL_LLM",
                          QString("Profile switched to '%1' (no local executable): stopping previous LLM process")
                              .arg(cfg.profileName),
                          m_launchedExecPath);
            stopProcess();
        }
    }
}

// ============================================================================
// AiPreferencesDialog
// ============================================================================

AiPreferencesDialog::AiPreferencesDialog(AiProvider* provider, QWidget* parent)
    : QDialog(parent), m_provider(provider) {
    setWindowTitle("AI Preferences");
    resize(680, 790);

    auto& cfgMgr = ConfigManager::instance();
    m_profiles = cfgMgr.aiProfiles();
    if (m_profiles.isEmpty()) m_profiles = ConfigManager::defaultAiProfiles();
    m_promptTemplates = cfgMgr.promptTemplates();
    auto behCfg = cfgMgr.aiBehavior();

    auto* mainLay = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    // =========================================================================
    // Вкладка 1: Connection & Profiles
    // =========================================================================
    auto* connPage = new QWidget(tabs);
    auto* connLay = new QVBoxLayout(connPage);

    auto* profileGroup = new QGroupBox("Connection Profile", connPage);
    auto* profileRow = new QHBoxLayout(profileGroup);
    m_profileCombo = new QComboBox(profileGroup);
    m_newProfileBtn = new QPushButton("New...", profileGroup);
    m_deleteProfileBtn = new QPushButton("Delete", profileGroup);
    m_resetProfilesBtn = new QPushButton("Reset Presets", profileGroup);
    profileRow->addWidget(new QLabel("Profile:", profileGroup));
    profileRow->addWidget(m_profileCombo, 1);
    profileRow->addWidget(m_newProfileBtn);
    profileRow->addWidget(m_deleteProfileBtn);
    profileRow->addWidget(m_resetProfilesBtn);
    connLay->addWidget(profileGroup);

    // Чекбокс использования локального исполняемого файла для текущего профиля
    m_useLocalExecCheck = new QCheckBox("Use / Launch Local LLM Executable for This Profile", connPage);
    m_useLocalExecCheck->setStyleSheet("font-weight: bold;");
    connLay->addWidget(m_useLocalExecCheck);

    // Секция под чекбоксом
    m_localExecGroup = new QGroupBox("Local LLM Executable && Command Line", connPage);
    auto* localExecLay = new QVBoxLayout(m_localExecGroup);
    localExecLay->setSpacing(6);

    // Общий чекбокс автозапуска для всех оффлайн-профилей с указанным исполняемым файлом
    m_autoStartLocalExecCheck = new QCheckBox(
        "Auto-start local executable on AIDE launch and switch process when changing profiles",
        m_localExecGroup);
    m_autoStartLocalExecCheck->setToolTip(
        "Shared option across offline profiles: automatically launches the configured executable when AIDE starts, "
        "and stops the previous process / starts the new one when switching profiles.");
    m_autoStartLocalExecCheck->setChecked(behCfg.autoStartLocalExecutable);
    localExecLay->addWidget(m_autoStartLocalExecCheck);

    auto* localForm = new QFormLayout();
    auto* execPathRow = new QHBoxLayout();
    m_localExecPathEdit = new QLineEdit(m_localExecGroup);
    m_localExecPathEdit->setPlaceholderText("Path to local LLM server binary (e.g. llama-server.exe, koboldcpp.exe)...");
    m_browseLocalExecBtn = new QPushButton("Browse...", m_localExecGroup);
    m_browseLocalExecBtn->setToolTip("Select local LLM executable file from filesystem");
    execPathRow->addWidget(m_localExecPathEdit, 1);
    execPathRow->addWidget(m_browseLocalExecBtn);

    auto* execArgsRow = new QHBoxLayout();
    m_localExecArgsEdit = new QLineEdit(m_localExecGroup);
    m_localExecArgsEdit->setPlaceholderText("e.g. -m \"model.gguf\" --port 5001 -c 16384 --ngl 99 (supports {model})");
    m_browseLocalModelBtn = new QPushButton("Browse Model...", m_localExecGroup);
    m_browseLocalModelBtn->setToolTip("Select a model file from filesystem and insert/replace its path in Command Line Args");
    execArgsRow->addWidget(m_localExecArgsEdit, 1);
    execArgsRow->addWidget(m_browseLocalModelBtn);

    localForm->addRow("Executable File:", execPathRow);
    localForm->addRow("Command Line Args:", execArgsRow);
    localExecLay->addLayout(localForm);

    auto* procCtrlRow = new QHBoxLayout();
    m_startLocalExecBtn = new QPushButton("Start", m_localExecGroup);
    m_startLocalExecBtn->setStyleSheet(
        "QPushButton { background-color: #50fa7b; color: #191a24; font-weight: bold; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #69ff94; }"
        "QPushButton:disabled { background-color: #44475a; color: #888899; }");

    m_stopLocalExecBtn = new QPushButton("Stop", m_localExecGroup);
    m_stopLocalExecBtn->setStyleSheet(
        "QPushButton { background-color: #ff5555; color: #ffffff; font-weight: bold; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #ff6e6e; }"
        "QPushButton:disabled { background-color: #44475a; color: #888899; }");

    m_restartLocalExecBtn = new QPushButton("Restart", m_localExecGroup);
    m_restartLocalExecBtn->setToolTip("Restart local executable (use when command-line parameters or executable path changed)");
    m_restartLocalExecBtn->setStyleSheet(
        "QPushButton { background-color: #ffb86c; color: #191a24; font-weight: bold; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #ffc98a; }"
        "QPushButton:disabled { background-color: #44475a; color: #888899; }");

    m_localExecStatusLabel = new QLabel("Status: Not running", m_localExecGroup);
    m_localExecStatusLabel->setWordWrap(true);

    procCtrlRow->addWidget(m_startLocalExecBtn);
    procCtrlRow->addWidget(m_stopLocalExecBtn);
    procCtrlRow->addWidget(m_restartLocalExecBtn);
    procCtrlRow->addWidget(m_localExecStatusLabel, 1);
    localExecLay->addLayout(procCtrlRow);

    // Прогресс-бар с диапазоном (0, 0) для индикации запуска, остановки и перезапуска
    m_localExecProgressBar = new QProgressBar(m_localExecGroup);
    m_localExecProgressBar->setMinimumHeight(14);
    m_localExecProgressBar->setMaximumHeight(14);
    m_localExecProgressBar->setRange(0, 0);
    m_localExecProgressBar->setTextVisible(false);
    m_localExecProgressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #44475a; border-radius: 4px; background: #191a22; }"
        "QProgressBar::chunk { background-color: #bd93f9; border-radius: 3px; }");
    m_localExecProgressBar->hide();
    localExecLay->addWidget(m_localExecProgressBar);

    connLay->addWidget(m_localExecGroup);

    auto* form = new QFormLayout();

    m_backendCombo = new QComboBox(connPage);
    m_backendCombo->addItems({"Ollama (Native API)", "KoboldCpp (Native / SSE)", "OpenAI-Compatible (/v1/chat/completions)"});

    m_endpointEdit = new QLineEdit(connPage);
    m_apiKeyEdit = new QLineEdit(connPage);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText("Optional Bearer API Key (e.g. for ollama.com)");

    auto* modelRow = new QHBoxLayout();
    m_modelCombo = new QComboBox(connPage);
    m_modelCombo->setEditable(true);
    m_fetchBtn = new QPushButton("Test / Fetch Models", connPage);
    modelRow->addWidget(m_modelCombo, 1);
    modelRow->addWidget(m_fetchBtn);

    m_statusLabel = new QLabel(connPage);

    m_tempSpin = new QDoubleSpinBox(connPage);
    m_tempSpin->setRange(0.0, 2.0);
    m_tempSpin->setSingleStep(0.05);

    m_topPSpin = new QDoubleSpinBox(connPage);
    m_topPSpin->setRange(0.0, 1.0);
    m_topPSpin->setSingleStep(0.05);

    m_maxCodeTokensSpin = new QSpinBox(connPage);
    m_maxCodeTokensSpin->setRange(64, 262144);
    m_maxCodeTokensSpin->setSingleStep(1024);
    m_maxCodeTokensSpin->setValue(65536);

    m_maxSummaryTokensSpin = new QSpinBox(connPage);
    m_maxSummaryTokensSpin->setRange(64, 65536);
    m_maxSummaryTokensSpin->setSingleStep(64);
    m_maxSummaryTokensSpin->setValue(1024);

    m_ctxWindowSpin = new QSpinBox(connPage);
    m_ctxWindowSpin->setRange(1024, 1048576);
    m_ctxWindowSpin->setSingleStep(4096);
    m_ctxWindowSpin->setValue(131072);

    m_timeoutSpin = new QSpinBox(connPage);
    m_timeoutSpin->setRange(10, 3600);
    m_timeoutSpin->setSuffix(" sec");

    m_summaryIntervalSpin = new QSpinBox(connPage);
    m_summaryIntervalSpin->setRange(1, 120);
    m_summaryIntervalSpin->setSuffix(" min");

    form->addRow("Provider:", m_backendCombo);
    form->addRow("Endpoint URL:", m_endpointEdit);
    form->addRow("API Key:", m_apiKeyEdit);
    form->addRow("Model Name:", modelRow);
    form->addRow("", m_statusLabel);
    form->addRow("Temperature:", m_tempSpin);
    form->addRow("Top-P:", m_topPSpin);
    form->addRow("Max Output Code Tokens:", m_maxCodeTokensSpin);
    form->addRow("Max Output Summary Tokens:", m_maxSummaryTokensSpin);
    form->addRow("Context Window:", m_ctxWindowSpin);
    form->addRow("Request Timeout:", m_timeoutSpin);
    form->addRow("Project Summary Rebuild:", m_summaryIntervalSpin);
    connLay->addLayout(form);
    connLay->addStretch();

    tabs->addTab(connPage, "Connection && Profiles");

    // =========================================================================
    // Вкладка 2: Behavior & Summary Rules
    // =========================================================================
    auto* behPage = new QWidget(tabs);
    auto* behLay = new QVBoxLayout(behPage);

    auto* localeGroup = new QGroupBox("Localization (Markdown && Code Comments Language)", behPage);
    auto* localeLay = new QHBoxLayout(localeGroup);
    localeLay->addWidget(new QLabel("Output Language:", localeGroup));
    m_localeCombo = new QComboBox(localeGroup);
    m_localeCombo->addItem("Default (No language instruction)", QStringLiteral("default"));

    struct LanguageEntry {
        QLocale::Language language = QLocale::AnyLanguage;
        QString code;
        QString langName;
        QString label;
    };
    QVector<LanguageEntry> langEntries;
    QSet<QLocale::Language> seenLanguages;

    const QList<QLocale> sysLocales = QLocale::matchingLocales(
        QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
    for (const QLocale& loc : sysLocales) {
        const QLocale::Language lang = loc.language();
        if (lang == QLocale::C || lang == QLocale::AnyLanguage || seenLanguages.contains(lang)) {
            continue;
        }
        const QString langStr = QLocale::languageToString(lang).trimmed();
        const QString langCode = QLocale::languageToCode(lang).trimmed();
        if (langStr.isEmpty() || langCode.isEmpty()) {
            continue;
        }
        seenLanguages.insert(lang);

        QString nativeLang = QLocale(lang).nativeLanguageName().trimmed();
        if (nativeLang.isEmpty()) {
            nativeLang = loc.nativeLanguageName().trimmed();
        }
        QString disp = langStr;
        if (!nativeLang.isEmpty() && nativeLang.compare(langStr, Qt::CaseInsensitive) != 0) {
            disp += QString(" [%1]").arg(nativeLang);
        }
        langEntries.append({lang, langCode, langStr, disp});
    }

    std::sort(langEntries.begin(), langEntries.end(), [](const LanguageEntry& a, const LanguageEntry& b) {
        return a.langName.compare(b.langName, Qt::CaseInsensitive) < 0;
    });

    int selectedLocaleIdx = 0;
    const QString targetLocale = behCfg.responseLocale.trimmed().isEmpty()
                                     ? QStringLiteral("default")
                                     : behCfg.responseLocale.trimmed();
    const QLocale::Language targetLangEnum =
        (targetLocale.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0)
            ? QLocale::AnyLanguage
            : QLocale(targetLocale).language();

    for (int i = 0; i < langEntries.size(); ++i) {
        m_localeCombo->addItem(langEntries[i].label, langEntries[i].code);
        if (langEntries[i].code.compare(targetLocale, Qt::CaseInsensitive) == 0 ||
            langEntries[i].langName.compare(targetLocale, Qt::CaseInsensitive) == 0 ||
            (targetLangEnum != QLocale::AnyLanguage &&
             targetLangEnum != QLocale::C &&
             langEntries[i].language == targetLangEnum)) {
            selectedLocaleIdx = i + 1;
        }
    }
    m_localeCombo->setCurrentIndex(selectedLocaleIdx);
    localeLay->addWidget(m_localeCombo, 1);
    behLay->addWidget(localeGroup);

    auto* promptGroup = new QGroupBox("System Prompt Context Composition (When Working with a Document)", behPage);
    auto* promptLay = new QVBoxLayout(promptGroup);
    m_useDocSummaryCheck = new QCheckBox("Include Document AI Summary in system prompt", promptGroup);
    m_useDocSummaryCheck->setChecked(behCfg.includeDocumentSummaryInPrompt);
    m_useProjSummaryCheck = new QCheckBox("Include Project Architecture Summary in system prompt", promptGroup);
    m_useProjSummaryCheck->setChecked(behCfg.includeProjectSummaryInPrompt);
    promptLay->addWidget(m_useDocSummaryCheck);
    promptLay->addWidget(m_useProjSummaryCheck);
    behLay->addWidget(promptGroup);

    auto* projPromptCtxGroup = new QGroupBox("Custom Prompt for Project — Context Source", behPage);
    auto* projPromptCtxLay = new QHBoxLayout(projPromptCtxGroup);
    projPromptCtxLay->addWidget(new QLabel("Context Provided to LLM:", projPromptCtxGroup));
    m_projCustomPromptCtxCombo = new QComboBox(projPromptCtxGroup);
    m_projCustomPromptCtxCombo->addItem(
        "1. Project Summary Only",
        static_cast<int>(ProjectCustomPromptContextMode::ProjectSummaryOnly));
    m_projCustomPromptCtxCombo->addItem(
        "2. Selected Project Files (Concatenated Internal Markdown)",
        static_cast<int>(ProjectCustomPromptContextMode::IndexedSourcesMarkdownOnly));
    m_projCustomPromptCtxCombo->addItem(
        "3. Both Project Summary and Selected Files Markdown",
        static_cast<int>(ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown));
    m_projCustomPromptCtxCombo->setCurrentIndex(
        qBound(0, static_cast<int>(behCfg.projectCustomPromptContextMode), 2));
    projPromptCtxLay->addWidget(m_projCustomPromptCtxCombo, 1);
    behLay->addWidget(projPromptCtxGroup);

    auto* filterGroup = new QGroupBox("Automatic && Recursive File Summary Generation Rules", behPage);
    auto* filterLay = new QVBoxLayout(filterGroup);

    auto* autoModeRow = new QHBoxLayout();
    autoModeRow->addWidget(new QLabel("Automatic Summary Creation:", filterGroup));
    m_autoSummaryModeCombo = new QComboBox(filterGroup);
    m_autoSummaryModeCombo->addItem("Do not auto-create summaries", static_cast<int>(AutoSummaryMode::None));
    m_autoSummaryModeCombo->addItem("Create file summaries only", static_cast<int>(AutoSummaryMode::FilesOnly));
    m_autoSummaryModeCombo->addItem("Create file and project summaries", static_cast<int>(AutoSummaryMode::FilesAndProject));
    m_autoSummaryModeCombo->setCurrentIndex(qBound(0, static_cast<int>(behCfg.autoSummaryMode), 2));
    autoModeRow->addWidget(m_autoSummaryModeCombo, 1);
    filterLay->addLayout(autoModeRow);

    m_useSyntaxExtensionsCheck = new QCheckBox(
        "Automatically include extensions from downloaded syntax highlighting definitions", filterGroup);
    m_useSyntaxExtensionsCheck->setChecked(behCfg.summaryUseSyntaxExtensions);
    filterLay->addWidget(m_useSyntaxExtensionsCheck);

    filterLay->addWidget(new QLabel(
        "Exact File Names / Wildcards for Summary (e.g. CMakeLists.txt is indexed, while readme.txt is skipped):",
        filterGroup));
    m_allowedFilenamesEdit = new QLineEdit(behCfg.summaryAllowedFilenames.join(", "), filterGroup);
    m_allowedFilenamesEdit->setPlaceholderText("CMakeLists.txt, Makefile, Dockerfile...");
    filterLay->addWidget(m_allowedFilenamesEdit);

    auto* extHeaderRow = new QHBoxLayout();
    extHeaderRow->addWidget(new QLabel("Allowed File Extensions for Summary (comma-separated):", filterGroup));
    extHeaderRow->addStretch();
    auto* syncSyntaxExtBtn = new QPushButton("Add Extensions from Loaded Syntaxes", filterGroup);
    extHeaderRow->addWidget(syncSyntaxExtBtn);
    filterLay->addLayout(extHeaderRow);

    m_allowedExtensionsEdit = new QPlainTextEdit(behCfg.summaryAllowedExtensions.join(", "), filterGroup);
    m_allowedExtensionsEdit->setPlaceholderText("cpp, h, c, py, sh, sql, js, ts...");
    m_allowedExtensionsEdit->setMaximumHeight(85);
    filterLay->addWidget(m_allowedExtensionsEdit);

    filterLay->addWidget(new QLabel(
        "Ignored Folders for Recursive Summary (comma-separated names or wildcards, e.g. .*, build, node_modules):",
        filterGroup));
    m_ignoredDirsEdit = new QPlainTextEdit(behCfg.summaryIgnoredDirs.join(", "), filterGroup);
    m_ignoredDirsEdit->setPlaceholderText(".*, build, build-*, cmake-build-*, out, dist, bin, obj, node_modules, __pycache__, venv...");
    m_ignoredDirsEdit->setMaximumHeight(75);
    filterLay->addWidget(m_ignoredDirsEdit);

    behLay->addWidget(filterGroup);
    behLay->addStretch();

    tabs->addTab(behPage, "Behavior && Summary Rules");

    // =========================================================================
    // Вкладка 3: Prompts
    // =========================================================================
    auto* promptsPage = new QWidget(tabs);
    auto* promptsLay = new QVBoxLayout(promptsPage);

    auto* promptsGroup = new QGroupBox("AI Menu && Summary Prompt Templates", promptsPage);
    auto* promptsGroupLay = new QVBoxLayout(promptsGroup);

    auto* actionRow = new QHBoxLayout();
    actionRow->addWidget(new QLabel("AI Menu Action:", promptsGroup));
    m_promptActionCombo = new QComboBox(promptsGroup);
    for (const auto& meta : kPromptActions) {
        m_promptActionCombo->addItem(QString::fromLatin1(meta.menuLabel), QString::fromLatin1(meta.key));
    }
    actionRow->addWidget(m_promptActionCombo, 1);
    promptsGroupLay->addLayout(actionRow);

    m_promptHintLabel = new QLabel(promptsGroup);
    m_promptHintLabel->setStyleSheet("color: #888899; font-size: 11px;");
    m_promptHintLabel->setWordWrap(true);
    promptsGroupLay->addWidget(m_promptHintLabel);

    promptsGroupLay->addWidget(new QLabel("Prompt Instruction Sent to LLM (target code or digest is appended after this prompt):", promptsGroup));
    m_promptEdit = new QPlainTextEdit(promptsGroup);
    m_promptEdit->setPlaceholderText("Enter prompt template for the selected AI action...");
    promptsGroupLay->addWidget(m_promptEdit, 1);

    auto* promptBtnRow = new QHBoxLayout();
    m_resetCurrentPromptBtn = new QPushButton("Reset Selected to Default", promptsGroup);
    m_resetAllPromptsBtn = new QPushButton("Reset All Prompts to Defaults", promptsGroup);
    promptBtnRow->addWidget(m_resetCurrentPromptBtn);
    promptBtnRow->addStretch();
    promptBtnRow->addWidget(m_resetAllPromptsBtn);
    promptsGroupLay->addLayout(promptBtnRow);

    auto* historyGroup = new QGroupBox("Global Custom Prompt History", promptsPage);
    auto* historyLay = new QHBoxLayout(historyGroup);
    historyLay->addWidget(new QLabel("Maximum Prompts Stored in History:", historyGroup));
    m_promptHistoryLimitSpin = new QSpinBox(historyGroup);
    m_promptHistoryLimitSpin->setRange(5, 1000);
    m_promptHistoryLimitSpin->setSingleStep(10);
    m_promptHistoryLimitSpin->setValue(behCfg.promptHistoryLimit);
    historyLay->addWidget(m_promptHistoryLimitSpin);
    historyLay->addStretch();
    promptsLay->addWidget(historyGroup);

    promptsLay->addWidget(promptsGroup, 1);
    tabs->addTab(promptsPage, "Prompts");

    mainLay->addWidget(tabs, 1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLay->addWidget(btns);

    m_updatingProfileUi = true;
    QString activeName = cfgMgr.activeAiProfileName();
    int activeIdx = 0;
    for (int i = 0; i < m_profiles.size(); ++i) {
        m_profileCombo->addItem(m_profiles[i].profileName);
        if (m_profiles[i].profileName == activeName) activeIdx = i;
    }
    m_profileCombo->setCurrentIndex(activeIdx);
    m_currentProfileIndex = activeIdx;
    loadProfileIntoUi(m_profiles[activeIdx]);
    m_deleteProfileBtn->setEnabled(m_profiles.size() > 1);
    m_updatingProfileUi = false;

    m_currentPromptIndex = 0;
    m_promptActionCombo->setCurrentIndex(0);
    loadPromptIntoUi(0);

    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &AiPreferencesDialog::onProfileSelectionChanged);
    connect(m_newProfileBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onNewProfileClicked);
    connect(m_deleteProfileBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onDeleteProfileClicked);
    connect(m_resetProfilesBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onResetDefaultProfilesClicked);
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, &AiPreferencesDialog::onBackendChanged);
    connect(m_fetchBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onFetchModelsClicked);
    connect(syncSyntaxExtBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onSyncSyntaxExtensionsClicked);

    connect(m_useLocalExecCheck, &QCheckBox::toggled, this, &AiPreferencesDialog::onUseLocalExecToggled);
    connect(m_browseLocalExecBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onBrowseLocalExecClicked);
    connect(m_browseLocalModelBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onBrowseLocalModelClicked);
    connect(m_startLocalExecBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onStartLocalExecClicked);
    connect(m_stopLocalExecBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onStopLocalExecClicked);
    connect(m_restartLocalExecBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onRestartLocalExecClicked);
    connect(m_localExecPathEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        updateLocalExecProcessUi();
    });
    connect(m_localExecArgsEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        updateLocalExecProcessUi();
    });

    auto& procMgr = LocalLlmProcessManager::instance();
    connect(&procMgr, &LocalLlmProcessManager::processStateChanged, this, &AiPreferencesDialog::updateLocalExecProcessUi);
    connect(&procMgr, &LocalLlmProcessManager::processFailure, this, [this](const QString& errText) {
        updateLocalExecProcessUi();
        if (isVisible()) {
            QMessageBox::warning(this, "Local LLM Process Error", errText);
        }
    });

    m_processPollTimer.setInterval(1200);
    connect(&m_processPollTimer, &QTimer::timeout, this, &AiPreferencesDialog::updateLocalExecProcessUi);
    m_processPollTimer.start();

    connect(m_promptActionCombo, &QComboBox::currentIndexChanged, this, &AiPreferencesDialog::onPromptSelectionChanged);
    connect(m_resetCurrentPromptBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onResetCurrentPromptClicked);
    connect(m_resetAllPromptsBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onResetAllPromptsClicked);

    connect(btns, &QDialogButtonBox::accepted, this, [this]() {
        saveUiIntoCurrentProfile();
        saveUiIntoCurrentPrompt();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_provider, &AiProvider::modelsFetched, this, [this](const QStringList& models) {
        m_fetchBtn->setEnabled(true);
        if (models.isEmpty()) {
            m_statusLabel->setStyleSheet("color: #ff5555;");
            m_statusLabel->setText("No models returned or connection failed.");
            return;
        }
        QString current = m_modelCombo->currentText();
        m_modelCombo->clear();
        m_modelCombo->addItems(models);
        if (models.contains(current)) m_modelCombo->setCurrentText(current);
        m_statusLabel->setStyleSheet("color: #50fa7b;");
        m_statusLabel->setText(QString("Connected! Found %1 model(s).").arg(models.size()));
    });

    updateLocalExecProcessUi();
}

void AiPreferencesDialog::loadProfileIntoUi(const AiConnectionConfig& cfg) {
    m_updatingProfileUi = true;
    m_backendCombo->setCurrentIndex(static_cast<int>(cfg.backend));
    m_endpointEdit->setText(cfg.endpointUrl);
    m_apiKeyEdit->setText(cfg.apiKey);
    m_modelCombo->setCurrentText(cfg.modelName);
    m_tempSpin->setValue(cfg.temperature);
    m_topPSpin->setValue(cfg.topP);
    m_maxCodeTokensSpin->setValue(cfg.maxOutputCodeTokens);
    m_maxSummaryTokensSpin->setValue(cfg.maxOutputSummaryTokens);
    m_ctxWindowSpin->setValue(cfg.contextWindow);
    m_timeoutSpin->setValue(cfg.timeoutSec);
    m_summaryIntervalSpin->setValue(cfg.projectSummaryIntervalMin);

    m_useLocalExecCheck->setChecked(cfg.useLocalExecutable);
    m_localExecGroup->setVisible(cfg.useLocalExecutable);
    m_localExecPathEdit->setText(cfg.localExecutablePath);
    m_localExecArgsEdit->setText(cfg.localCommandLineArgs);

    m_statusLabel->clear();
    m_updatingProfileUi = false;
    updateLocalExecProcessUi();
}

void AiPreferencesDialog::saveUiIntoCurrentProfile() {
    if (m_currentProfileIndex < 0 || m_currentProfileIndex >= m_profiles.size()) return;
    AiConnectionConfig& dst = m_profiles[m_currentProfileIndex];
    dst.backend = static_cast<AiBackendType>(m_backendCombo->currentIndex());
    dst.endpointUrl = m_endpointEdit->text().trimmed();
    dst.apiKey = m_apiKeyEdit->text().trimmed();
    dst.modelName = m_modelCombo->currentText().trimmed();
    dst.temperature = m_tempSpin->value();
    dst.topP = m_topPSpin->value();
    dst.maxOutputCodeTokens = m_maxCodeTokensSpin->value();
    dst.maxOutputSummaryTokens = m_maxSummaryTokensSpin->value();
    dst.contextWindow = m_ctxWindowSpin->value();
    dst.timeoutSec = m_timeoutSpin->value();
    dst.projectSummaryIntervalMin = m_summaryIntervalSpin->value();

    dst.useLocalExecutable = m_useLocalExecCheck->isChecked();
    dst.localExecutablePath = m_localExecPathEdit->text().trimmed();
    dst.localCommandLineArgs = m_localExecArgsEdit->text().trimmed();
}

void AiPreferencesDialog::onUseLocalExecToggled(bool checked) {
    m_localExecGroup->setVisible(checked);
    updateLocalExecProcessUi();
}

void AiPreferencesDialog::onBrowseLocalExecClicked() {
    QString currentExec = m_localExecPathEdit->text().trimmed();
    QString startDir = ConfigManager::instance().resolveInitialDialogDir(currentExec);
#ifdef Q_OS_WIN
    QString filter = "Executables (*.exe *.bat *.cmd);;All Files (*)";
#else
    QString filter = "All Files (*)";
#endif
    QString path = QFileDialog::getOpenFileName(this, "Select Local LLM Executable", startDir, filter);
    if (path.isEmpty()) return;
    ConfigManager::instance().setLastDialogDir(path);
    m_localExecPathEdit->setText(QDir::toNativeSeparators(path));
}

QString AiPreferencesDialog::substituteModelIntoArgs(const QString& currentArgs,
                                                     const QString& modelFilePath,
                                                     const QString& execPath) {
    const QString nativeModel = QDir::toNativeSeparators(modelFilePath.trimmed());
    const QString quotedModel = nativeModel.contains(' ')
                                    ? QString("\"%1\"").arg(nativeModel)
                                    : nativeModel;
    QString args = currentArgs.trimmed();

    static const QRegularExpression rxPlaceholder(R"(\{model\})", QRegularExpression::CaseInsensitiveOption);
    if (rxPlaceholder.match(args).hasMatch()) {
        args.replace(rxPlaceholder, quotedModel);
        return args;
    }

    static const QRegularExpression rxExistingFlag(
        R"(((?:^|\s)(?:-m|--model)(?:\s+|=))(?:"[^"]*"|'[^']*'|[^\s]+))");
    auto m = rxExistingFlag.match(args);
    if (m.hasMatch()) {
        QString replacement = m.captured(1) + quotedModel;
        args.replace(m.capturedStart(), m.capturedLength(), replacement);
        return args;
    }

    if (args.endsWith("-m") || args.endsWith("--model")) {
        return args + " " + quotedModel;
    }

    const QString exeName = QFileInfo(execPath).fileName().toLower();
    const QString flag = exeName.contains("kobold") ? QStringLiteral("--model") : QStringLiteral("-m");
    if (args.isEmpty()) {
        return QString("%1 %2").arg(flag, quotedModel);
    }

    return QString("%1 %2 %3").arg(args, flag, quotedModel);
}

void AiPreferencesDialog::onBrowseLocalModelClicked() {
    QString startDir = ConfigManager::instance().resolveInitialDialogDir(m_localExecPathEdit->text().trimmed());
    QString filter = "LLM Model Files (*.gguf *.bin *.safetensors *.ggml *.pt *.pth *.onnx *.kobold);;All Files (*)";
    QString modelPath = QFileDialog::getOpenFileName(
        this, "Select Local Model File to Substitute into Command Line", startDir, filter);
    if (modelPath.isEmpty()) return;

    ConfigManager::instance().setLastDialogDir(modelPath);
    QString updatedArgs = substituteModelIntoArgs(
        m_localExecArgsEdit->text(), modelPath, m_localExecPathEdit->text());
    m_localExecArgsEdit->setText(updatedArgs);
}

void AiPreferencesDialog::onStartLocalExecClicked() {
    const QString execPath = m_localExecPathEdit->text().trimmed();
    const QString cliArgs = m_localExecArgsEdit->text().trimmed();
    if (execPath.isEmpty()) {
        QMessageBox::warning(this, "Local LLM Executable", "Please select a local executable file first.");
        return;
    }

    auto& procMgr = LocalLlmProcessManager::instance();
    qint64 activePid = 0;
    bool isExt = false;
    if (procMgr.isExecutableRunning(execPath, &activePid, &isExt)) {
        QMessageBox::information(
            this,
            "Already Running",
            QString("The local executable is already running (PID: %1%2).\n"
                    "Use 'Restart' if you want to relaunch it with new command-line parameters.")
                .arg(activePid)
                .arg(isExt ? ", launched externally in OS" : ""));
        updateLocalExecProcessUi();
        return;
    }

    procMgr.startProcess(execPath, cliArgs);
}

void AiPreferencesDialog::onStopLocalExecClicked() {
    const QString execPath = m_localExecPathEdit->text().trimmed();
    LocalLlmProcessManager::instance().stopProcess(execPath);
}

void AiPreferencesDialog::onRestartLocalExecClicked() {
    const QString execPath = m_localExecPathEdit->text().trimmed();
    const QString cliArgs = m_localExecArgsEdit->text().trimmed();
    if (execPath.isEmpty()) {
        QMessageBox::warning(this, "Local LLM Executable", "Please select a local executable file first.");
        return;
    }
    LocalLlmProcessManager::instance().restartProcess(execPath, cliArgs);
}

void AiPreferencesDialog::updateLocalExecProcessUi() {
    if (!m_localExecGroup) return;

    auto& procMgr = LocalLlmProcessManager::instance();
    const auto tState = procMgr.transitionState();

    // Во время запуска/остановки/перезапуска показываем бегущий прогресс-бар (0, 0)
    if (tState != LocalLlmProcessManager::TransitionState::Idle) {
        m_localExecProgressBar->show();
        m_startLocalExecBtn->setEnabled(false);
        m_stopLocalExecBtn->setEnabled(false);
        m_restartLocalExecBtn->setEnabled(false);
        m_localExecStatusLabel->setStyleSheet("color: #bd93f9; font-weight: bold;");

        if (tState == LocalLlmProcessManager::TransitionState::Starting) {
            m_localExecStatusLabel->setText("Starting local LLM executable...");
        } else if (tState == LocalLlmProcessManager::TransitionState::Stopping) {
            m_localExecStatusLabel->setText("Stopping local LLM executable...");
        } else {
            m_localExecStatusLabel->setText("Restarting local LLM executable...");
        }
        return;
    }

    m_localExecProgressBar->hide();

    const QString execPath = m_localExecPathEdit->text().trimmed();
    const QString cliArgs = m_localExecArgsEdit->text().trimmed();
    const QString normExec = LocalLlmProcessManager::normalizeExecPath(execPath);
    const bool execExists = !normExec.isEmpty() && QFileInfo::exists(normExec) && QFileInfo(normExec).isFile();

    qint64 pid = 0;
    bool isExternal = false;
    const bool running = procMgr.isExecutableRunning(execPath, &pid, &isExternal);
    const bool configChanged = procMgr.isRunningConfigModified(execPath, cliArgs);

    m_startLocalExecBtn->setEnabled(!running && execExists);
    m_stopLocalExecBtn->setEnabled(running);
    m_restartLocalExecBtn->setEnabled(running && execExists);

    if (running) {
        if (configChanged) {
            m_restartLocalExecBtn->setText("Restart (Apply Changes)");
            m_localExecStatusLabel->setStyleSheet("color: #ffb86c; font-weight: bold;");
            m_localExecStatusLabel->setText(
                QString("Running (PID: %1) — Executable or CLI args modified. Click 'Restart' to apply.")
                    .arg(pid));
        } else if (isExternal) {
            m_restartLocalExecBtn->setText("Restart");
            m_localExecStatusLabel->setStyleSheet("color: #8be9fd; font-weight: bold;");
            m_localExecStatusLabel->setText(
                QString("Already running in OS (PID: %1). Click 'Restart' to relaunch with current CLI args.")
                    .arg(pid));
        } else {
            m_restartLocalExecBtn->setText("Restart");
            m_localExecStatusLabel->setStyleSheet("color: #50fa7b; font-weight: bold;");
            m_localExecStatusLabel->setText(QString("Running (PID: %1)").arg(pid));
        }
    } else {
        m_restartLocalExecBtn->setText("Restart");
        if (execPath.isEmpty()) {
            m_localExecStatusLabel->setStyleSheet("color: #888899;");
            m_localExecStatusLabel->setText("Status: Stopped (select an executable file)");
        } else if (!execExists) {
            m_localExecStatusLabel->setStyleSheet("color: #ff5555; font-weight: bold;");
            m_localExecStatusLabel->setText("Status: Executable file not found on disk");
        } else {
            m_localExecStatusLabel->setStyleSheet("color: #888899;");
            m_localExecStatusLabel->setText("Status: Stopped (Ready to start)");
        }
    }
}

void AiPreferencesDialog::loadPromptIntoUi(int index) {
    if (index < 0 || index >= m_promptActionCombo->count()) return;
    m_updatingPromptUi = true;
    QString key = m_promptActionCombo->itemData(index).toString();
    QString text = m_promptTemplates[key].toString();
    if (text.isEmpty()) text = ConfigManager::defaultPromptTemplate(key);
    m_promptEdit->setPlainText(text);
    if (index < static_cast<int>(std::size(kPromptActions))) {
        m_promptHintLabel->setText(QString::fromLatin1(kPromptActions[index].hint));
    }
    m_updatingPromptUi = false;
}

void AiPreferencesDialog::saveUiIntoCurrentPrompt() {
    if (m_currentPromptIndex < 0 || m_currentPromptIndex >= m_promptActionCombo->count()) return;
    QString key = m_promptActionCombo->itemData(m_currentPromptIndex).toString();
    QString text = m_promptEdit->toPlainText().trimmed();
    m_promptTemplates[key] = text.isEmpty() ? ConfigManager::defaultPromptTemplate(key) : text;
}

void AiPreferencesDialog::onPromptSelectionChanged(int index) {
    if (m_updatingPromptUi || index < 0) return;
    saveUiIntoCurrentPrompt();
    m_currentPromptIndex = index;
    loadPromptIntoUi(index);
}

void AiPreferencesDialog::onResetCurrentPromptClicked() {
    if (m_currentPromptIndex < 0 || m_currentPromptIndex >= m_promptActionCombo->count()) return;
    QString key = m_promptActionCombo->itemData(m_currentPromptIndex).toString();
    QString def = ConfigManager::defaultPromptTemplate(key);
    m_promptTemplates[key] = def;
    m_updatingPromptUi = true;
    m_promptEdit->setPlainText(def);
    m_updatingPromptUi = false;
}

void AiPreferencesDialog::onResetAllPromptsClicked() {
    m_promptTemplates = ConfigManager::defaultPromptTemplates();
    loadPromptIntoUi(m_currentPromptIndex);
}

void AiPreferencesDialog::onProfileSelectionChanged(int index) {
    if (m_updatingProfileUi || index < 0 || index >= m_profiles.size()) return;
    saveUiIntoCurrentProfile();
    m_currentProfileIndex = index;
    loadProfileIntoUi(m_profiles[index]);

    // Если включён автозапуск — закрываем прошлый процесс и запускаем процесс выбранного профиля
    if (m_autoStartLocalExecCheck && m_autoStartLocalExecCheck->isChecked()) {
        LocalLlmProcessManager::instance().syncWithProfile(m_profiles[index], true);
    }
}

void AiPreferencesDialog::onNewProfileClicked() {
    saveUiIntoCurrentProfile();
    bool ok = false;
    QString name = QInputDialog::getText(this, "New AI Profile", "Profile name:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    for (const auto& p : m_profiles) {
        if (p.profileName.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, "Duplicate Profile", "A profile with this name already exists.");
            return;
        }
    }

    AiConnectionConfig newProf = resultConfig();
    newProf.profileName = name;
    m_profiles.append(newProf);

    m_updatingProfileUi = true;
    m_profileCombo->addItem(name);
    int newIdx = m_profiles.size() - 1;
    m_profileCombo->setCurrentIndex(newIdx);
    m_currentProfileIndex = newIdx;
    m_deleteProfileBtn->setEnabled(m_profiles.size() > 1);
    m_updatingProfileUi = false;
}

void AiPreferencesDialog::onDeleteProfileClicked() {
    if (m_profiles.size() <= 1 || m_currentProfileIndex < 0) return;
    m_updatingProfileUi = true;
    m_profiles.removeAt(m_currentProfileIndex);
    m_profileCombo->removeItem(m_currentProfileIndex);
    m_currentProfileIndex = qBound(0, m_currentProfileIndex, static_cast<int>(m_profiles.size()) - 1);
    m_profileCombo->setCurrentIndex(m_currentProfileIndex);
    loadProfileIntoUi(m_profiles[m_currentProfileIndex]);
    m_deleteProfileBtn->setEnabled(m_profiles.size() > 1);
    m_updatingProfileUi = false;
}

void AiPreferencesDialog::onResetDefaultProfilesClicked() {
    m_updatingProfileUi = true;
    m_profiles = ConfigManager::defaultAiProfiles();
    m_profileCombo->clear();
    for (const auto& p : m_profiles) m_profileCombo->addItem(p.profileName);
    m_currentProfileIndex = 0;
    m_profileCombo->setCurrentIndex(0);
    loadProfileIntoUi(m_profiles[0]);
    m_deleteProfileBtn->setEnabled(m_profiles.size() > 1);
    m_updatingProfileUi = false;
}

void AiPreferencesDialog::onBackendChanged(int index) {
    if (m_updatingProfileUi) return;
    auto b = static_cast<AiBackendType>(index);
    QString url = m_endpointEdit->text().trimmed();
    if (b == AiBackendType::Ollama && (url.isEmpty() || url.contains(":5001")))
        m_endpointEdit->setText("http://127.0.0.1:11434");
    else if (b == AiBackendType::KoboldCpp && (url.isEmpty() || url.contains(":11434")))
        m_endpointEdit->setText("http://localhost:5001");
    else if (b == AiBackendType::OpenAiCompatible && (url.isEmpty() || url.contains(":11434") || url == "http://127.0.0.1:5001" || url == "http://localhost:5001"))
        m_endpointEdit->setText("http://localhost:5001/v1/");
}

void AiPreferencesDialog::onFetchModelsClicked() {
    m_fetchBtn->setEnabled(false);
    m_statusLabel->setStyleSheet("color: #f1fa8c;");
    m_statusLabel->setText("Connecting to endpoint...");
    m_provider->fetchAvailableModels(resultConfig());
}

void AiPreferencesDialog::onSyncSyntaxExtensionsClicked() {
    QStringList current = splitCommaSeparated(m_allowedExtensionsEdit->toPlainText());
    for (QString& e : current) {
        if (e.startsWith('.')) e.remove(0, 1);
        e = e.toLower();
    }
    const QStringList synExts = ConfigManager::instance().extensionsFromLoadedSyntaxes();
    for (const QString& ext : synExts) {
        if (!current.contains(ext, Qt::CaseInsensitive)) {
            current.append(ext);
        }
    }
    m_allowedExtensionsEdit->setPlainText(current.join(", "));
}

QStringList AiPreferencesDialog::splitCommaSeparated(const QString& text) {
    QString normalized = text;
    normalized.replace(';', ',');
    normalized.replace('\n', ',');
    QStringList out;
    for (QString part : normalized.split(',', Qt::SkipEmptyParts)) {
        part = part.trimmed();
        if (!part.isEmpty() && !out.contains(part, Qt::CaseInsensitive)) {
            out.append(part);
        }
    }
    return out;
}

QString AiPreferencesDialog::resultActiveProfileName() const {
    return m_profileCombo->currentText().trimmed();
}

AiConnectionConfig AiPreferencesDialog::resultConfig() const {
    return {
        m_profileCombo->currentText().trimmed(),
        static_cast<AiBackendType>(m_backendCombo->currentIndex()),
        m_endpointEdit->text().trimmed(),
        m_apiKeyEdit->text().trimmed(),
        m_modelCombo->currentText().trimmed(),
        m_tempSpin->value(),
        m_topPSpin->value(),
        m_maxCodeTokensSpin->value(),
        m_maxSummaryTokensSpin->value(),
        m_ctxWindowSpin->value(),
        m_timeoutSpin->value(),
        m_summaryIntervalSpin->value(),
        m_useLocalExecCheck->isChecked(),
        m_localExecPathEdit->text().trimmed(),
        m_localExecArgsEdit->text().trimmed()
    };
}

AiBehaviorConfig AiPreferencesDialog::resultBehavior() const {
    AiBehaviorConfig b;
    b.responseLocale = m_localeCombo ? m_localeCombo->currentData().toString().trimmed() : QStringLiteral("default");
    if (b.responseLocale.isEmpty()) b.responseLocale = QStringLiteral("default");
    b.autoSummaryMode = static_cast<AutoSummaryMode>(m_autoSummaryModeCombo->currentData().toInt());
    b.includeDocumentSummaryInPrompt = m_useDocSummaryCheck->isChecked();
    b.includeProjectSummaryInPrompt = m_useProjSummaryCheck->isChecked();
    b.summaryUseSyntaxExtensions = m_useSyntaxExtensionsCheck->isChecked();
    b.summaryAllowedFilenames = splitCommaSeparated(m_allowedFilenamesEdit->text());
    for (QString ext : splitCommaSeparated(m_allowedExtensionsEdit->toPlainText())) {
        if (ext.startsWith("*.")) ext = ext.mid(2);
        else if (ext.startsWith('.')) ext = ext.mid(1);
        ext = ext.toLower().trimmed();
        if (!ext.isEmpty() && !b.summaryAllowedExtensions.contains(ext))
            b.summaryAllowedExtensions.append(ext);
    }
    b.summaryIgnoredDirs = splitCommaSeparated(m_ignoredDirsEdit->toPlainText());
    b.promptHistoryLimit = m_promptHistoryLimitSpin ? m_promptHistoryLimitSpin->value() : 50;
    b.projectCustomPromptContextMode = static_cast<ProjectCustomPromptContextMode>(
        m_projCustomPromptCtxCombo
            ? m_projCustomPromptCtxCombo->currentData().toInt()
            : static_cast<int>(ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown));
    b.autoStartLocalExecutable = m_autoStartLocalExecCheck && m_autoStartLocalExecCheck->isChecked();
    return b;
}
