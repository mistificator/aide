// src/core/ConfigManager.cpp
#include "ConfigManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

ConfigManager::ConfigManager(QObject* parent) : QObject(parent) {
    initDefaultPrompts();
    initDefaultFileTypes();
    m_aiProfiles = defaultAiProfiles();
    m_aiConfig = m_aiProfiles.first();
    m_aiBehavior = defaultAiBehavior();
}

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

QString ConfigManager::configDirPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.aide";
}

QString ConfigManager::computePromptHash(const QString& promptText) {
    QByteArray norm = promptText.trimmed().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(norm, QCryptographicHash::Sha256).toHex());
}

QVector<AiConnectionConfig> ConfigManager::defaultAiProfiles() {
    AiConnectionConfig ollamaLocal;
    ollamaLocal.profileName = "Ollama (Offline)";
    ollamaLocal.backend = AiBackendType::Ollama;
    ollamaLocal.endpointUrl = "http://127.0.0.1:11434";
    ollamaLocal.modelName = "qwen2.5-coder:7b";
    ollamaLocal.maxOutputCodeTokens = 65536;
    ollamaLocal.maxOutputSummaryTokens = 1024;
    ollamaLocal.contextWindow = 131072;

    AiConnectionConfig koboldLocal;
    koboldLocal.profileName = "KoboldCpp (Offline)";
    koboldLocal.backend = AiBackendType::OpenAiCompatible;
    koboldLocal.endpointUrl = "http://localhost:5001/v1/";
    koboldLocal.modelName = "";
    koboldLocal.maxOutputCodeTokens = 65536;
    koboldLocal.maxOutputSummaryTokens = 1024;
    koboldLocal.contextWindow = 131072;

    AiConnectionConfig ollamaOnline;
    ollamaOnline.profileName = "Ollama.com (Online)";
    ollamaOnline.backend = AiBackendType::Ollama;
    ollamaOnline.endpointUrl = "https://ollama.com";
    ollamaOnline.modelName = "qwen2.5-coder:7b";
    ollamaOnline.maxOutputCodeTokens = 65536;
    ollamaOnline.maxOutputSummaryTokens = 1024;
    ollamaOnline.contextWindow = 131072;

    return {ollamaLocal, koboldLocal, ollamaOnline};
}

AiBehaviorConfig ConfigManager::defaultAiBehavior() {
    AiBehaviorConfig b;
    b.autoSummaryMode = AutoSummaryMode::FilesAndProject;
    b.includeDocumentSummaryInPrompt = true;
    b.includeProjectSummaryInPrompt = true;
    b.projectCustomPromptContextMode = ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown;
    b.summaryUseSyntaxExtensions = true;
    b.promptHistoryLimit = 50;
    b.summaryAllowedFilenames = {
        "CMakeLists.txt", "Makefile", "GNUmakefile", "Dockerfile", "meson.build", "SConstruct"
    };
    b.summaryAllowedExtensions = {
        "cpp", "cxx", "cc", "hpp", "hxx", "hh", "h", "c",
        "py", "pyw", "pyi", "sh", "bash", "zsh",
        "sql", "ddl", "dml", "js", "mjs", "cjs", "jsx", "ts", "tsx", "cmake"
    };
    b.summaryIgnoredDirs = {
        ".*",
        "build",
        "build-*",
        "cmake-build-*",
        "out",
        "dist",
        "bin",
        "obj",
        "node_modules",
        "__pycache__",
        "venv",
        ".venv",
        "vendor",
        "third_party",
        "3rdparty",
        "external",
        "target",
        "Debug",
        "Release"
    };
    return b;
}

bool ConfigManager::initializeStorage() {
    QDir dir(configDirPath());
    bool ok = dir.mkpath(".") && dir.mkpath("syntax") && dir.mkpath("themes");
    if (!QFile::exists(dir.filePath("config.json"))) saveConfig();
    if (!QFile::exists(dir.filePath("prompts.json"))) savePrompts();
    return ok;
}

QJsonObject ConfigManager::defaultPromptTemplates() {
    return {
        {"ExplainCode", "Explain the logic, architecture, and potential pitfalls of the following code clearly and concisely:"},
        {"RefactorCode", "Refactor the following code to improve readability, structure, and idiomatic style. Output ONLY raw code without markdown fences:"},
        {"FindBugs", "Find potential bugs, memory leaks, edge cases, or vulnerabilities in the following code and fix them. Output ONLY raw code without markdown fences:"},
        {"OptimizePerformance", "Optimize the algorithmic complexity and memory usage of the following code. Output ONLY raw code without markdown fences:"},
        {"AddDocumentation", "Add comprehensive documentation comments (Doxygen/Docstrings) to the following code. Output ONLY raw code without markdown fences:"},
        {"GenerateUnitTests", "Generate unit tests covering main scenarios and edge cases for the following code. Output ONLY raw code without markdown fences:"},
        {"CompleteCode", "Complete the implementation of the following code based on its context and signatures. Output ONLY raw code without markdown fences:"},
        {"CustomPrompt", "If modifying code, output ONLY raw code without markdown fences:"},
        {"ProjectCustomPrompt", "Provide a comprehensive, well-structured response in Markdown format. When outputting files or code fragments, always use fenced code blocks with the language identifier and precede each file block with a Markdown heading containing its relative path (e.g., `### File: src/path/file.ext`):"},
        {"FileSummary", "Analyze this source file and output a compact structured technical summary in Markdown format (use ### headings, bullet points, and `inline code`): 1) Purpose, 2) Exported classes/structs/functions with signatures, 3) Key dependencies."},
        {"ProjectSummary", "Synthesize a concise architectural summary of the entire project in Markdown format (use ### headings, bullet points, and `inline code`) based on the following individual file summaries:"}
    };
}

QString ConfigManager::defaultPromptTemplate(const QString& actionKey) {
    return defaultPromptTemplates()[actionKey].toString();
}

void ConfigManager::initDefaultPrompts() {
    m_promptTemplates = defaultPromptTemplates();
}

void ConfigManager::savePrompts() const {
    QSaveFile f(configDirPath() + "/prompts.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_promptTemplates).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void ConfigManager::setPromptTemplates(const QJsonObject& templates) {
    m_promptTemplates = defaultPromptTemplates();
    for (auto it = templates.begin(); it != templates.end(); ++it) {
        QString val = it.value().toString().trimmed();
        if (!val.isEmpty()) {
            m_promptTemplates[it.key()] = val;
        }
    }
    savePrompts();
    emit promptsChanged();
}

void ConfigManager::enforcePromptHistoryLimit() {
    int limit = qBound(5, m_aiBehavior.promptHistoryLimit, 1000);

    std::stable_partition(m_promptHistory.begin(), m_promptHistory.end(),
                          [](const PromptHistoryItem& item) { return item.pinned; });

    while (m_promptHistory.size() > limit) {
        int lastUnpinned = -1;
        for (int i = m_promptHistory.size() - 1; i >= 0; --i) {
            if (!m_promptHistory[i].pinned) {
                lastUnpinned = i;
                break;
            }
        }
        if (lastUnpinned >= 0) {
            m_promptHistory.removeAt(lastUnpinned);
        } else {
            m_promptHistory.removeLast();
        }
    }
}

void ConfigManager::setPromptHistory(const QVector<PromptHistoryItem>& items) {
    m_promptHistory.clear();
    QSet<QString> seenHashes;

    for (auto it : items) {
        it.text = it.text.trimmed();
        if (it.text.isEmpty()) continue;
        it.sha256 = computePromptHash(it.text);
        if (seenHashes.contains(it.sha256)) continue;
        seenHashes.insert(it.sha256);
        if (!it.updatedAt.isValid()) it.updatedAt = QDateTime::currentDateTimeUtc();
        m_promptHistory.append(it);
    }

    enforcePromptHistoryLimit();
    saveConfig();
    emit promptHistoryChanged();
}

void ConfigManager::addPromptToHistory(const QString& promptText) {
    QString clean = promptText.trimmed();
    if (clean.isEmpty()) return;

    QString hash = computePromptHash(clean);
    QDateTime now = QDateTime::currentDateTimeUtc();

    int existingIdx = -1;
    for (int i = 0; i < m_promptHistory.size(); ++i) {
        if (m_promptHistory[i].sha256 == hash) {
            existingIdx = i;
            break;
        }
    }

    int firstUnpinnedIdx = 0;
    while (firstUnpinnedIdx < m_promptHistory.size() && m_promptHistory[firstUnpinnedIdx].pinned) {
        ++firstUnpinnedIdx;
    }

    if (existingIdx >= 0) {
        PromptHistoryItem item = m_promptHistory.takeAt(existingIdx);
        item.text = clean;
        item.updatedAt = now;
        if (item.pinned) {
            m_promptHistory.prepend(item);
        } else {
            int insertPos = 0;
            while (insertPos < m_promptHistory.size() && m_promptHistory[insertPos].pinned) {
                ++insertPos;
            }
            m_promptHistory.insert(insertPos, item);
        }
    } else {
        PromptHistoryItem item;
        item.text = clean;
        item.sha256 = hash;
        item.pinned = false;
        item.updatedAt = now;
        m_promptHistory.insert(firstUnpinnedIdx, item);
    }

    enforcePromptHistoryLimit();
    saveConfig();
    emit promptHistoryChanged();
}

void ConfigManager::setPromptHistoryLimit(int limit) {
    int bounded = qBound(5, limit, 1000);
    if (m_aiBehavior.promptHistoryLimit == bounded) return;
    m_aiBehavior.promptHistoryLimit = bounded;
    enforcePromptHistoryLimit();
    saveConfig();
    emit promptHistoryChanged();
    emit aiBehaviorChanged(m_aiBehavior);
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

QJsonObject ConfigManager::serializeAiConfig(const AiConnectionConfig& cfg) {
    return QJsonObject{
        {"profileName", cfg.profileName},
        {"backend", static_cast<int>(cfg.backend)},
        {"endpointUrl", cfg.endpointUrl},
        {"apiKey", cfg.apiKey},
        {"modelName", cfg.modelName},
        {"temperature", cfg.temperature},
        {"topP", cfg.topP},
        {"maxOutputCodeTokens", cfg.maxOutputCodeTokens},
        {"maxOutputSummaryTokens", cfg.maxOutputSummaryTokens},
        {"contextWindow", cfg.contextWindow},
        {"timeoutSec", cfg.timeoutSec},
        {"projectSummaryIntervalMin", cfg.projectSummaryIntervalMin}
    };
}

AiConnectionConfig ConfigManager::deserializeAiConfig(const QJsonObject& ai, const QString& fallbackName) {
    AiConnectionConfig cfg;
    cfg.profileName = ai["profileName"].toString(fallbackName);
    cfg.backend = static_cast<AiBackendType>(ai["backend"].toInt(0));
    cfg.endpointUrl = ai["endpointUrl"].toString("http://127.0.0.1:11434");
    cfg.apiKey = ai["apiKey"].toString();
    cfg.modelName = ai["modelName"].toString("qwen2.5-coder:7b");
    cfg.temperature = ai["temperature"].toDouble(0.2);
    cfg.topP = ai["topP"].toDouble(0.9);
    int legacyMaxTokens = ai["maxOutputTokens"].toInt(65536);
    cfg.maxOutputCodeTokens = ai["maxOutputCodeTokens"].toInt(legacyMaxTokens);
    cfg.maxOutputSummaryTokens = ai["maxOutputSummaryTokens"].toInt(1024);
    cfg.contextWindow = ai["contextWindow"].toInt(131072);
    cfg.timeoutSec = ai["timeoutSec"].toInt(120);
    cfg.projectSummaryIntervalMin = ai["projectSummaryIntervalMin"].toInt(5);

    if (cfg.profileName == "KoboldCpp (Offline)" &&
        cfg.backend == AiBackendType::KoboldCpp &&
        (cfg.endpointUrl == "http://127.0.0.1:5001" || cfg.endpointUrl == "http://localhost:5001")) {
        cfg.backend = AiBackendType::OpenAiCompatible;
        cfg.endpointUrl = "http://localhost:5001/v1/";
    }
    return cfg;
}

void ConfigManager::loadConfig() {
    QFile cfgFile(configDirPath() + "/config.json");
    if (cfgFile.open(QIODevice::ReadOnly)) {
        auto obj = QJsonDocument::fromJson(cfgFile.readAll()).object();
        m_activeTheme = obj["activeTheme"].toString("dracula");
        m_projectExplorerFilterSupported = obj["projectExplorerFilterSupported"].toBool(false);
        m_lastProjectPath = obj["lastProjectPath"].toString();
        m_lastDialogDir = obj["lastDialogDir"].toString();
        m_lastCustomPrompt = obj["lastCustomPrompt"].toString();
        m_windowGeometry = QByteArray::fromBase64(obj["windowGeometry"].toString().toLatin1());
        m_windowState = QByteArray::fromBase64(obj["windowState"].toString().toLatin1());
        m_syntaxBaseUrl = obj["syntaxBaseUrl"].toString("https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/");

        m_recentProjects.clear();
        for (const auto& v : obj["recentProjects"].toArray()) {
            QString p = v.toString();
            if (!p.isEmpty() && QDir(p).exists() && !m_recentProjects.contains(p))
                m_recentProjects.append(p);
        }

        m_aiProfiles.clear();
        if (obj.contains("aiProfiles") && obj["aiProfiles"].isArray()) {
            for (const auto& item : obj["aiProfiles"].toArray()) {
                if (item.isObject()) {
                    m_aiProfiles.append(deserializeAiConfig(item.toObject()));
                }
            }
        }
        if (m_aiProfiles.isEmpty()) {
            m_aiProfiles = defaultAiProfiles();
            if (obj.contains("ai") && obj["ai"].isObject()) {
                AiConnectionConfig legacy = deserializeAiConfig(obj["ai"].toObject(), "Ollama (Offline)");
                m_aiProfiles[0] = legacy;
            }
        }

        QString activeProfName = obj["activeAiProfile"].toString();
        bool foundActive = false;
        for (const auto& p : m_aiProfiles) {
            if (p.profileName == activeProfName) {
                m_aiConfig = p;
                foundActive = true;
                break;
            }
        }
        if (!foundActive) {
            if (obj.contains("ai") && obj["ai"].isObject()) {
                m_aiConfig = deserializeAiConfig(obj["ai"].toObject(), m_aiProfiles.first().profileName);
            } else {
                m_aiConfig = m_aiProfiles.first();
            }
        }

        m_aiBehavior = defaultAiBehavior();
        if (obj.contains("aiBehavior") && obj["aiBehavior"].isObject()) {
            auto beh = obj["aiBehavior"].toObject();
            int modeVal = beh["autoSummaryMode"].toInt(static_cast<int>(AutoSummaryMode::FilesAndProject));
            m_aiBehavior.autoSummaryMode = static_cast<AutoSummaryMode>(qBound(0, modeVal, 2));
            m_aiBehavior.includeDocumentSummaryInPrompt = beh["includeDocumentSummaryInPrompt"].toBool(true);
            m_aiBehavior.includeProjectSummaryInPrompt = beh["includeProjectSummaryInPrompt"].toBool(true);
            int projCtxVal = beh["projectCustomPromptContextMode"].toInt(
                static_cast<int>(ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown));
            m_aiBehavior.projectCustomPromptContextMode =
                static_cast<ProjectCustomPromptContextMode>(qBound(0, projCtxVal, 2));
            m_aiBehavior.summaryUseSyntaxExtensions = beh["summaryUseSyntaxExtensions"].toBool(true);
            m_aiBehavior.promptHistoryLimit = qBound(5, beh["promptHistoryLimit"].toInt(50), 1000);

            if (beh.contains("summaryAllowedFilenames")) {
                m_aiBehavior.summaryAllowedFilenames.clear();
                for (const auto& f : beh["summaryAllowedFilenames"].toArray()) {
                    QString fn = f.toString().trimmed();
                    if (!fn.isEmpty()) m_aiBehavior.summaryAllowedFilenames.append(fn);
                }
            }
            if (beh.contains("summaryAllowedExtensions")) {
                m_aiBehavior.summaryAllowedExtensions.clear();
                for (const auto& e : beh["summaryAllowedExtensions"].toArray()) {
                    QString ext = e.toString().trimmed().toLower();
                    if (ext.startsWith('.')) ext.remove(0, 1);
                    if (!ext.isEmpty()) m_aiBehavior.summaryAllowedExtensions.append(ext);
                }
            }
            if (beh.contains("summaryIgnoredDirs")) {
                m_aiBehavior.summaryIgnoredDirs.clear();
                for (const auto& d : beh["summaryIgnoredDirs"].toArray()) {
                    QString dirPat = d.toString().trimmed();
                    if (!dirPat.isEmpty()) m_aiBehavior.summaryIgnoredDirs.append(dirPat);
                }
            }
        }

        m_promptHistory.clear();
        if (obj.contains("promptHistory") && obj["promptHistory"].isArray()) {
            QSet<QString> seen;
            for (const auto& itemVal : obj["promptHistory"].toArray()) {
                auto po = itemVal.toObject();
                QString txt = po["text"].toString().trimmed();
                if (txt.isEmpty()) continue;
                QString hash = po["sha256"].toString();
                if (hash.isEmpty()) hash = computePromptHash(txt);
                if (seen.contains(hash)) continue;
                seen.insert(hash);

                PromptHistoryItem item;
                item.text = txt;
                item.sha256 = hash;
                item.pinned = po["pinned"].toBool(false);
                item.updatedAt = QDateTime::fromString(po["updatedAt"].toString(), Qt::ISODate);
                if (!item.updatedAt.isValid()) item.updatedAt = QDateTime::currentDateTimeUtc();
                m_promptHistory.append(item);
            }
            enforcePromptHistoryLimit();
        } else if (!m_lastCustomPrompt.trimmed().isEmpty()) {
            PromptHistoryItem seed;
            seed.text = m_lastCustomPrompt.trimmed();
            seed.sha256 = computePromptHash(seed.text);
            seed.pinned = false;
            seed.updatedAt = QDateTime::currentDateTimeUtc();
            m_promptHistory.append(seed);
        }

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

    m_promptTemplates = defaultPromptTemplates();
    QFile prFile(configDirPath() + "/prompts.json");
    if (prFile.open(QIODevice::ReadOnly)) {
        auto loaded = QJsonDocument::fromJson(prFile.readAll()).object();
        for (auto it = loaded.begin(); it != loaded.end(); ++it) {
            if (!it.value().toString().trimmed().isEmpty()) {
                m_promptTemplates[it.key()] = it.value();
            }
        }
    }
}

void ConfigManager::saveConfig() {
    QJsonArray profilesArr;
    for (const auto& p : m_aiProfiles) {
        profilesArr.append(serializeAiConfig(p));
    }

    QJsonArray allowedFilesArr;
    for (const QString& fn : m_aiBehavior.summaryAllowedFilenames) allowedFilesArr.append(fn);

    QJsonArray allowedExtsArr;
    for (const QString& ext : m_aiBehavior.summaryAllowedExtensions) allowedExtsArr.append(ext);

    QJsonArray ignoredDirsArr;
    for (const QString& dirPat : m_aiBehavior.summaryIgnoredDirs) ignoredDirsArr.append(dirPat);

    QJsonObject behaviorObj{
        {"autoSummaryMode", static_cast<int>(m_aiBehavior.autoSummaryMode)},
        {"includeDocumentSummaryInPrompt", m_aiBehavior.includeDocumentSummaryInPrompt},
        {"includeProjectSummaryInPrompt", m_aiBehavior.includeProjectSummaryInPrompt},
        {"projectCustomPromptContextMode", static_cast<int>(m_aiBehavior.projectCustomPromptContextMode)},
        {"summaryUseSyntaxExtensions", m_aiBehavior.summaryUseSyntaxExtensions},
        {"promptHistoryLimit", m_aiBehavior.promptHistoryLimit},
        {"summaryAllowedFilenames", allowedFilesArr},
        {"summaryAllowedExtensions", allowedExtsArr},
        {"summaryIgnoredDirs", ignoredDirsArr}
    };

    QJsonArray historyArr;
    for (const auto& item : m_promptHistory) {
        historyArr.append(QJsonObject{
            {"text", item.text},
            {"sha256", item.sha256},
            {"pinned", item.pinned},
            {"updatedAt", item.updatedAt.toString(Qt::ISODate)}
        });
    }

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

    QJsonObject root{
        {"activeTheme", m_activeTheme},
        {"projectExplorerFilterSupported", m_projectExplorerFilterSupported},
        {"lastProjectPath", m_lastProjectPath},
        {"lastDialogDir", m_lastDialogDir},
        {"recentProjects", recentArr},
        {"lastCustomPrompt", m_lastCustomPrompt},
        {"promptHistory", historyArr},
        {"windowGeometry", QString::fromLatin1(m_windowGeometry.toBase64())},
        {"windowState", QString::fromLatin1(m_windowState.toBase64())},
        {"syntaxBaseUrl", m_syntaxBaseUrl},
        {"fileTypes", typesArr},
        {"activeAiProfile", m_aiConfig.profileName},
        {"aiProfiles", profilesArr},
        {"ai", serializeAiConfig(m_aiConfig)},
        {"aiBehavior", behaviorObj}
    };

    QSaveFile f(configDirPath() + "/config.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
}

void ConfigManager::setAiConfig(const AiConnectionConfig& cfg) {
    m_aiConfig = cfg;
    bool updated = false;
    for (auto& p : m_aiProfiles) {
        if (p.profileName == cfg.profileName) {
            p = cfg;
            updated = true;
            break;
        }
    }
    if (!updated) {
        m_aiProfiles.append(cfg);
    }
    saveConfig();
    emit aiConfigChanged(m_aiConfig);
}

void ConfigManager::setAiProfiles(const QVector<AiConnectionConfig>& profiles, const QString& activeProfileName) {
    m_aiProfiles = profiles.isEmpty() ? defaultAiProfiles() : profiles;
    bool found = false;
    for (const auto& p : m_aiProfiles) {
        if (p.profileName == activeProfileName) {
            m_aiConfig = p;
            found = true;
            break;
        }
    }
    if (!found) m_aiConfig = m_aiProfiles.first();
    saveConfig();
    emit aiConfigChanged(m_aiConfig);
}

void ConfigManager::setAiBehavior(const AiBehaviorConfig& behavior) {
    m_aiBehavior = behavior;
    m_aiBehavior.promptHistoryLimit = qBound(5, m_aiBehavior.promptHistoryLimit, 1000);
    enforcePromptHistoryLimit();
    saveConfig();
    emit promptHistoryChanged();
    emit aiBehaviorChanged(m_aiBehavior);
}

void ConfigManager::setProjectExplorerFilterSupported(bool filterSupportedOnly) {
    if (m_projectExplorerFilterSupported == filterSupportedOnly) return;
    m_projectExplorerFilterSupported = filterSupportedOnly;
    saveConfig();
    emit projectExplorerFilterChanged(m_projectExplorerFilterSupported);
}

QStringList ConfigManager::extensionsFromLoadedSyntaxes() const {
    QStringList result;
    QString synDir = syntaxDirPath();
    for (const auto& r : m_fileTypes) {
        bool hasXml = !r.syntaxFile.isEmpty() && QFile::exists(synDir + "/" + r.syntaxFile);
        if (hasXml || !r.extensions.isEmpty()) {
            for (const QString& ext : r.extensions) {
                QString clean = ext.trimmed().toLower();
                if (!clean.isEmpty() && !result.contains(clean)) {
                    result.append(clean);
                }
            }
        }
    }
    return result;
}

QStringList ConfigManager::supportedProjectFileNameFilters() const {
    QStringList filters;
    auto addExt = [&](QString ext) {
        ext = ext.trimmed().toLower();
        if (ext.startsWith("*.")) ext = ext.mid(2);
        else if (ext.startsWith('.')) ext = ext.mid(1);
        if (!ext.isEmpty()) {
            QString mask = "*." + ext;
            if (!filters.contains(mask, Qt::CaseInsensitive)) {
                filters.append(mask);
            }
        }
    };

    for (const auto& r : m_fileTypes) {
        for (const QString& ext : r.extensions) {
            addExt(ext);
        }
    }
    for (const QString& ext : m_aiBehavior.summaryAllowedExtensions) {
        addExt(ext);
    }
    for (const QString& fn : m_aiBehavior.summaryAllowedFilenames) {
        QString clean = fn.trimmed();
        if (!clean.isEmpty() && !filters.contains(clean, Qt::CaseInsensitive)) {
            filters.append(clean);
        }
    }
    return filters;
}

bool ConfigManager::shouldGenerateSummaryForFile(const QString& filePath, QString* outSkipReasonMarkdown) const {
    if (filePath.isEmpty()) return false;
    QFileInfo fi(filePath);
    QString fileName = fi.fileName();
    QString ext = fi.suffix().toLower();

    for (const QString& pattern : m_aiBehavior.summaryAllowedFilenames) {
        QString cleanPat = pattern.trimmed();
        if (cleanPat.isEmpty()) continue;
        if (fileName.compare(cleanPat, Qt::CaseInsensitive) == 0 ||
            QDir::match(cleanPat, fileName)) {
            return true;
        }
    }

    if (!ext.isEmpty()) {
        for (const QString& allowedExt : m_aiBehavior.summaryAllowedExtensions) {
            if (ext.compare(allowedExt.trimmed(), Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        if (m_aiBehavior.summaryUseSyntaxExtensions) {
            const QStringList synExts = extensionsFromLoadedSyntaxes();
            if (synExts.contains(ext, Qt::CaseInsensitive)) {
                return true;
            }
        }
    }

    if (outSkipReasonMarkdown) {
        *outSkipReasonMarkdown = QString(
                                     "### Summary Not Required\n\n"
                                     "AI summary generation is disabled for **`%1`** according to current **AI Preferences -> Behavior & Summary Rules**.\n\n"
                                     "- **File Name:** `%1` *(not in allowed file names list)*\n"
                                     "- **Extension:** `%2` *(not in allowed summary extensions)*\n\n"
                                     "*You can add `%1` or `%2` in `AI -> Preferences -> Behavior & Summary Rules`, or click **Regenerate** to force summary creation manually.*")
                                     .arg(fileName, ext.isEmpty() ? "(none)" : "." + ext);
    }
    return false;
}

bool ConfigManager::isDirectoryIgnoredForSummary(const QString& dirNameOrPath) const {
    if (dirNameOrPath.isEmpty()) return false;
    QString clean = QDir::cleanPath(dirNameOrPath);
    QString dirName = QFileInfo(clean).fileName();
    if (dirName.isEmpty()) dirName = clean;

    for (const QString& patRaw : m_aiBehavior.summaryIgnoredDirs) {
        QString pat = patRaw.trimmed();
        if (pat.isEmpty()) continue;
        if ((pat == ".*" || pat == ".") && dirName.startsWith('.')) {
            return true;
        }
        if (dirName.compare(pat, Qt::CaseInsensitive) == 0 ||
            QDir::match(pat, dirName)) {
            return true;
        }
    }
    return false;
}

void ConfigManager::setActiveTheme(const QString& themeName) {
    if (m_activeTheme == themeName) return;
    m_activeTheme = themeName;
    saveConfig();
    emit themeChanged(m_activeTheme);
}

void ConfigManager::setLastProjectPath(const QString& path) {
    m_lastProjectPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (!m_lastProjectPath.isEmpty()) {
        m_lastDialogDir = m_lastProjectPath;
    }
    saveConfig();
}

void ConfigManager::setLastDialogDir(const QString& dirOrFilePath) {
    if (dirOrFilePath.isEmpty()) return;
    QFileInfo fi(dirOrFilePath);
    QString dirPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (!dirPath.isEmpty() && QDir(dirPath).exists()) {
        m_lastDialogDir = QDir::cleanPath(dirPath);
        saveConfig();
    }
}

QString ConfigManager::resolveInitialDialogDir(const QString& preferredFileOrDir) const {
    if (!preferredFileOrDir.isEmpty()) {
        QFileInfo fi(preferredFileOrDir);
        if (fi.exists() || QDir(fi.absolutePath()).exists()) {
            return QDir::cleanPath(preferredFileOrDir);
        }
    }
    if (!m_lastProjectPath.isEmpty() && QDir(m_lastProjectPath).exists()) {
        return m_lastProjectPath;
    }
    if (!m_lastDialogDir.isEmpty() && QDir(m_lastDialogDir).exists()) {
        return m_lastDialogDir;
    }
    if (!m_recentProjects.isEmpty() && QDir(m_recentProjects.first()).exists()) {
        return m_recentProjects.first();
    }
    return QDir::homePath();
}

void ConfigManager::addRecentProject(const QString& path) {
    if (path.isEmpty()) return;
    QString clean = QDir::cleanPath(path);
    m_recentProjects.removeAll(clean);
    m_recentProjects.prepend(clean);
    while (m_recentProjects.size() > 10) m_recentProjects.removeLast();
    m_lastProjectPath = clean;
    m_lastDialogDir = clean;
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
    addPromptToHistory(prompt);
}

void ConfigManager::setWindowGeometryAndState(const QByteArray& geometry, const QByteArray& state) {
    m_windowGeometry = geometry;
    m_windowState = state;
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
    QString val = m_promptTemplates[actionKey].toString();
    return val.isEmpty() ? defaultPromptTemplate(actionKey) : val;
}
