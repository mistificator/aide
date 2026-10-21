// src/core/ConfigManager.h
#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>

enum class AiBackendType { Ollama, KoboldCpp, OpenAiCompatible };

struct AiConnectionConfig {
    AiBackendType backend = AiBackendType::Ollama;
    QString endpointUrl = "http://127.0.0.1:11434";
    QString apiKey, modelName = "qwen2.5-coder:7b";
    double temperature = 0.2, topP = 0.9;
    int maxOutputTokens = 2048, contextWindow = 8192, timeoutSec = 120, projectSummaryIntervalMin = 5;
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

    QString promptTemplate(const QString& actionKey) const;
    QString configDirPath() const;
    QString syntaxDirPath() const { return configDirPath() + "/syntax"; }
    QString themesDirPath() const { return configDirPath() + "/themes"; }

signals:
    void aiConfigChanged(const AiConnectionConfig& newConfig);
    void themeChanged(const QString& themeName);

private:
    explicit ConfigManager(QObject* parent = nullptr);
    void initDefaultPrompts();

    AiConnectionConfig m_aiConfig;
    QString m_activeTheme = "dracula";
    QJsonObject m_promptTemplates;
};