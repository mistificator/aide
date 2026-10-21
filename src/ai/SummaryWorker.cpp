// src/ai/SummaryWorker.cpp
#include "SummaryWorker.h"
#include "../core/ProjectManager.h"
#include "../core/ConfigManager.h"

SummaryWorker::SummaryWorker(ProjectManager* projMgr, AiTaskQueue* queue, QObject* parent)
    : QObject(parent), m_projectMgr(projMgr), m_taskQueue(queue) {
    connect(&m_projectRebuildTimer, &QTimer::timeout, this, &SummaryWorker::onPeriodicTimerTimeout);
    connect(m_taskQueue, &AiTaskQueue::taskCompleted, this, &SummaryWorker::onTaskCompleted);
    connect(m_taskQueue, &AiTaskQueue::taskFailed, this, &SummaryWorker::onTaskFailed);
    connect(&ConfigManager::instance(), &ConfigManager::aiConfigChanged, this, [this](const AiConnectionConfig&) {
        updateTimerInterval();
    });
    updateTimerInterval();
}

void SummaryWorker::updateTimerInterval() {
    int mins = qMax(1, ConfigManager::instance().aiConfig().projectSummaryIntervalMin);
    m_projectRebuildTimer.start(mins * 60 * 1000);
}

void SummaryWorker::checkAndScheduleFileSummary(const QString& filePath, const QByteArray& content) {
    if (filePath.isEmpty() || content.trimmed().isEmpty()) return;
    if (m_projectMgr->isFileSummaryOutdated(filePath, content))
        forceRebuildFileSummary(filePath, content);
}

void SummaryWorker::forceRebuildFileSummary(const QString& filePath, const QByteArray& content) {
    if (filePath.isEmpty() || content.trimmed().isEmpty()) return;
    QString relPath = m_projectMgr->toRelativePath(filePath);
    m_projectMgr->setFileSummaryPending(relPath, true);

    AiTask task;
    task.priority = TaskPriority::Low_BackgroundSummary;
    task.targetType = TaskTargetType::FileSummary;
    task.filePath = relPath;
    task.fileSha256 = ProjectManager::computeSha256(content);
    task.systemPrompt = m_projectMgr->projectSystemPrompt();
    task.userPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                    + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(content));
    task.stream = false;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::forceRebuildProjectSummary() {
    QString digest = m_projectMgr->buildAggregatedFilesDigest();
    if (digest.trimmed().isEmpty()) return;

    AiTask task;
    task.priority = TaskPriority::Low_BackgroundSummary;
    task.targetType = TaskTargetType::ProjectSummary;
    task.filePath = "__PROJECT_SUMMARY__";
    task.systemPrompt = m_projectMgr->projectSystemPrompt();
    task.userPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;
    task.stream = false;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::onPeriodicTimerTimeout() {
    if (m_projectMgr->isProjectSummaryDirty() && m_taskQueue->isIdle())
        forceRebuildProjectSummary();
}

void SummaryWorker::onTaskCompleted(const AiTask& task, const QString& resultText) {
    if (task.targetType == TaskTargetType::FileSummary)
        m_projectMgr->updateFileSummary(task.filePath, task.fileSha256, resultText);
    else if (task.targetType == TaskTargetType::ProjectSummary)
        m_projectMgr->setProjectSummary(resultText);
}

void SummaryWorker::onTaskFailed(const AiTask& task, const QString&) {
    if (task.targetType == TaskTargetType::FileSummary)
        m_projectMgr->setFileSummaryPending(task.filePath, false);
}