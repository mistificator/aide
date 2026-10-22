// src/core/ProjectManager.h
#pragma once
#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QDateTime>

struct FileContextData {
    QString relativePath, sha256, userPrompt, aiSummary;
    QDateTime updatedAt;
    bool isSummaryPending = false;
};

class ProjectManager : public QObject {
    Q_OBJECT
public:
    explicit ProjectManager(QObject* parent = nullptr);

    bool openProjectFolder(const QString& folderPath);
    bool loadProjectFile(const QString& aideFilePath);
    bool saveProjectFile();
    void closeProject();

    bool hasOpenProject() const { return !m_rootPath.isEmpty(); }
    QString projectRootPath() const { return m_rootPath; }
    QString projectFilePath() const { return m_aideFilePath; }

    QString projectSystemPrompt() const { return m_projectSystemPrompt; }
    void setProjectSystemPrompt(const QString& prompt);

    QString projectSummary() const { return m_projectSummary; }
    void setProjectSummary(const QString& summary);

    FileContextData fileContext(const QString& absoluteOrRelPath) const;
    void updateFileUserPrompt(const QString& filePath, const QString& prompt);
    void updateFileSummary(const QString& filePath, const QString& sha256, const QString& summary);
    void setFileSummaryPending(const QString& filePath, bool pending);

    bool isFileSummaryOutdated(const QString& filePath, const QByteArray& currentContent) const;
    bool isProjectSummaryDirty() const { return m_projectSummaryDirty; }

    QString toRelativePath(const QString& path) const;
    QString buildEffectiveSystemContext(const QString& filePath) const;
    QString buildAggregatedFilesDigest() const;

    static QString computeSha256(const QByteArray& data);

signals:
    void projectLoaded(const QString& rootPath);
    void projectClosed();
    void fileContextChanged(const QString& relativePath);
    void projectContextChanged();

private:
    QString m_rootPath, m_aideFilePath, m_projectSystemPrompt, m_projectSummary;
    QStringList m_ignoredDirs = {".git", "build", "__pycache__", "node_modules", ".aide"};
    bool m_projectSummaryDirty = false;
    QMap<QString, FileContextData> m_files;
};
