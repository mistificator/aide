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
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QLabel>

namespace {
struct PromptActionMeta {
    const char* key;
    const char* menuLabel;
    const char* hint;
};

constexpr PromptActionMeta kPromptActions[] = {
    {"ExplainCode",         "Explain Code",                             "Triggered by 'AI -> Explain Code'. Output is rendered as Markdown in a dialog."},
    {"RefactorCode",        "Refactor & Clean Up",                      "Triggered by 'AI -> Refactor & Clean Up'. Replaces selected or full code in editor."},
    {"FindBugs",            "Find Bugs & Edge Cases",                   "Triggered by 'AI -> Find Bugs & Edge Cases'. Replaces selected or full code in editor."},
    {"OptimizePerformance", "Optimize Performance",                     "Triggered by 'AI -> Optimize Performance'. Replaces selected or full code in editor."},
    {"AddDocumentation",    "Add Documentation / Comments",             "Triggered by 'AI -> Add Documentation / Comments'. Replaces selected or full code in editor."},
    {"GenerateUnitTests",   "Generate Unit Tests",                      "Triggered by 'AI -> Generate Unit Tests'. Replaces or appends unit test code in editor."},
    {"CompleteCode",        "Complete / Continue Code",                 "Triggered by 'AI -> Complete / Continue Code'. Completes implementation in editor."},
    {"CustomPrompt",        "Custom Prompt... (Document Instruction)",  "Appended after user's custom instruction in 'AI -> Custom Prompt...' for a document."},
    {"ProjectCustomPrompt", "Custom Prompt for Project... (Markdown)",  "Appended after user's custom instruction in 'AI -> Custom Prompt for Project...'. Requests Markdown output."},
    {"FileSummary",         "Rebuild File Summary",                     "Used when indexing a source file into Markdown summary (background or manual)."},
    {"ProjectSummary",      "Rebuild Project Summary",                  "Used when synthesizing the global project architecture summary from file summaries."}
};
} // namespace

AiPreferencesDialog::AiPreferencesDialog(AiProvider* provider, QWidget* parent)
    : QDialog(parent), m_provider(provider) {
    setWindowTitle("AI Preferences");
    resize(650, 640);

    auto& cfgMgr = ConfigManager::instance();
    m_profiles = cfgMgr.aiProfiles();
    if (m_profiles.isEmpty()) m_profiles = ConfigManager::defaultAiProfiles();
    m_promptTemplates = cfgMgr.promptTemplates();

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

    auto behCfg = cfgMgr.aiBehavior();

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

    auto* filterGroup = new QGroupBox("Automatic & Recursive File Summary Generation Rules", behPage);
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

    auto* promptsGroup = new QGroupBox("AI Menu & Summary Prompt Templates", promptsPage);
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
    m_statusLabel->clear();
    m_updatingProfileUi = false;
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
        m_summaryIntervalSpin->value()
    };
}

AiBehaviorConfig AiPreferencesDialog::resultBehavior() const {
    AiBehaviorConfig b;
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
    return b;
}
