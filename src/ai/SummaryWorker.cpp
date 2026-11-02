// src/ai/SummaryWorker.cpp
#include "SummaryWorker.h"
#include "../core/ProjectManager.h"
#include "../core/ConfigManager.h"
#include <QDir>
#include <QFileInfo>

SummaryWorker::SummaryWorker(ProjectManager* projMgr, AiTaskQueue* queue, QObject* parent)
    : QObject(parent), m_projectMgr(projMgr), m_taskQueue(queue) {
    connect(&m_projectRebuildTimer, &QTimer::timeout, this, &SummaryWorker::onPeriodicTimerTimeout);
    connect(m_taskQueue, &AiTaskQueue::taskStarted, this, &SummaryWorker::onTaskStarted);
    connect(m_taskQueue, &AiTaskQueue::taskStreamChunk, this, &SummaryWorker::onTaskStreamChunk);
    connect(m_taskQueue, &AiTaskQueue::taskCompleted, this, &SummaryWorker::onTaskCompleted);
    connect(m_taskQueue, &AiTaskQueue::taskFailed, this, &SummaryWorker::onTaskFailed);
    connect(m_taskQueue, &AiTaskQueue::taskCancelled, this, &SummaryWorker::onTaskCancelled);
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
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;

    // Проверяем, что файл действительно принадлежит папке открытого проекта
    QString cleanRoot = QDir::cleanPath(m_projectMgr->projectRootPath());
    QString cleanFile = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    if (!cleanFile.startsWith(cleanRoot + "/", Qt::CaseInsensitive) &&
        cleanFile.compare(cleanRoot, Qt::CaseInsensitive) != 0) {
        return;
    }

    if (ConfigManager::instance().aiBehavior().autoSummaryMode == AutoSummaryMode::None) {
        return;
    }

    if (m_projectMgr->hasFileSummary(filePath)) {
        return;
    }

    if (!ConfigManager::instance().shouldGenerateSummaryForFile(filePath)) {
        return;
    }

    QString relPath = m_projectMgr->toRelativePath(filePath);
    if (m_taskQueue->hasPendingOrActiveTask(TaskTargetType::FileSummary, relPath)) {
        return;
    }

    const auto& cfg = ConfigManager::instance().aiConfig();
    if (m_projectMgr->isFileSummaryOutdated(filePath, content, cfg.llmIdentifier())) {
        forceRebuildFileSummary(filePath, content);
    }
}

void SummaryWorker::forceRebuildFileSummary(const QString& filePath,
                                            const QByteArray& content,
                                            int maxOutputTokensOverride,
                                            int contextWindowOverride) {
    if (filePath.isEmpty() || content.trimmed().isEmpty()) return;
    const auto& cfg = ConfigManager::instance().aiConfig();
    QString relPath = m_projectMgr->toRelativePath(filePath);
    QString sha256 = ProjectManager::computeSha256(content);
    QString llmModel = cfg.llmIdentifier();

    m_projectMgr->setFileSummaryPending(relPath, true, sha256, llmModel);

    AiTask task;
    task.priority = TaskPriority::Low_BackgroundSummary;
    task.targetType = TaskTargetType::FileSummary;
    task.filePath = relPath;
    task.fileSha256 = sha256;
    task.llmModel = llmModel;
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = m_projectMgr->projectSystemPrompt();
    task.userPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                      + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(content));
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::continueFileSummary(const QString& filePath,
                                        const QByteArray& content,
                                        const QString& followUpInstruction,
                                        int maxOutputTokensOverride,
                                        int contextWindowOverride) {
    if (filePath.isEmpty() || content.trimmed().isEmpty()) return;
    const auto& cfg = ConfigManager::instance().aiConfig();
    QString relPath = m_projectMgr->toRelativePath(filePath);
    auto fc = m_projectMgr->fileContext(relPath);
    if (fc.aiSummary.trimmed().isEmpty()) {
        forceRebuildFileSummary(filePath, content, maxOutputTokensOverride, contextWindowOverride);
        return;
    }

    QString sha256 = ProjectManager::computeSha256(content);
    QString llmModel = cfg.llmIdentifier();
    m_projectMgr->setFileSummaryPending(relPath, true, sha256, llmModel);

    QString initialUserPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                                + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(content));

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = TaskTargetType::FileSummary;
    task.filePath = relPath;
    task.fileSha256 = sha256;
    task.llmModel = llmModel;
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = m_projectMgr->projectSystemPrompt();
    task.conversationHistory = {
        {"user", initialUserPrompt},
        {"assistant", fc.aiSummary}
    };
    task.userPrompt = followUpInstruction.trimmed().isEmpty()
                          ? QStringLiteral("Continue and complete the technical summary from the exact point where it stopped without repeating already written sections.")
                          : followUpInstruction.trimmed();
    task.isContinuation = true;
    task.appendToPreviousOutput = true;
    task.previousOutput = fc.aiSummary;
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::forceRebuildProjectSummary(int maxOutputTokensOverride,
                                               int contextWindowOverride) {
    QString digest = m_projectMgr->buildAggregatedFilesDigest();
    if (digest.trimmed().isEmpty()) return;

    const auto& cfg = ConfigManager::instance().aiConfig();

    AiTask task;
    task.priority = TaskPriority::Low_BackgroundSummary;
    task.targetType = TaskTargetType::ProjectSummary;
    task.filePath = "__PROJECT_SUMMARY__";
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = m_projectMgr->projectSystemPrompt();
    task.userPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::continueProjectSummary(const QString& followUpInstruction,
                                           int maxOutputTokensOverride,
                                           int contextWindowOverride) {
    QString existingSummary = m_projectMgr->projectSummary();
    if (existingSummary.trimmed().isEmpty()) {
        forceRebuildProjectSummary(maxOutputTokensOverride, contextWindowOverride);
        return;
    }

    QString digest = m_projectMgr->buildAggregatedFilesDigest();
    const auto& cfg = ConfigManager::instance().aiConfig();
    QString initialUserPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = TaskTargetType::ProjectSummary;
    task.filePath = "__PROJECT_SUMMARY__";
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = m_projectMgr->projectSystemPrompt();
    task.conversationHistory = {
        {"user", initialUserPrompt},
        {"assistant", existingSummary}
    };
    task.userPrompt = followUpInstruction.trimmed().isEmpty()
                          ? QStringLiteral("Continue and complete the project architectural summary from the exact point where it stopped without repeating previous text.")
                          : followUpInstruction.trimmed();
    task.isContinuation = true;
    task.appendToPreviousOutput = true;
    task.previousOutput = existingSummary;
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::onPeriodicTimerTimeout() {
    if (ConfigManager::instance().aiBehavior().autoSummaryMode != AutoSummaryMode::FilesAndProject)
        return;
    if (m_projectMgr->isProjectSummaryDirty() && m_taskQueue->isIdle())
        forceRebuildProjectSummary();
}

void SummaryWorker::onTaskStarted(const AiTask& task) {
    if (task.targetType == TaskTargetType::FileSummary) {
        emit summaryStreamingReset(task.filePath, false);
        if (task.appendToPreviousOutput && !task.previousOutput.isEmpty()) {
            emit summaryStreamingChunk(task.filePath, false, task.previousOutput + "\n");
        }
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        emit summaryStreamingReset({}, true);
        if (task.appendToPreviousOutput && !task.previousOutput.isEmpty()) {
            emit summaryStreamingChunk({}, true, task.previousOutput + "\n");
        }
    }
}

void SummaryWorker::onTaskStreamChunk(const AiTask& task, const QString& chunk) {
    if (task.targetType == TaskTargetType::FileSummary)
        emit summaryStreamingChunk(task.filePath, false, chunk);
    else if (task.targetType == TaskTargetType::ProjectSummary)
        emit summaryStreamingChunk({}, true, chunk);
}

void SummaryWorker::onTaskCompleted(const AiTask& task, const QString& resultText) {
    if (task.targetType == TaskTargetType::FileSummary) {
        QString finalSummary = (task.appendToPreviousOutput && !task.previousOutput.isEmpty())
        ? (task.previousOutput + "\n" + resultText)
        : resultText;
        m_projectMgr->updateFileSummary(task.filePath, task.fileSha256, task.llmModel, finalSummary);
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        QString finalSummary = (task.appendToPreviousOutput && !task.previousOutput.isEmpty())
        ? (task.previousOutput + "\n" + resultText)
        : resultText;
        m_projectMgr->setProjectSummary(finalSummary, task.llmModel);
    }
}

void SummaryWorker::onTaskFailed(const AiTask& task, const QString&) {
    if (task.targetType == TaskTargetType::FileSummary) {
        m_projectMgr->setFileSummaryPending(task.filePath, false);
        emit summaryStreamingReset(task.filePath, false);
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        emit summaryStreamingReset({}, true);
    }
}

void SummaryWorker::onTaskCancelled(const AiTask& task) {
    if (task.targetType == TaskTargetType::FileSummary) {
        m_projectMgr->setFileSummaryPending(task.filePath, false);
        emit summaryStreamingReset(task.filePath, false);
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        emit summaryStreamingReset({}, true);
    }
}
