// src/ai/SummaryWorker.h
#pragma once
#include <QObject>
#include <QTimer>
#include "AiTaskQueue.h"

class ProjectManager;

class SummaryWorker : public QObject {
    Q_OBJECT
public:
    SummaryWorker(ProjectManager* projMgr, AiTaskQueue* queue, QObject* parent = nullptr);

public slots:
    void checkAndScheduleFileSummary(const QString& filePath, const QByteArray& content);
    void forceRebuildFileSummary(const QString& filePath, const QByteArray& content);
    void forceRebuildProjectSummary();

signals:
    void summaryStreamingChunk(const QString& filePath, bool isProjectSummary, const QString& chunk);
    void summaryStreamingReset(const QString& filePath, bool isProjectSummary);

private slots:
    void onPeriodicTimerTimeout();
    void onTaskStarted(const AiTask& task);
    void onTaskStreamChunk(const AiTask& task, const QString& chunk);
    void onTaskCompleted(const AiTask& task, const QString& resultText);
    void onTaskFailed(const AiTask& task, const QString& errorText);
    void onTaskCancelled(const AiTask& task);

private:
    void updateTimerInterval();

    ProjectManager* m_projectMgr = nullptr;
    AiTaskQueue* m_taskQueue = nullptr;
    QTimer m_projectRebuildTimer;
};
