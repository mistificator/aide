// src/core/ProjectManager.h
#pragma once
#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QTimer>
#include "ConfigManager.h"

struct FileContextData {
    QString relativePath;       // Путь относительно корня открытого проекта (m_rootNode.rootPath)
    QString localRelativePath;  // Путь относительно владеющего проекта/подпроекта
    QString subprojectRelPath;  // Относительный путь к подпроекту (пусто для корневого проекта)
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

    bool hasOpenProject() const { return !m_rootNode.rootPath.isEmpty(); }
    QString projectRootPath() const { return m_rootNode.rootPath; }
    QString projectFilePath() const { return m_rootNode.aideFilePath; }

    // Работа с корневым проектом или конкретным подпроектом (projectDirOrRel = "" -> корневой проект)
    QString projectSystemPrompt(const QString& projectDirOrRel = {}) const;
    void setProjectSystemPrompt(const QString& prompt, const QString& projectDirOrRel = {});

    QString projectSummary(const QString& projectDirOrRel = {}) const;
    QString projectSummaryLlm(const QString& projectDirOrRel = {}) const;
    void setProjectSummary(const QString& summary,
                           const QString& llmModel = {},
                           const QString& projectDirOrRel = {});

    bool isProjectSummaryDirty(const QString& projectDirOrRel = {}) const;

    // Управление подпроектами (.aide во вложенных папках)
    bool isSubprojectDir(const QString& dirPath) const;
    QStringList subprojectRelPaths() const;
    QStringList directChildSubprojects(const QString& parentProjectDirOrRel = {}) const;
    QStringList dirtyProjectRelPathsBottomUp() const;
    QString owningProjectRelPathForFile(const QString& filePath) const;
    QString owningProjectDirPathForFile(const QString& filePath) const;
    QString projectDirPathForRel(const QString& projectDirOrRel = {}) const;

    bool createSubproject(const QString& dirPath);
    bool removeSubproject(const QString& dirPath);
    void reconcileProjectStructure();

    // Операции с файлами проекта и их саммари
    FileContextData fileContext(const QString& absoluteOrRelPath) const;
    void updateFileUserPrompt(const QString& filePath, const QString& prompt);
    void updateFileSummary(const QString& filePath,
                           const QString& sha256,
                           const QString& llmModel,
                           const QString& summary);
    void clearFileSummary(const QString& filePath, const QString& sha256 = {});
    void removeFileSummary(const QString& filePath);
    void setFileSummaryPending(const QString& filePath,
                               bool pending,
                               const QString& pendingSha256 = {},
                               const QString& pendingLlmModel = {});

    bool renameProjectFile(const QString& oldFilePath, const QString& newFilePath);
    bool deleteProjectFile(const QString& filePath);

    bool isFileSummaryOutdated(const QString& filePath,
                               const QByteArray& currentContent,
                               const QString& currentLlm = {}) const;
    int indexedFilesCount(const QString& projectDirOrRel = {}, bool recursive = true) const;

    QString toRelativePath(const QString& path) const;
    QString toAbsolutePath(const QString& relOrAbsPath) const;

    QString buildEffectiveSystemContext(const QString& filePath) const;
    QString buildAggregatedFilesDigest(const QString& projectDirOrRel = {}) const;
    QString buildProjectCustomPromptContext(ProjectCustomPromptContextMode mode,
                                            const QStringList& selectedFiles,
                                            const QMap<QString, QString>& openBuffers,
                                            int* outIncludedFiles = nullptr) const;

    static QString computeSha256(const QByteArray& data);

signals:
    void projectLoaded(const QString& rootPath);
    void projectClosed();
    void fileContextChanged(const QString& relativePath);
    void fileRemovedFromProject(const QString& relativePath, const QString& absolutePath);
    void fileRenamedInProject(const QString& oldRelativePath,
                              const QString& newRelativePath,
                              const QString& newAbsolutePath);
    void projectContextChanged();
    void subprojectsChanged();

private:
    struct ProjectNode {
        QString rootPath;
        QString aideFilePath;
        QString relPathFromRoot; // "" для корневого проекта
        QString projectSystemPrompt;
        QString projectSummary;
        QString projectSummaryLlm;
        QStringList ignoredDirs = {".git", "build", "__pycache__", "node_modules", ".aide"};
        bool projectSummaryDirty = false;
        QMap<QString, FileContextData> files; // Ключ: путь относительно rootPath этого узла
    };

    static QString defaultAideFilePathForDir(const QString& dirPath);
    static bool loadNodeFromDisk(const QString& aideFilePath, ProjectNode& outNode);
    static bool saveNodeToDisk(const ProjectNode& node);

    QString normalizeNodeKey(const QString& projectDirOrRel) const;
    ProjectNode* nodeForKey(const QString& nodeKey);
    const ProjectNode* nodeForKey(const QString& nodeKey) const;

    QString findOwningNodeKeyForAbsPath(const QString& absPath) const;
    QString findParentNodeKey(const QString& childNodeKey) const;
    void markNodeAndAncestorsDirty(const QString& nodeKey);
    void rebuildFileWatchers();
    bool isSkippedSystemDir(const QString& dirName) const;

    ProjectNode m_rootNode;
    QMap<QString, ProjectNode> m_subprojects; // Ключ: relPathFromRoot (например, "libs/core")
    QFileSystemWatcher m_fsWatcher;
    QTimer m_reconcileTimer;
    bool m_reconciling = false;
};
