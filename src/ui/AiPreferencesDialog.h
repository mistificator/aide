// src/ui/AiPreferencesDialog.h
#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include "../core/ConfigManager.h"

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QGroupBox;
class QProgressBar;
class QPlainTextEdit;
class AiProvider;

class LocalLlmProcessManager : public QObject {
    Q_OBJECT
public:
    enum class TransitionState {
        Idle,
        Starting,
        Stopping,
        Restarting
    };

    static LocalLlmProcessManager& instance();

    TransitionState transitionState() const { return m_transitionState; }
    bool isTransitioning() const { return m_transitionState != TransitionState::Idle; }
    bool isManagedRunning() const;
    qint64 managedPid() const;
    QString launchedExecutablePath() const { return m_launchedExecPath; }
    QString launchedCommandLineArgs() const { return m_launchedCliArgs; }
    QString lastErrorText() const { return m_lastError; }

    bool isExecutableRunning(const QString& execPath, qint64* outPid = nullptr, bool* outIsExternal = nullptr) const;
    bool isRunningConfigModified(const QString& currentExecPath, const QString& currentCliArgs) const;

    void startProcess(const QString& execPath, const QString& cliArgs);
    void stopProcess(const QString& execPath = {});
    void restartProcess(const QString& execPath, const QString& cliArgs);

    // Автоматический запуск/перезапуск/остановка при старте AIDE или переключении профиля
    void syncWithProfile(const AiConnectionConfig& cfg, bool autoStartEnabled);

    static qint64 findExternalProcessPid(const QString& execPath, qint64 excludePid = 0);
    static bool terminateExternalProcessPid(qint64 pid);
    static QString normalizeExecPath(const QString& path);

signals:
    void processStateChanged();
    void processFailure(const QString& errorText);
    void debugLog(const QString& category, const QString& summary, const QString& payload);

private:
    explicit LocalLlmProcessManager(QObject* parent = nullptr);
    ~LocalLlmProcessManager() override;

    void beginStartInternal(const QString& execPath, const QString& cliArgs);
    void finishTransitionToIdle();
    void shutdown();

    QProcess* m_process = nullptr;
    QTimer* m_killFallbackTimer = nullptr;
    TransitionState m_transitionState = TransitionState::Idle;
    QString m_launchedExecPath;
    QString m_launchedCliArgs;
    QString m_pendingExecPath;
    QString m_pendingCliArgs;
    QString m_lastError;
};

class AiPreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit AiPreferencesDialog(AiProvider* provider, QWidget* parent = nullptr);

    AiConnectionConfig resultConfig() const;
    QVector<AiConnectionConfig> resultProfiles() const { return m_profiles; }
    QString resultActiveProfileName() const;
    AiBehaviorConfig resultBehavior() const;
    QJsonObject resultPrompts() const { return m_promptTemplates; }

private slots:
    void onProfileSelectionChanged(int index);
    void onNewProfileClicked();
    void onDeleteProfileClicked();
    void onResetDefaultProfilesClicked();
    void onBackendChanged(int index);
    void onFetchModelsClicked();
    void onSyncSyntaxExtensionsClicked();

    void onUseLocalExecToggled(bool checked);
    void onBrowseLocalExecClicked();
    void onBrowseLocalModelClicked();
    void onStartLocalExecClicked();
    void onStopLocalExecClicked();
    void onRestartLocalExecClicked();
    void updateLocalExecProcessUi();

    void onPromptSelectionChanged(int index);
    void onResetCurrentPromptClicked();
    void onResetAllPromptsClicked();

private:
    void loadProfileIntoUi(const AiConnectionConfig& cfg);
    void saveUiIntoCurrentProfile();
    void loadPromptIntoUi(int index);
    void saveUiIntoCurrentPrompt();
    static QStringList splitCommaSeparated(const QString& text);
    static QString substituteModelIntoArgs(const QString& currentArgs,
                                           const QString& modelFilePath,
                                           const QString& execPath);

    AiProvider* m_provider = nullptr;
    QVector<AiConnectionConfig> m_profiles;
    int m_currentProfileIndex = -1;
    bool m_updatingProfileUi = false;

    // Вкладка 1: Connection & Profiles
    QComboBox* m_profileCombo = nullptr;
    QPushButton* m_newProfileBtn = nullptr;
    QPushButton* m_deleteProfileBtn = nullptr;
    QPushButton* m_resetProfilesBtn = nullptr;

    QComboBox* m_backendCombo = nullptr;
    QLineEdit* m_endpointEdit = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QPushButton* m_fetchBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Секция локального исполняемого файла нейросети
    QCheckBox* m_useLocalExecCheck = nullptr;
    QGroupBox* m_localExecGroup = nullptr;
    QCheckBox* m_autoStartLocalExecCheck = nullptr;
    QLineEdit* m_localExecPathEdit = nullptr;
    QPushButton* m_browseLocalExecBtn = nullptr;
    QLineEdit* m_localExecArgsEdit = nullptr;
    QPushButton* m_browseLocalModelBtn = nullptr;
    QPushButton* m_startLocalExecBtn = nullptr;
    QPushButton* m_stopLocalExecBtn = nullptr;
    QPushButton* m_restartLocalExecBtn = nullptr;
    QLabel* m_localExecStatusLabel = nullptr;
    QProgressBar* m_localExecProgressBar = nullptr;
    QTimer m_processPollTimer;

    QDoubleSpinBox* m_tempSpin = nullptr;
    QDoubleSpinBox* m_topPSpin = nullptr;
    QSpinBox* m_maxCodeTokensSpin = nullptr;
    QSpinBox* m_maxSummaryTokensSpin = nullptr;
    QSpinBox* m_ctxWindowSpin = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QSpinBox* m_summaryIntervalSpin = nullptr;

    // Вкладка 2: Behavior & Summary Rules
    QComboBox* m_localeCombo = nullptr;
    QComboBox* m_autoSummaryModeCombo = nullptr;
    QCheckBox* m_useDocSummaryCheck = nullptr;
    QCheckBox* m_useProjSummaryCheck = nullptr;
    QComboBox* m_projCustomPromptCtxCombo = nullptr;
    QCheckBox* m_useSyntaxExtensionsCheck = nullptr;
    QLineEdit* m_allowedFilenamesEdit = nullptr;
    QPlainTextEdit* m_allowedExtensionsEdit = nullptr;
    QPlainTextEdit* m_ignoredDirsEdit = nullptr;

    // Вкладка 3: Prompts
    QJsonObject m_promptTemplates;
    int m_currentPromptIndex = -1;
    bool m_updatingPromptUi = false;
    QComboBox* m_promptActionCombo = nullptr;
    QLabel* m_promptHintLabel = nullptr;
    QPlainTextEdit* m_promptEdit = nullptr;
    QPushButton* m_resetCurrentPromptBtn = nullptr;
    QPushButton* m_resetAllPromptsBtn = nullptr;
    QSpinBox* m_promptHistoryLimitSpin = nullptr;
};
