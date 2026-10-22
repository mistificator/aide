// src/core/ConfigManager.cpp
#include "ConfigManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>

ConfigManager::ConfigManager(QObject* parent) : QObject(parent) {
    initDefaultPrompts();
    initDefaultFileTypes();
}

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
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(m_promptTemplates).toJson());
            f.commit();
        }
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
        {"FileSummary", "Analyze this source file and output a compact structured technical summary in Markdown format (use ### headings, bullet points, and `inline code`): 1) Purpose, 2) Exported classes/structs/functions with signatures, 3) Key dependencies."},
        {"ProjectSummary", "Synthesize a concise architectural summary of the entire project in Markdown format (use ### headings, bullet points, and `inline code`) based on the following individual file summaries:"}
    };
}

void ConfigManager::initDefaultFileTypes() {
    m_fileTypes = {
        {"cpp",        "C++",        "cpp.xml",        {"cpp", "cxx", "cc", "hpp", "hxx", "hh", "h"}},
        {"c",          "C",          "c.xml",          {"c"}},
        {"python",     "Python",     "python.xml",     {"py", "pyw", "pyi"}},
        {"bash",       "Bash",       "bash.xml",       {"sh", "bash", "zsh"}},
        {"sql",        "SQL",        "sql.xml",        {"sql", "ddl", "dml"}},
        {"javascript", "JavaScript", "javascript.xml", {"js", "mjs", "cjs", "jsx", "ts", "tsx"}}
    };
}

void ConfigManager::loadConfig() {
    QFile cfgFile(configDirPath() + "/config.json");
    if (cfgFile.open(QIODevice::ReadOnly)) {
        auto obj = QJsonDocument::fromJson(cfgFile.readAll()).object();
        m_activeTheme = obj["activeTheme"].toString("dracula");
        m_lastProjectPath = obj["lastProjectPath"].toString();
        m_lastCustomPrompt = obj["lastCustomPrompt"].toString();
        m_syntaxBaseUrl = obj["syntaxBaseUrl"].toString("https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/");

        m_recentProjects.clear();
        for (const auto& v : obj["recentProjects"].toArray()) {
            QString p = v.toString();
            if (!p.isEmpty() && QDir(p).exists() && !m_recentProjects.contains(p))
                m_recentProjects.append(p);
        }

        auto ai = obj["ai"].toObject();
        m_aiConfig.backend = static_cast<AiBackendType>(ai["backend"].toInt(0));
        m_aiConfig.endpointUrl = ai["endpointUrl"].toString("http://127.0.0.1:11434");
        m_aiConfig.apiKey = ai["apiKey"].toString();
        m_aiConfig.modelName = ai["modelName"].toString("qwen2.5-coder:7b");
        m_aiConfig.temperature = ai["temperature"].toDouble(0.2);
        m_aiConfig.topP = ai["topP"].toDouble(0.9);

        int legacyMaxTokens = ai["maxOutputTokens"].toInt(2048);
        m_aiConfig.maxOutputCodeTokens = ai["maxOutputCodeTokens"].toInt(legacyMaxTokens);
        m_aiConfig.maxOutputSummaryTokens = ai["maxOutputSummaryTokens"].toInt(1024);

        m_aiConfig.contextWindow = ai["contextWindow"].toInt(8192);
        m_aiConfig.timeoutSec = ai["timeoutSec"].toInt(120);
        m_aiConfig.projectSummaryIntervalMin = ai["projectSummaryIntervalMin"].toInt(5);

        if (obj.contains("fileTypes")) {
            QVector<FileTypeRule> loadedRules;
            for (const auto& item : obj["fileTypes"].toArray()) {
                auto ro = item.toObject();
                FileTypeRule r;
                r.langId = ro["langId"].toString();
                r.displayName = ro["displayName"].toString(r.langId);
                r.syntaxFile = ro["syntaxFile"].toString();
                for (const auto& ext : ro["extensions"].toArray())
                    r.extensions.append(ext.toString().toLower().trimmed());
                if (!r.langId.isEmpty()) loadedRules.append(r);
            }
            if (!loadedRules.isEmpty()) m_fileTypes = loadedRules;
        }
    }

    QFile prFile(configDirPath() + "/prompts.json");
    if (prFile.open(QIODevice::ReadOnly)) {
        auto loaded = QJsonDocument::fromJson(prFile.readAll()).object();
        for (auto it = loaded.begin(); it != loaded.end(); ++it)
            m_promptTemplates[it.key()] = it.value();
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
        {"maxOutputCodeTokens", m_aiConfig.maxOutputCodeTokens},
        {"maxOutputSummaryTokens", m_aiConfig.maxOutputSummaryTokens},
        {"contextWindow", m_aiConfig.contextWindow},
        {"timeoutSec", m_aiConfig.timeoutSec},
        {"projectSummaryIntervalMin", m_aiConfig.projectSummaryIntervalMin}
    };

    QJsonArray recentArr;
    for (const QString& p : m_recentProjects) recentArr.append(p);

    QJsonArray typesArr;
    for (const auto& r : m_fileTypes) {
        QJsonArray exts;
        for (const QString& e : r.extensions) exts.append(e);
        typesArr.append(QJsonObject{
            {"langId", r.langId},
            {"displayName", r.displayName},
            {"syntaxFile", r.syntaxFile},
            {"extensions", exts}
        });
    }

    QJsonObject root {
        {"activeTheme", m_activeTheme},
        {"lastProjectPath", m_lastProjectPath},
        {"recentProjects", recentArr},
        {"lastCustomPrompt", m_lastCustomPrompt},
        {"syntaxBaseUrl", m_syntaxBaseUrl},
        {"fileTypes", typesArr},
        {"ai", ai}
    };

    QSaveFile f(configDirPath() + "/config.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
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

void ConfigManager::setLastProjectPath(const QString& path) {
    m_lastProjectPath = QDir::cleanPath(path);
    if (path.isEmpty()) m_lastProjectPath.clear();
    saveConfig();
}

void ConfigManager::addRecentProject(const QString& path) {
    if (path.isEmpty()) return;
    QString clean = QDir::cleanPath(path);
    m_recentProjects.removeAll(clean);
    m_recentProjects.prepend(clean);
    while (m_recentProjects.size() > 10) m_recentProjects.removeLast();
    m_lastProjectPath = clean;
    saveConfig();
    emit recentProjectsChanged();
}

void ConfigManager::clearRecentProjects() {
    m_recentProjects.clear();
    saveConfig();
    emit recentProjectsChanged();
}

void ConfigManager::setLastCustomPrompt(const QString& prompt) {
    m_lastCustomPrompt = prompt;
    saveConfig();
}

void ConfigManager::setSyntaxBaseUrl(const QString& url) {
    QString clean = url.trimmed();
    if (!clean.isEmpty() && !clean.endsWith('/')) clean += '/';
    m_syntaxBaseUrl = clean;
    saveConfig();
}

void ConfigManager::setFileTypeRules(const QVector<FileTypeRule>& rules) {
    m_fileTypes = rules;
    saveConfig();
    emit fileTypesChanged();
}

void ConfigManager::upsertFileTypeRule(const FileTypeRule& rule) {
    for (auto& existing : m_fileTypes) {
        if (existing.langId.compare(rule.langId, Qt::CaseInsensitive) == 0) {
            existing = rule;
            saveConfig();
            emit fileTypesChanged();
            return;
        }
    }
    m_fileTypes.append(rule);
    saveConfig();
    emit fileTypesChanged();
}

QString ConfigManager::languageIdForFile(const QString& filePath) const {
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext.isEmpty()) return "cpp";
    for (const auto& r : m_fileTypes) {
        if (r.extensions.contains(ext)) return r.langId;
    }
    return ext;
}

FileTypeRule ConfigManager::ruleForLanguage(const QString& langId) const {
    for (const auto& r : m_fileTypes) {
        if (r.langId.compare(langId, Qt::CaseInsensitive) == 0) return r;
    }
    return {langId, langId.toUpper(), langId + ".xml", {langId}};
}

QString ConfigManager::promptTemplate(const QString& actionKey) const {
    return m_promptTemplates[actionKey].toString();
}
