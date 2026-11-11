// src/core/ProjectManager.h
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QDateTime>
#include <QTimer>
#include <functional>
#include "ConfigManager.h"

class QFileSystemWatcher;

struct FileContextData {
    QString relativePath;
    QString subprojectRelPath;
    QString sha256;
    QString llmModel;
    QString userPrompt;
    QString aiSummary;
    bool summaryCreated = false;
    bool isSummaryPending = false;
    QString pendingSha256;
    QString pendingLlmModel;
    QDateTime updatedAt;
};

class ProjectManager : public QObject {
    Q_OBJECT
public:
    using ProgressCallback = std::function<bool(int processedDirs, int foundFiles, const QString& currentDir)>;

    explicit ProjectManager(QObject* parent = nullptr);

    bool openProjectFolder(const QString& folderPath, const ProgressCallback& progressCb = nullptr);
    void closeProject();
    bool saveProjectFile();

    bool hasOpenProject() const { return !m_rootPath.isEmpty(); }
    QString projectRootPath() const { return m_rootPath; }
    QString projectFilePath() const;

    QString toRelativePath(const QString& filePathOrRel) const;
    QString toAbsolutePath(const QString& relPath) const;

    // Различие между любым подпроектом (виртуальным или .aide) и физическим файлом .aide на диске
    bool isSubprojectDir(const QString& dirPathOrRel) const;
    bool hasStandaloneAideFile(const QString& dirPathOrRel) const;
    void ensureSubprojectNode(const QString& dirPathOrRel);

    QStringList subprojectRelPaths() const;
    QStringList directChildSubprojects(const QString& parentSubRel = {}) const;
    QStringList dirtyProjectRelPathsBottomUp() const;
    QString owningProjectRelPathForFile(const QString& filePathOrRel) const;
    QString projectDirPathForRel(const QString& subprojectRel) const;

    bool createSubproject(const QString& dirPath);
    bool removeSubproject(const QString& dirPath);
    void reconcileProjectStructure(const ProgressCallback& progressCb = nullptr, bool fullDiskScan = false);

    QString projectSystemPrompt(const QString& subprojectRel = {}) const;
    void setProjectSystemPrompt(const QString& prompt, const QString& subprojectRel = {});

    QString projectSummary(const QString& subprojectRel = {}) const;
    QString projectSummaryLlm(const QString& subprojectRel = {}) const;
    void setProjectSummary(const QString& summary, const QString& llmModel = {}, const QString& subprojectRel = {});
    bool isProjectSummaryDirty(const QString& subprojectRel = {}) const;
    void markProjectSummaryDirty(const QString& subprojectRel = {}, bool cascadeToParents = true);

    int indexedFilesCount(const QString& subprojectRel = {}, bool includeNestedSubprojects = true) const;
    QStringList unindexedAllowedFilePaths(const QString& subprojectRel = {}, bool includeNestedSubprojects = false) const;

    FileContextData fileContext(const QString& filePathOrRel) const;
    bool isFileSummaryOutdated(const QString& filePathOrRel, const QByteArray& content, const QString& currentLlm = {}) const;

    void updateFileUserPrompt(const QString& filePathOrRel, const QString& prompt);
    void setFileSummaryPending(const QString& filePathOrRel,
                               bool pending,
                               const QString& pendingSha256 = {},
                               const QString& pendingLlm = {});
    void updateFileSummary(const QString& filePathOrRel,
                           const QString& sha256,
                           const QString& llmModel,
                           const QString& summary);
    void clearFileSummary(const QString& filePathOrRel, const QString& sha256 = {});
    void removeFileSummary(const QString& filePathOrRel);

    bool renameProjectFile(const QString& oldAbsPath, const QString& newAbsPath);
    bool deleteProjectFile(const QString& absPath);

    QString buildAggregatedFilesDigest(const QString& subprojectRel = {}) const;
    QString buildEffectiveSystemContext(const QString& filePath) const;
    QString buildProjectCustomPromptContext(ProjectCustomPromptContextMode mode,
                                            const QStringList& selectedFiles,
                                            const QMap<QString, QString>& openBuffers,
                                            int* outIncludedFiles = nullptr) const;

    void beginBatchUpdate();
    void endBatchUpdate();

    static QString computeSha256(const QByteArray& data);

signals:
    void projectLoaded(const QString& rootPath);
    void projectClosed();
    void fileContextChanged(const QString& relPath);
    void projectContextChanged();
    void subprojectsChanged();
    void fileRemovedFromProject(const QString& relPath, const QString& absPath);
    void fileRenamedInProject(const QString& oldRelPath, const QString& newRelPath, const QString& newAbsPath);

private:
    struct ProjectNodeData {
        QString subprojectRel; // "" для корня проекта
        QString aideFilePath;  // пусто для виртуального подпроекта; непусто для корня и отдельных .aide
        QString projectName;
        QString systemPrompt;
        QString summary;
        QString summaryLlm;
        bool summaryDirty = false;
        QDateTime updatedAt;

        bool isVirtual() const { return !subprojectRel.isEmpty() && aideFilePath.isEmpty(); }
    };

    QString normalizeScopeRel(const QString& scopeOrPath) const;
    QString parentSubprojectRel(const QString& subRel) const;
    QString owningStorageNodeForRelPath(const QString& relPath) const;
    void ensureVirtualSubprojectChain(const QString& dirRel);

    static QString defaultAideFilePathForDir(const QString& absDirPath);
    static QString findExistingAideFileInDir(const QString& absDirPath);

    bool loadNodeFromDisk(const QString& subRel,
                          const QString& aidePath,
                          QMap<QString, FileContextData>& outLoadedFiles,
                          QMap<QString, ProjectNodeData>& outLoadedVirtualNodes);
    bool saveNodeToDisk(const ProjectNodeData& storageNode) const;
    void rebuildWatcherPaths();
    void reassignAllFilesToNearestOwners();

    QString m_rootPath;
    QMap<QString, ProjectNodeData> m_nodes;     // ключ: subprojectRel ("" = корень, остальные = реальные или виртуальные папки)
    QMap<QString, FileContextData> m_files;     // ключ: relPath относительно m_rootPath
    QFileSystemWatcher* m_fsWatcher = nullptr;
    QTimer m_watchDebounceTimer;
    bool m_reconciling = false;
    int m_batchDepth = 0;
    bool m_batchProjectDirty = false;
    QString m_lastBatchChangedFileRel;
};
