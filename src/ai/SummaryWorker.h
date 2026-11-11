// src/ai/SummaryWorker.h
#pragma once
#include <QObject>
#include <QTimer>
#include <QMap>
#include "AiTaskQueue.h"

class ProjectManager;

struct BatchFileSummaryItem {
    QString filePath;
    QByteArray content;
};

class SummaryWorker : public QObject {
    Q_OBJECT
public:
    SummaryWorker(ProjectManager* projMgr, AiTaskQueue* queue, QObject* parent = nullptr);

public slots:
    void checkAndScheduleFileSummary(const QString& filePath, const QByteArray& content);
    void forceRebuildFileSummary(const QString& filePath,
                                 const QByteArray& content,
                                 int maxOutputTokensOverride = 0,
                                 int contextWindowOverride = 0);
    int scheduleFileSummariesBatch(const QVector<BatchFileSummaryItem>& items,
                                   int maxOutputTokensOverride = 0,
                                   int contextWindowOverride = 0);
    void continueFileSummary(const QString& filePath,
                             const QByteArray& content,
                             const QString& followUpInstruction = {},
                             int maxOutputTokensOverride = 0,
                             int contextWindowOverride = 0);

    void forceRebuildProjectSummary(int maxOutputTokensOverride = 0,
                                    int contextWindowOverride = 0,
                                    const QString& projectDirOrRel = {});
    void continueProjectSummary(const QString& followUpInstruction = {},
                                int maxOutputTokensOverride = 0,
                                int contextWindowOverride = 0,
                                const QString& projectDirOrRel = {});

signals:
    void summaryStreamingChunk(const QString& targetRelPath, bool isProjectSummary, const QString& chunk);
    void summaryStreamingReset(const QString& targetRelPath, bool isProjectSummary);

private slots:
    void onPeriodicTimerTimeout();
    void onTaskStarted(const AiTask& task);
    void onTaskStreamChunk(const AiTask& task, const QString& chunk);
    void onTaskCompleted(const AiTask& task, const QString& resultText);
    void onTaskFailed(const AiTask& task, const QString& errorText);
    void onTaskCancelled(const AiTask& task);

private:
    struct DeferredScopeLimits {
        int maxOutputTokens = 0;
        int contextWindow = 0;
    };

    void updateTimerInterval();
    void scheduleNextDirtyProjectSummaryIfIdle();
    void scheduleReadyDeferredProjectSummaries();
    bool areDependenciesReadyForScope(const QString& subRel) const;
    void enqueueProjectSummaryNow(const QString& subRel, int maxOutputTokensOverride, int contextWindowOverride);
    bool hasPendingFileSummariesForScope(const QString& subRel) const;
    QString buildSystemPromptForNode(const QString& subprojectRel) const;

    static QString makeProjectSummaryTaskKey(const QString& subprojectRel);
    static QString extractSubprojectRelFromTaskKey(const QString& taskFilePath);

    ProjectManager* m_projectMgr = nullptr;
    AiTaskQueue* m_taskQueue = nullptr;
    QTimer m_projectRebuildTimer;
    QMap<QString, DeferredScopeLimits> m_deferredProjectScopes;
};
