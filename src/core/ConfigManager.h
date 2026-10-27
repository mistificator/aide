// src/core/ConfigManager.h
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QJsonObject>
#include <QVector>
#include <QDateTime>

enum class AiBackendType { Ollama, KoboldCpp, OpenAiCompatible };

enum class AutoSummaryMode {
    None = 0,
    FilesOnly = 1,
    FilesAndProject = 2
};

enum class ProjectCustomPromptContextMode {
    ProjectSummaryOnly = 0,             // 1. Только саммари проекта
    IndexedSourcesMarkdownOnly = 1,     // 2. Все проиндексированные файлы в одном внутреннем Markdown
    BothSummaryAndSourcesMarkdown = 2   // 3. И саммари проекта, и внутренний Markdown с исходниками
};

struct AiConnectionConfig {
    QString profileName = "Ollama (Offline)";
    AiBackendType backend = AiBackendType::Ollama;
    QString endpointUrl = "http://127.0.0.1:11434";
    QString apiKey;
    QString modelName = "qwen2.5-coder:7b";
    double temperature = 0.2;
    double topP = 0.9;
    int maxOutputCodeTokens = 65536;
    int maxOutputSummaryTokens = 1024;
    int contextWindow = 131072;
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
    AutoSummaryMode autoSummaryMode = AutoSummaryMode::FilesAndProject;
    bool includeDocumentSummaryInPrompt = true;
    bool includeProjectSummaryInPrompt = true;
    ProjectCustomPromptContextMode projectCustomPromptContextMode = ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown;
    bool summaryUseSyntaxExtensions = true;
    QStringList summaryAllowedFilenames;
    QStringList summaryAllowedExtensions;
    QStringList summaryIgnoredDirs;
    int promptHistoryLimit = 50;
};

struct PromptHistoryItem {
    QString text;
    QString sha256;
    bool pinned = false;
    QDateTime updatedAt;
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

    // Профили и конфигурация подключения AI
    AiConnectionConfig aiConfig() const { return m_aiConfig; }
    void setAiConfig(const AiConnectionConfig& cfg);

    QVector<AiConnectionConfig> aiProfiles() const { return m_aiProfiles; }
    QString activeAiProfileName() const { return m_aiConfig.profileName; }
    void setAiProfiles(const QVector<AiConnectionConfig>& profiles, const QString& activeProfileName);
    static QVector<AiConnectionConfig> defaultAiProfiles();

    // Настройки поведения AI и правил генерации саммари
    AiBehaviorConfig aiBehavior() const { return m_aiBehavior; }
    void setAiBehavior(const AiBehaviorConfig& behavior);
    static AiBehaviorConfig defaultAiBehavior();

    QStringList extensionsFromLoadedSyntaxes() const;
    QStringList supportedProjectFileNameFilters() const;
    bool shouldGenerateSummaryForFile(const QString& filePath, QString* outSkipReasonMarkdown = nullptr) const;
    bool isDirectoryIgnoredForSummary(const QString& dirNameOrPath) const;

    // Фильтр файлов в Project Explorer
    bool projectExplorerFilterSupported() const { return m_projectExplorerFilterSupported; }
    void setProjectExplorerFilterSupported(bool filterSupportedOnly);

    // Глобальная история кастомных промптов
    QVector<PromptHistoryItem> promptHistory() const { return m_promptHistory; }
    void setPromptHistory(const QVector<PromptHistoryItem>& items);
    void addPromptToHistory(const QString& promptText);
    int promptHistoryLimit() const { return m_aiBehavior.promptHistoryLimit; }
    void setPromptHistoryLimit(int limit);
    static QString computePromptHash(const QString& promptText);

    // Тема и пути
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

    QByteArray windowGeometry() const { return m_windowGeometry; }
    QByteArray windowState() const { return m_windowState; }
    void setWindowGeometryAndState(const QByteArray& geometry, const QByteArray& state);

    // Типы файлов и подсветка синтаксиса
    QString syntaxBaseUrl() const { return m_syntaxBaseUrl; }
    void setSyntaxBaseUrl(const QString& url);

    QVector<FileTypeRule> fileTypeRules() const { return m_fileTypes; }
    void setFileTypeRules(const QVector<FileTypeRule>& rules);
    void upsertFileTypeRule(const FileTypeRule& rule);
    bool hasFileTypeRule(const QString& langId) const;
    void repairFileTypeExtensions();
    static QStringList knownExtensionsForLanguage(const QString& langIdOrFile);

    QString languageIdForFile(const QString& filePath) const;
    FileTypeRule ruleForLanguage(const QString& langId) const;

    // Шаблоны промптов
    QString promptTemplate(const QString& actionKey) const;
    QJsonObject promptTemplates() const { return m_promptTemplates; }
    void setPromptTemplates(const QJsonObject& templates);
    static QJsonObject defaultPromptTemplates();
    static QString defaultPromptTemplate(const QString& actionKey);

    QString configDirPath() const;
    QString syntaxDirPath() const { return configDirPath() + "/syntax"; }
    QString themesDirPath() const { return configDirPath() + "/themes"; }

signals:
    void aiConfigChanged(const AiConnectionConfig& newConfig);
    void aiBehaviorChanged(const AiBehaviorConfig& newBehavior);
    void projectExplorerFilterChanged(bool filterSupportedOnly);
    void promptsChanged();
    void promptHistoryChanged();
    void themeChanged(const QString& themeName);
    void fileTypesChanged();
    void recentProjectsChanged();

private:
    explicit ConfigManager(QObject* parent = nullptr);
    void initDefaultPrompts();
    void initDefaultFileTypes();
    void savePrompts() const;
    void enforcePromptHistoryLimit();
    static QJsonObject serializeAiConfig(const AiConnectionConfig& cfg);
    static AiConnectionConfig deserializeAiConfig(const QJsonObject& ai, const QString& fallbackName = "Default");

    AiConnectionConfig m_aiConfig;
    QVector<AiConnectionConfig> m_aiProfiles;
    AiBehaviorConfig m_aiBehavior;
    QVector<PromptHistoryItem> m_promptHistory;

    QString m_activeTheme = "dracula";
    bool m_projectExplorerFilterSupported = false;
    QString m_lastProjectPath;
    QString m_lastDialogDir;
    QStringList m_recentProjects;
    QString m_lastCustomPrompt;
    QByteArray m_windowGeometry;
    QByteArray m_windowState;
    QString m_syntaxBaseUrl = "https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/";
    QVector<FileTypeRule> m_fileTypes;
    QJsonObject m_promptTemplates;
};
