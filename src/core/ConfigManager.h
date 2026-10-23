// src/core/ConfigManager.h
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>

enum class AiBackendType { Ollama, KoboldCpp, OpenAiCompatible };

struct AiConnectionConfig {
    QString profileName = "Ollama (Offline)";
    AiBackendType backend = AiBackendType::Ollama;
    QString endpointUrl = "http://127.0.0.1:11434";
    QString apiKey;
    QString modelName = "qwen2.5-coder:7b";
    double temperature = 0.2;
    double topP = 0.9;
    int maxOutputCodeTokens = 2048;
    int maxOutputSummaryTokens = 256;
    int contextWindow = 8192;
    int timeoutSec = 120;
    int projectSummaryIntervalMin = 5;

    QString llmIdentifier() const {
        QString providerPrefix = (backend == AiBackendType::Ollama) ? "ollama"
                                 : (backend == AiBackendType::KoboldCpp) ? "koboldcpp"
                                                                         : "openai";
        QString model = modelName.trimmed().isEmpty() ? endpointUrl.trimmed() : modelName.trimmed();
        return providerPrefix + ":" + model;
    }
};

struct AiBehaviorConfig {
    bool includeDocumentSummaryInPrompt = true;
    bool includeProjectSummaryInPrompt = true;
    bool summaryUseSyntaxExtensions = true;
    QStringList summaryAllowedFilenames;
    QStringList summaryAllowedExtensions;
};

struct FileTypeRule {
    QString langId;
    QString displayName;
    QString syntaxFile;
    QStringList extensions;
};

class ConfigManager : public QObject {
    Q_OBJECT
public:
    static ConfigManager& instance();

    bool initializeStorage();
    void loadConfig();
    void saveConfig();

    // Профили подключения AI
    AiConnectionConfig aiConfig() const { return m_aiConfig; }
    void setAiConfig(const AiConnectionConfig& cfg);

    QVector<AiConnectionConfig> aiProfiles() const { return m_aiProfiles; }
    QString activeAiProfileName() const { return m_aiConfig.profileName; }
    void setAiProfiles(const QVector<AiConnectionConfig>& profiles, const QString& activeProfileName);
    static QVector<AiConnectionConfig> defaultAiProfiles();

    // Настройки поведения AI и фильтрации саммари
    AiBehaviorConfig aiBehavior() const { return m_aiBehavior; }
    void setAiBehavior(const AiBehaviorConfig& behavior);
    static AiBehaviorConfig defaultAiBehavior();

    QStringList extensionsFromLoadedSyntaxes() const;
    bool shouldGenerateSummaryForFile(const QString& filePath, QString* outSkipReasonMarkdown = nullptr) const;

    // Темы и пути
    QString activeTheme() const { return m_activeTheme; }
    void setActiveTheme(const QString& themeName);

    QString lastProjectPath() const { return m_lastProjectPath; }
    void setLastProjectPath(const QString& path);

    QString lastDialogDir() const { return m_lastDialogDir; }
    void setLastDialogDir(const QString& dirOrFilePath);
    QString resolveInitialDialogDir(const QString& preferredFileOrDir = {}) const;

    QStringList recentProjects() const { return m_recentProjects; }
    void addRecentProject(const QString& path);
    void clearRecentProjects();

    QString lastCustomPrompt() const { return m_lastCustomPrompt; }
    void setLastCustomPrompt(const QString& prompt);

    // Подсветка синтаксиса
    QString syntaxBaseUrl() const { return m_syntaxBaseUrl; }
    void setSyntaxBaseUrl(const QString& url);

    QVector<FileTypeRule> fileTypeRules() const { return m_fileTypes; }
    void setFileTypeRules(const QVector<FileTypeRule>& rules);
    void upsertFileTypeRule(const FileTypeRule& rule);

    QString languageIdForFile(const QString& filePath) const;
    FileTypeRule ruleForLanguage(const QString& langId) const;

    QString promptTemplate(const QString& actionKey) const;
    QString configDirPath() const;
    QString syntaxDirPath() const { return configDirPath() + "/syntax"; }
    QString themesDirPath() const { return configDirPath() + "/themes"; }

signals:
    void aiConfigChanged(const AiConnectionConfig& newConfig);
    void aiBehaviorChanged(const AiBehaviorConfig& newBehavior);
    void themeChanged(const QString& themeName);
    void fileTypesChanged();
    void recentProjectsChanged();

private:
    explicit ConfigManager(QObject* parent = nullptr);
    void initDefaultPrompts();
    void initDefaultFileTypes();
    static QJsonObject serializeAiConfig(const AiConnectionConfig& cfg);
    static AiConnectionConfig deserializeAiConfig(const QJsonObject& ai, const QString& fallbackName = "Default");

    AiConnectionConfig m_aiConfig;
    QVector<AiConnectionConfig> m_aiProfiles;
    AiBehaviorConfig m_aiBehavior;

    QString m_activeTheme = "dracula";
    QString m_lastProjectPath;
    QString m_lastDialogDir;
    QStringList m_recentProjects;
    QString m_lastCustomPrompt;
    QString m_syntaxBaseUrl = "https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/";
    QVector<FileTypeRule> m_fileTypes;
    QJsonObject m_promptTemplates;
};
