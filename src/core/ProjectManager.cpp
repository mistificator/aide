// src/core/ProjectManager.cpp
#include "ProjectManager.h"
#include "ConfigManager.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <ranges>

ProjectManager::ProjectManager(QObject* parent) : QObject(parent) {}

QString ProjectManager::computeSha256(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString ProjectManager::toRelativePath(const QString& path) const {
    if (path.isEmpty()) return {};
    if (m_rootPath.isEmpty()) return QDir::cleanPath(path);
    QFileInfo fi(path);
    if (fi.isRelative()) return QDir::cleanPath(path);
    return QDir(m_rootPath).relativeFilePath(fi.absoluteFilePath());
}

bool ProjectManager::openProjectFolder(const QString& folderPath) {
    QDir dir(folderPath);
    if (!dir.exists()) return false;
    m_rootPath = dir.absolutePath();

    auto aideFiles = dir.entryList({"*.aide"}, QDir::Files);
    m_aideFilePath = dir.filePath(aideFiles.isEmpty()
                                      ? (dir.dirName().isEmpty() ? "project.aide" : dir.dirName() + ".aide")
                                      : aideFiles.first());

    m_files.clear();
    m_projectSystemPrompt.clear();
    m_projectSummary.clear();
    m_projectSummaryLlmModel.clear();
    m_projectSummaryDirty = false;

    if (QFile::exists(m_aideFilePath)) loadProjectFile(m_aideFilePath);
    else saveProjectFile();

    emit projectLoaded(m_rootPath);
    return true;
}

bool ProjectManager::loadProjectFile(const QString& aideFilePath) {
    QFile f(aideFilePath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QFileInfo fi(aideFilePath);
    m_aideFilePath = fi.absoluteFilePath();
    m_rootPath = fi.absolutePath();

    auto root = QJsonDocument::fromJson(f.readAll()).object();
    m_projectSystemPrompt = root["project_system_prompt"].toString();
    m_projectSummary = root["project_summary"].toString();
    m_projectSummaryLlmModel = root["project_summary_llm_model"].toString();
    m_projectSummaryDirty = root["project_summary_dirty"].toBool(false);

    QString defaultLlm = ConfigManager::instance().aiConfig().llmIdentifier();

    m_files.clear();
    auto filesObj = root["files"].toObject();
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        auto fo = it.value().toObject();
        QString summary = fo["ai_summary"].toString();
        QString savedLlm = fo["llm_model"].toString();
        QString sha = fo["sha256"].toString();
        bool created = fo.contains("summary_created")
                           ? fo["summary_created"].toBool()
                           : (!summary.trimmed().isEmpty() || !sha.isEmpty());
        if (savedLlm.isEmpty() && !summary.isEmpty()) {
            savedLlm = defaultLlm;
        }
        m_files[it.key()] = {
            it.key(),
            sha,
            savedLlm,
            fo["user_prompt"].toString(),
            summary,
            QDateTime::fromString(fo["updated_at"].toString(), Qt::ISODate),
            created,
            false,
            {},
            {}
        };
    }

    emit projectLoaded(m_rootPath);
    emit projectContextChanged();
    return true;
}

bool ProjectManager::saveProjectFile() {
    if (m_aideFilePath.isEmpty()) return false;
    QJsonObject filesObj;
    for (const auto& [relPath, data] : m_files.asKeyValueRange()) {
        filesObj[relPath] = QJsonObject{
            {"sha256", data.sha256},
            {"llm_model", data.llmModel},
            {"user_prompt", data.userPrompt},
            {"ai_summary", data.aiSummary},
            {"summary_created", data.summaryCreated},
            {"updated_at", data.updatedAt.toString(Qt::ISODate)}
        };
    }

    QJsonArray ignoredArr;
    for (const auto& d : ConfigManager::instance().aiBehavior().summaryIgnoredDirs) {
        ignoredArr.append(d);
    }

    QJsonObject root{
        {"project_name", QDir(m_rootPath).dirName()},
        {"ignored_dirs", ignoredArr},
        {"project_system_prompt", m_projectSystemPrompt},
        {"project_summary", m_projectSummary},
        {"project_summary_llm_model", m_projectSummaryLlmModel},
        {"project_summary_dirty", m_projectSummaryDirty},
        {"updated_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"files", filesObj}
    };

    QSaveFile f(m_aideFilePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

void ProjectManager::closeProject() {
    if (!m_rootPath.isEmpty()) {
        saveProjectFile();
    }
    m_rootPath.clear();
    m_aideFilePath.clear();
    m_projectSystemPrompt.clear();
    m_projectSummary.clear();
    m_projectSummaryLlmModel.clear();
    m_projectSummaryDirty = false;
    m_files.clear();
    emit projectClosed();
    emit projectContextChanged();
}

void ProjectManager::setProjectSystemPrompt(const QString& prompt) {
    if (m_projectSystemPrompt == prompt) return;
    m_projectSystemPrompt = prompt;
    saveProjectFile();
    emit projectContextChanged();
}

void ProjectManager::setProjectSummary(const QString& summary, const QString& llmModel) {
    m_projectSummary = summary;
    if (!llmModel.isEmpty()) m_projectSummaryLlmModel = llmModel;
    m_projectSummaryDirty = false;
    saveProjectFile();
    emit projectContextChanged();
}

FileContextData ProjectManager::fileContext(const QString& absoluteOrRelPath) const {
    auto rel = toRelativePath(absoluteOrRelPath);
    return m_files.value(rel, FileContextData{.relativePath = rel});
}

bool ProjectManager::hasFileSummary(const QString& absoluteOrRelPath) const {
    auto rel = toRelativePath(absoluteOrRelPath);
    if (!m_files.contains(rel)) return false;
    const auto& entry = m_files[rel];
    return entry.summaryCreated || !entry.aiSummary.trimmed().isEmpty();
}

void ProjectManager::updateFileUserPrompt(const QString& filePath, const QString& prompt) {
    auto rel = toRelativePath(filePath);
    auto& entry = m_files[rel];
    entry.relativePath = rel;
    if (entry.userPrompt == prompt) return;
    entry.userPrompt = prompt;
    saveProjectFile();
    emit fileContextChanged(rel);
}

void ProjectManager::updateFileSummary(const QString& filePath, const QString& sha256, const QString& llmModel, const QString& summary) {
    auto rel = toRelativePath(filePath);
    auto& entry = m_files[rel];
    entry.relativePath = rel;
    entry.sha256 = sha256;
    entry.llmModel = llmModel;
    entry.aiSummary = summary;
    entry.summaryCreated = true;
    entry.updatedAt = QDateTime::currentDateTimeUtc();
    entry.isSummaryPending = false;
    entry.pendingSha256.clear();
    entry.pendingLlmModel.clear();
    m_projectSummaryDirty = true;
    saveProjectFile();
    emit fileContextChanged(rel);
    emit projectContextChanged();
}

void ProjectManager::clearFileSummary(const QString& filePath, const QString& currentSha256) {
    auto rel = toRelativePath(filePath);
    auto& entry = m_files[rel];
    entry.relativePath = rel;
    if (!currentSha256.isEmpty()) {
        entry.sha256 = currentSha256;
    }
    entry.llmModel = "cleared (empty)";
    entry.aiSummary.clear();
    entry.summaryCreated = true; // Считаем, что саммари создано, просто оно пустое
    entry.updatedAt = QDateTime::currentDateTimeUtc();
    entry.isSummaryPending = false;
    entry.pendingSha256.clear();
    entry.pendingLlmModel.clear();
    m_projectSummaryDirty = true;
    saveProjectFile();
    emit fileContextChanged(rel);
    emit projectContextChanged();
}

void ProjectManager::setFileSummaryPending(const QString& filePath, bool pending, const QString& pendingSha256, const QString& pendingLlmModel) {
    auto rel = toRelativePath(filePath);
    auto& entry = m_files[rel];
    entry.relativePath = rel;
    entry.isSummaryPending = pending;
    if (pending) {
        entry.pendingSha256 = pendingSha256;
        entry.pendingLlmModel = pendingLlmModel;
    } else {
        entry.pendingSha256.clear();
        entry.pendingLlmModel.clear();
    }
    emit fileContextChanged(rel);
}

bool ProjectManager::isFileSummaryOutdated(const QString& filePath, const QByteArray& /*currentContent*/, const QString& /*currentLlmModel*/) const {
    auto rel = toRelativePath(filePath);
    if (!m_files.contains(rel)) return true;
    const auto& entry = m_files[rel];

    if (entry.isSummaryPending) return false;
    if (entry.summaryCreated || !entry.aiSummary.trimmed().isEmpty()) return false;
    return true;
}

QString ProjectManager::buildEffectiveSystemContext(const QString& filePath) const {
    const auto beh = ConfigManager::instance().aiBehavior();
    QStringList sections;

    if (!m_projectSystemPrompt.trimmed().isEmpty())
        sections << "=== PROJECT SYSTEM INSTRUCTIONS ===\n" + m_projectSystemPrompt.trimmed();

    if (beh.includeProjectSummaryInPrompt && !m_projectSummary.trimmed().isEmpty())
        sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + m_projectSummary.trimmed();

    if (!filePath.isEmpty()) {
        auto fc = fileContext(filePath);
        if (!fc.userPrompt.trimmed().isEmpty())
            sections << QString("=== DOCUMENT INSTRUCTIONS (%1) ===\n%2").arg(fc.relativePath, fc.userPrompt.trimmed());
        if (beh.includeDocumentSummaryInPrompt && !fc.aiSummary.trimmed().isEmpty())
            sections << QString("=== DOCUMENT SUMMARY (%1) ===\n%2").arg(fc.relativePath, fc.aiSummary.trimmed());
    }
    return sections.join("\n\n");
}

QString ProjectManager::buildAggregatedFilesDigest() const {
    QStringList digest;
    for (const auto& fc : m_files | std::views::filter([](const auto& f) { return !f.aiSummary.trimmed().isEmpty(); }))
        digest << QString("## File: %1\n%2").arg(fc.relativePath, fc.aiSummary.trimmed());
    return digest.join("\n\n");
}

int ProjectManager::indexedFilesCount() const {
    int count = 0;
    for (const auto& [relPath, fc] : m_files.asKeyValueRange()) {
        if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty()) continue;
        QString absPath = m_rootPath.isEmpty() ? relPath : QDir(m_rootPath).filePath(relPath);
        if (QFileInfo::exists(absPath)) {
            ++count;
        }
    }
    return count;
}

QString ProjectManager::buildIndexedFilesSourceMarkdown(const QMap<QString, QString>& unsavedBufferOverrides,
                                                        int* outIncludedFilesCount) const {
    if (outIncludedFilesCount) *outIncludedFilesCount = 0;
    if (m_rootPath.isEmpty()) return {};

    QStringList blocks;
    int fileCounter = 0;

    for (const auto& [relPath, fc] : m_files.asKeyValueRange()) {
        if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty()) {
            continue;
        }

        QString absPath = QFileInfo(QDir(m_rootPath).filePath(relPath)).absoluteFilePath();
        QString content;

        if (unsavedBufferOverrides.contains(absPath)) {
            content = unsavedBufferOverrides.value(absPath);
        } else if (unsavedBufferOverrides.contains(relPath)) {
            content = unsavedBufferOverrides.value(relPath);
        } else {
            QFile f(absPath);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            content = QString::fromUtf8(f.readAll());
        }

        QString langId = ConfigManager::instance().languageIdForFile(absPath);
        QString fence = "```";
        while (content.contains(fence)) {
            fence += '`';
        }

        QString block = QString("## File: `%1`\n%2%3\n%4\n%2")
                            .arg(relPath, fence, langId, content);
        blocks.append(block);
        ++fileCounter;
    }

    if (outIncludedFilesCount) *outIncludedFilesCount = fileCounter;
    return blocks.join("\n\n");
}

QString ProjectManager::buildProjectCustomPromptContext(ProjectCustomPromptContextMode mode,
                                                        const QMap<QString, QString>& unsavedBufferOverrides,
                                                        int* outIndexedFilesCount) const {
    if (outIndexedFilesCount) *outIndexedFilesCount = 0;
    QStringList sections;

    if (!m_projectSystemPrompt.trimmed().isEmpty()) {
        sections << "=== PROJECT SYSTEM INSTRUCTIONS ===\n" + m_projectSystemPrompt.trimmed();
    }

    bool includeSummary = (mode == ProjectCustomPromptContextMode::ProjectSummaryOnly ||
                           mode == ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown);
    bool includeSources = (mode == ProjectCustomPromptContextMode::IndexedSourcesMarkdownOnly ||
                           mode == ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown);

    if (includeSummary) {
        QString sumText = m_projectSummary.trimmed();
        if (sumText.isEmpty()) {
            sumText = buildAggregatedFilesDigest().trimmed();
        }
        if (!sumText.isEmpty()) {
            sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + sumText;
        }
    }

    if (includeSources) {
        int count = 0;
        QString sourcesMd = buildIndexedFilesSourceMarkdown(unsavedBufferOverrides, &count);
        if (outIndexedFilesCount) *outIndexedFilesCount = count;
        if (!sourcesMd.trimmed().isEmpty()) {
            sections << QString("=== ALL INDEXED PROJECT SOURCE FILES (%1 file(s)) ===\n\n%2")
            .arg(count)
                .arg(sourcesMd);
        }
    }

    return sections.join("\n\n");
}
