// src/core/ProjectManager.h
#pragma once
#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include "ConfigManager.h"

struct FileContextData {
    QString relativePath;
    QString sha256;
    QString llmModel;
    QString userPrompt;
    QString aiSummary;
    QDateTime updatedAt;
    bool summaryCreated = false;
    bool isSummaryPending = false;
    QString pendingSha256;
    QString pendingLlmModel;
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
    QString projectSummaryLlmModel() const { return m_projectSummaryLlmModel; }
    void setProjectSummary(const QString& summary, const QString& llmModel = {});

    FileContextData fileContext(const QString& absoluteOrRelPath) const;
    bool hasFileSummary(const QString& absoluteOrRelPath) const;
    int indexedFilesCount() const;

    void updateFileUserPrompt(const QString& filePath, const QString& prompt);
    void updateFileSummary(const QString& filePath, const QString& sha256, const QString& llmModel, const QString& summary);
    void clearFileSummary(const QString& filePath, const QString& currentSha256 = {});
    void setFileSummaryPending(const QString& filePath, bool pending, const QString& pendingSha256 = {}, const QString& pendingLlmModel = {});

    bool isFileSummaryOutdated(const QString& filePath, const QByteArray& currentContent, const QString& currentLlmModel) const;
    bool isProjectSummaryDirty() const { return m_projectSummaryDirty; }

    QString toRelativePath(const QString& path) const;
    QString buildEffectiveSystemContext(const QString& filePath) const;
    QString buildAggregatedFilesDigest() const;

    // Склеивает все проиндексированные файлы проекта в один внутренний Markdown в памяти
    QString buildIndexedFilesSourceMarkdown(const QMap<QString, QString>& unsavedBufferOverrides = {},
                                            int* outIncludedFilesCount = nullptr) const;
    // Формирует контекст для Custom Prompt по проекту согласно выбранной опции (1, 2 или 3)
    QString buildProjectCustomPromptContext(ProjectCustomPromptContextMode mode,
                                            const QMap<QString, QString>& unsavedBufferOverrides = {},
                                            int* outIndexedFilesCount = nullptr) const;

    static QString computeSha256(const QByteArray& data);

signals:
    void projectLoaded(const QString& rootPath);
    void projectClosed();
    void fileContextChanged(const QString& relativePath);
    void projectContextChanged();

private:
    QString m_rootPath, m_aideFilePath, m_projectSystemPrompt, m_projectSummary, m_projectSummaryLlmModel;
    bool m_projectSummaryDirty = false;
    QMap<QString, FileContextData> m_files;
};
