// src/core/ConfigManager.cpp
#include "ConfigManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>

ConfigManager::ConfigManager(QObject* parent) : QObject(parent) { initDefaultPrompts(); }

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

QString ConfigManager::configDirPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.aide";
}

bool ConfigManager::initializeStorage() {
    QDir dir(configDirPath());
    bool ok = dir.mkpath(".") && dir.mkpath("syntax") && dir.mkpath("themes");
    if (!QFile::exists(dir.filePath("config.json"))) saveConfig();
    if (!QFile::exists(dir.filePath("prompts.json"))) {
        QSaveFile f(dir.filePath("prompts.json"));
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(m_promptTemplates).toJson()); f.commit(); }
    }
    return ok;
}

void ConfigManager::initDefaultPrompts() {
    m_promptTemplates = {
        {"ExplainCode", "Explain the logic, architecture, and potential pitfalls of the following code clearly and concisely:"},
        {"RefactorCode", "Refactor the following code to improve readability, structure, and idiomatic style. Output ONLY raw code without markdown fences:"},
        {"FindBugs", "Find potential bugs, memory leaks, edge cases, or vulnerabilities in the following code and fix them. Output ONLY raw code without markdown fences:"},
        {"OptimizePerformance", "Optimize the algorithmic complexity and memory usage of the following code. Output ONLY raw code without markdown fences:"},
        {"AddDocumentation", "Add comprehensive documentation comments (Doxygen/Docstrings) to the following code. Output ONLY raw code without markdown fences:"},
        {"GenerateUnitTests", "Generate unit tests covering main scenarios and edge cases for the following code. Output ONLY raw code without markdown fences:"},
        {"CompleteCode", "Complete the implementation of the following code based on its context and signatures. Output ONLY raw code without markdown fences:"},
        {"FileSummary", "Analyze this source file and output a compact structured technical summary: 1) Purpose, 2) Exported classes/structs/functions with signatures, 3) Key dependencies."},
        {"ProjectSummary", "Synthesize a concise architectural summary of the entire project based on the following individual file summaries:"}
    };
}

void ConfigManager::loadConfig() {
    QFile cfgFile(configDirPath() + "/config.json");
    if (cfgFile.open(QIODevice::ReadOnly)) {
        auto obj = QJsonDocument::fromJson(cfgFile.readAll()).object();
        m_activeTheme = obj["activeTheme"].toString("dracula");
        auto ai = obj["ai"].toObject();
        m_aiConfig.backend = static_cast<AiBackendType>(ai["backend"].toInt(0));
        m_aiConfig.endpointUrl = ai["endpointUrl"].toString("http://127.0.0.1:11434");
        m_aiConfig.apiKey = ai["apiKey"].toString();
        m_aiConfig.modelName = ai["modelName"].toString("qwen2.5-coder:7b");
        m_aiConfig.temperature = ai["temperature"].toDouble(0.2);
        m_aiConfig.topP = ai["topP"].toDouble(0.9);
        m_aiConfig.maxOutputTokens = ai["maxOutputTokens"].toInt(2048);
        m_aiConfig.contextWindow = ai["contextWindow"].toInt(8192);
        m_aiConfig.timeoutSec = ai["timeoutSec"].toInt(120);
        m_aiConfig.projectSummaryIntervalMin = ai["projectSummaryIntervalMin"].toInt(5);
    }
    QFile prFile(configDirPath() + "/prompts.json");
    if (prFile.open(QIODevice::ReadOnly)) {
        auto loaded = QJsonDocument::fromJson(prFile.readAll()).object();
        for (auto it = loaded.begin(); it != loaded.end(); ++it) m_promptTemplates[it.key()] = it.value();
    }
}

void ConfigManager::saveConfig() {
    QJsonObject ai {
        {"backend", static_cast<int>(m_aiConfig.backend)},
        {"endpointUrl", m_aiConfig.endpointUrl},
        {"apiKey", m_aiConfig.apiKey},
        {"modelName", m_aiConfig.modelName},
        {"temperature", m_aiConfig.temperature},
        {"topP", m_aiConfig.topP},
        {"maxOutputTokens", m_aiConfig.maxOutputTokens},
        {"contextWindow", m_aiConfig.contextWindow},
        {"timeoutSec", m_aiConfig.timeoutSec},
        {"projectSummaryIntervalMin", m_aiConfig.projectSummaryIntervalMin}
    };
    QJsonObject root {{"activeTheme", m_activeTheme}, {"ai", ai}};
    QSaveFile f(configDirPath() + "/config.json");
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(root).toJson()); f.commit(); }
}

void ConfigManager::setAiConfig(const AiConnectionConfig& cfg) {
    m_aiConfig = cfg;
    saveConfig();
    emit aiConfigChanged(m_aiConfig);
}

void ConfigManager::setActiveTheme(const QString& themeName) {
    if (m_activeTheme == themeName) return;
    m_activeTheme = themeName;
    saveConfig();
    emit themeChanged(m_activeTheme);
}

QString ConfigManager::promptTemplate(const QString& actionKey) const {
    return m_promptTemplates[actionKey].toString();
}