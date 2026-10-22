// src/core/ConfigManager.h
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>

enum class AiBackendType { Ollama, KoboldCpp, OpenAiCompatible };

struct AiConnectionConfig {
    AiBackendType backend = AiBackendType::Ollama;
    QString endpointUrl = "http://127.0.0.1:11434";
    QString apiKey, modelName = "qwen2.5-coder:7b";
    double temperature = 0.2, topP = 0.9;
    int maxOutputTokens = 2048, contextWindow = 8192, timeoutSec = 120, projectSummaryIntervalMin = 5;
};

struct FileTypeRule {
    QString langId;       // Например: "cpp", "python", "rust"
    QString displayName;  // Например: "C++", "Python", "Rust"
    QString syntaxFile;   // Имя файла в ~/.aide/syntax/, например "cpp.xml"
    QStringList extensions; // Например: {"cpp", "cxx", "cc", "h", "hpp"}
};

class ConfigManager : public QObject {
    Q_OBJECT
public:
    static ConfigManager& instance();

    bool initializeStorage();
    void loadConfig();
    void saveConfig();

    AiConnectionConfig aiConfig() const { return m_aiConfig; }
    void setAiConfig(const AiConnectionConfig& cfg);

    QString activeTheme() const { return m_activeTheme; }
    void setActiveTheme(const QString& themeName);

    // Recent & Last Project
    QString lastProjectPath() const { return m_lastProjectPath; }
    void setLastProjectPath(const QString& path);
    QStringList recentProjects() const { return m_recentProjects; }
    void addRecentProject(const QString& path);
    void clearRecentProjects();

    // Last Custom Prompt
    QString lastCustomPrompt() const { return m_lastCustomPrompt; }
    void setLastCustomPrompt(const QString& prompt);

    // Dynamic Syntax & File Types
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
    void themeChanged(const QString& themeName);
    void fileTypesChanged();
    void recentProjectsChanged();

private:
    explicit ConfigManager(QObject* parent = nullptr);
    void initDefaultPrompts();
    void initDefaultFileTypes();

    AiConnectionConfig m_aiConfig;
    QString m_activeTheme = "dracula";
    QString m_lastProjectPath;
    QStringList m_recentProjects;
    QString m_lastCustomPrompt;
    QString m_syntaxBaseUrl = "https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/";
    QVector<FileTypeRule> m_fileTypes;
    QJsonObject m_promptTemplates;
};
