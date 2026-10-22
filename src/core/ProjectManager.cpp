// src/core/ProjectManager.cpp
#include "ProjectManager.h"
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
    if (m_rootPath.isEmpty()) return QDir::cleanPath(path);
    QFileInfo fi(path);
    return fi.isRelative() ? QDir::cleanPath(path) : QDir(m_rootPath).relativeFilePath(fi.absoluteFilePath());
}

bool ProjectManager::openProjectFolder(const QString& folderPath) {
    QDir dir(folderPath);
    if (!dir.exists()) return false;
    m_rootPath = dir.absolutePath();
    auto aideFiles = dir.entryList({"*.aide"}, QDir::Files);
    m_aideFilePath = dir.filePath(aideFiles.isEmpty() ? (dir.dirName().isEmpty() ? "project.aide" : dir.dirName() + ".aide") : aideFiles.first());

    m_files.clear();
    m_projectSystemPrompt.clear();
    m_projectSummary.clear();
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
    m_projectSummaryDirty = root["project_summary_dirty"].toBool(false);

    m_files.clear();
    auto filesObj = root["files"].toObject();
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        auto fo = it.value().toObject();
        m_files[it.key()] = {
            it.key(), fo["sha256"].toString(), fo["user_prompt"].toString(),
            fo["ai_summary"].toString(), QDateTime::fromString(fo["updated_at"].toString(), Qt::ISODate), false
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
            {"user_prompt", data.userPrompt},
            {"ai_summary", data.aiSummary},
            {"updated_at", data.updatedAt.toString(Qt::ISODate)}
        };
    }
    QJsonArray ignoredArr;
    for (const auto& d : m_ignoredDirs) ignoredArr.append(d);

    QJsonObject root{
        {"project_name", QDir(m_rootPath).dirName()},
        {"ignored_dirs", ignoredArr},
        {"project_system_prompt", m_projectSystemPrompt},
        {"project_summary", m_projectSummary},
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

void ProjectManager::setProjectSummary(const QString& summary) {
    m_projectSummary = summary;
    m_projectSummaryDirty = false;
    saveProjectFile();
    emit projectContextChanged();
}

FileContextData ProjectManager::fileContext(const QString& absoluteOrRelPath) const {
    auto rel = toRelativePath(absoluteOrRelPath);
    return m_files.value(rel, FileContextData{.relativePath = rel});
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

void ProjectManager::updateFileSummary(const QString& filePath, const QString& sha256, const QString& summary) {
    auto rel = toRelativePath(filePath);
    auto& entry = m_files[rel];
    entry = {rel, sha256, entry.userPrompt, summary, QDateTime::currentDateTimeUtc(), false};
    m_projectSummaryDirty = true;
    saveProjectFile();
    emit fileContextChanged(rel);
    emit projectContextChanged();
}

void ProjectManager::setFileSummaryPending(const QString& filePath, bool pending) {
    auto rel = toRelativePath(filePath);
    auto& entry = m_files[rel];
    entry.relativePath = rel;
    entry.isSummaryPending = pending;
    emit fileContextChanged(rel);
}

bool ProjectManager::isFileSummaryOutdated(const QString& filePath, const QByteArray& currentContent) const {
    auto rel = toRelativePath(filePath);
    if (!m_files.contains(rel)) return true;
    const auto& entry = m_files[rel];
    return entry.aiSummary.isEmpty() || entry.sha256 != computeSha256(currentContent);
}

QString ProjectManager::buildEffectiveSystemContext(const QString& filePath) const {
    QStringList sections;
    if (!m_projectSystemPrompt.trimmed().isEmpty())
        sections << "=== PROJECT SYSTEM INSTRUCTIONS ===\n" + m_projectSystemPrompt.trimmed();
    if (!m_projectSummary.trimmed().isEmpty())
        sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + m_projectSummary.trimmed();

    if (!filePath.isEmpty()) {
        auto fc = fileContext(filePath);
        if (!fc.userPrompt.trimmed().isEmpty())
            sections << QString("=== DOCUMENT INSTRUCTIONS (%1) ===\n%2").arg(fc.relativePath, fc.userPrompt.trimmed());
        if (!fc.aiSummary.trimmed().isEmpty())
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
