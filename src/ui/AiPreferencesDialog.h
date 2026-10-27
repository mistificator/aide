// src/ui/AiPreferencesDialog.h
#pragma once
#include <QDialog>
#include <QJsonObject>
#include "../core/ConfigManager.h"

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QPlainTextEdit;
class AiProvider;

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
    void onPromptSelectionChanged(int index);
    void onResetCurrentPromptClicked();
    void onResetAllPromptsClicked();

private:
    void loadProfileIntoUi(const AiConnectionConfig& cfg);
    void saveUiIntoCurrentProfile();
    void loadPromptIntoUi(int index);
    void saveUiIntoCurrentPrompt();
    static QStringList splitCommaSeparated(const QString& text);

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
    QDoubleSpinBox* m_tempSpin = nullptr;
    QDoubleSpinBox* m_topPSpin = nullptr;
    QSpinBox* m_maxCodeTokensSpin = nullptr;
    QSpinBox* m_maxSummaryTokensSpin = nullptr;
    QSpinBox* m_ctxWindowSpin = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QSpinBox* m_summaryIntervalSpin = nullptr;

    // Вкладка 2: Behavior & Summary Rules
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
