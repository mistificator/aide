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

private slots:
    void onPeriodicTimerTimeout();
    void onTaskCompleted(const AiTask& task, const QString& resultText);
    void onTaskFailed(const AiTask& task, const QString& errorText);

private:
    void updateTimerInterval();

    ProjectManager* m_projectMgr = nullptr;
    AiTaskQueue* m_taskQueue = nullptr;
    QTimer m_projectRebuildTimer;
};