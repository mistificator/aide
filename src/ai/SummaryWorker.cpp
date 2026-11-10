// src/ai/SummaryWorker.cpp
#include "SummaryWorker.h"
#include "../core/ProjectManager.h"
#include "../core/ConfigManager.h"
#include <QDir>
#include <QFileInfo>

namespace {
constexpr const char* kRootProjectTaskKey = "__PROJECT_SUMMARY__";
constexpr const char* kSubprojectTaskPrefix = "__PROJECT_SUMMARY__:";
} // namespace

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

    if (m_projectMgr) {
        connect(m_projectMgr, &ProjectManager::fileRemovedFromProject, this,
                [this](const QString& relPath, const QString&) {
                    if (!relPath.isEmpty()) {
                        m_taskQueue->cancelFileSummaryTask(relPath);
                        emit summaryStreamingReset(relPath, false);
                    }
                });
        connect(m_projectMgr, &ProjectManager::fileRenamedInProject, this,
                [this](const QString& oldRelPath, const QString&, const QString&) {
                    if (!oldRelPath.isEmpty()) {
                        m_taskQueue->cancelFileSummaryTask(oldRelPath);
                        emit summaryStreamingReset(oldRelPath, false);
                    }
                });
        connect(m_projectMgr, &ProjectManager::projectClosed, this, [this]() {
            m_deferredProjectScopes.clear();
        });
    }

    updateTimerInterval();
}

QString SummaryWorker::makeProjectSummaryTaskKey(const QString& subprojectRel) {
    const QString clean = subprojectRel.trimmed();
    if (clean.isEmpty()) {
        return QString::fromLatin1(kRootProjectTaskKey);
    }
    return QString::fromLatin1(kSubprojectTaskPrefix) + clean;
}

QString SummaryWorker::extractSubprojectRelFromTaskKey(const QString& taskFilePath) {
    const QString prefix = QString::fromLatin1(kSubprojectTaskPrefix);
    if (taskFilePath.startsWith(prefix)) {
        return taskFilePath.mid(prefix.length());
    }
    return {};
}

QString SummaryWorker::buildSystemPromptForNode(const QString& subprojectRel) const {
    if (!m_projectMgr) return {};
    QStringList parts;
    const QString rootSys = m_projectMgr->projectSystemPrompt().trimmed();
    if (!rootSys.isEmpty()) {
        parts << rootSys;
    }
    if (!subprojectRel.isEmpty()) {
        const QString subSys = m_projectMgr->projectSystemPrompt(subprojectRel).trimmed();
        if (!subSys.isEmpty() && subSys != rootSys) {
            parts << QString("Subproject (%1) instructions:\n%2").arg(subprojectRel, subSys);
        }
    }
    return parts.join("\n\n");
}

void SummaryWorker::updateTimerInterval() {
    int mins = qMax(1, ConfigManager::instance().aiConfig().projectSummaryIntervalMin);
    m_projectRebuildTimer.start(mins * 60 * 1000);
}

bool SummaryWorker::hasPendingFileSummariesForScope(const QString& subRel) const {
    if (!m_taskQueue) return false;
    const QString prefix = subRel.isEmpty() ? QString() : (subRel + '/');

    auto matchesScope = [&](const AiTask& t) -> bool {
        if (t.targetType != TaskTargetType::FileSummary) return false;
        if (subRel.isEmpty()) return true;
        return t.filePath.startsWith(prefix, Qt::CaseInsensitive) ||
               (m_projectMgr && m_projectMgr->owningProjectRelPathForFile(t.filePath).compare(subRel, Qt::CaseInsensitive) == 0);
    };

    if (m_taskQueue->isProcessing() && matchesScope(m_taskQueue->currentTask())) {
        return true;
    }
    for (const AiTask& t : m_taskQueue->lowQueueTasks()) {
        if (matchesScope(t)) return true;
    }
    for (const AiTask& t : m_taskQueue->highQueueTasks()) {
        if (matchesScope(t)) return true;
    }
    return false;
}

void SummaryWorker::checkAndScheduleFileSummary(const QString& filePath, const QByteArray& content) {
    if (filePath.isEmpty() || content.trimmed().isEmpty()) return;
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;

    if (ConfigManager::instance().aiBehavior().autoSummaryMode == AutoSummaryMode::None) {
        return;
    }

    QString cleanRoot = QDir::cleanPath(m_projectMgr->projectRootPath());
    QString cleanFile = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    if (!cleanFile.startsWith(cleanRoot + "/", Qt::CaseInsensitive) &&
        cleanFile.compare(cleanRoot, Qt::CaseInsensitive) != 0) {
        return;
    }

    if (!ConfigManager::instance().shouldGenerateSummaryForFile(filePath)) {
        return;
    }

    QString relPath = m_projectMgr->toRelativePath(filePath);
    if (m_taskQueue->hasPendingOrActiveTask(TaskTargetType::FileSummary, relPath)) {
        return;
    }

    const auto fc = m_projectMgr->fileContext(relPath);
    const QString currentSha = ProjectManager::computeSha256(content);

    if (fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && fc.sha256 == currentSha) {
        return;
    }
    if (!fc.aiSummary.trimmed().isEmpty() && !fc.sha256.isEmpty() && fc.sha256 == currentSha) {
        return;
    }

    forceRebuildFileSummary(filePath, content);
}

void SummaryWorker::forceRebuildFileSummary(const QString& filePath,
                                            const QByteArray& content,
                                            int maxOutputTokensOverride,
                                            int contextWindowOverride) {
    if (filePath.isEmpty() || content.trimmed().isEmpty() || !m_projectMgr) return;

    const auto& cfg = ConfigManager::instance().aiConfig();
    const QString relPath = m_projectMgr->toRelativePath(filePath);
    const QString ownerSubRel = m_projectMgr->owningProjectRelPathForFile(relPath);
    const QString sha256 = ProjectManager::computeSha256(content);
    const QString llmModel = cfg.llmIdentifier();

    m_projectMgr->setFileSummaryPending(relPath, true, sha256, llmModel);

    AiTask task;
    task.priority = TaskPriority::Low_BackgroundSummary;
    task.targetType = TaskTargetType::FileSummary;
    task.filePath = relPath;
    task.fileSha256 = sha256;
    task.llmModel = llmModel;
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = buildSystemPromptForNode(ownerSubRel);
    task.userPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                      + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(content));
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

int SummaryWorker::scheduleFileSummariesBatch(const QVector<BatchFileSummaryItem>& items,
                                              int maxOutputTokensOverride,
                                              int contextWindowOverride) {
    if (items.isEmpty() || !m_projectMgr || !m_taskQueue) return 0;

    const auto& cfg = ConfigManager::instance().aiConfig();
    const QString llmModel = cfg.llmIdentifier();
    const QString tmpl = ConfigManager::instance().promptTemplate("FileSummary");
    const int maxTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    const int ctxWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;

    QVector<AiTask> tasksToEnqueue;
    tasksToEnqueue.reserve(items.size());

    m_projectMgr->beginBatchUpdate();
    for (const BatchFileSummaryItem& item : items) {
        if (item.filePath.isEmpty() || item.content.trimmed().isEmpty()) continue;

        const QString relPath = m_projectMgr->toRelativePath(item.filePath);
        if (relPath.isEmpty()) continue;

        const QString ownerSubRel = m_projectMgr->owningProjectRelPathForFile(relPath);
        const QString sha256 = ProjectManager::computeSha256(item.content);

        m_projectMgr->setFileSummaryPending(relPath, true, sha256, llmModel);

        AiTask task;
        task.priority = TaskPriority::Low_BackgroundSummary;
        task.targetType = TaskTargetType::FileSummary;
        task.filePath = relPath;
        task.fileSha256 = sha256;
        task.llmModel = llmModel;
        task.maxOutputTokens = maxTokens;
        task.contextWindow = ctxWindow;
        task.systemPrompt = buildSystemPromptForNode(ownerSubRel);
        task.userPrompt = tmpl + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(item.content));
        task.stream = true;

        tasksToEnqueue.append(std::move(task));
    }
    m_projectMgr->endBatchUpdate();

    if (!tasksToEnqueue.isEmpty()) {
        m_taskQueue->enqueueTasksBatch(tasksToEnqueue);
    }
    return tasksToEnqueue.size();
}

void SummaryWorker::continueFileSummary(const QString& filePath,
                                        const QByteArray& content,
                                        const QString& followUpInstruction,
                                        int maxOutputTokensOverride,
                                        int contextWindowOverride) {
    if (filePath.isEmpty() || content.trimmed().isEmpty() || !m_projectMgr) return;

    const auto& cfg = ConfigManager::instance().aiConfig();
    const QString relPath = m_projectMgr->toRelativePath(filePath);
    const auto fc = m_projectMgr->fileContext(relPath);
    if (fc.aiSummary.trimmed().isEmpty()) {
        forceRebuildFileSummary(filePath, content, maxOutputTokensOverride, contextWindowOverride);
        return;
    }

    const QString ownerSubRel = m_projectMgr->owningProjectRelPathForFile(relPath);
    const QString sha256 = ProjectManager::computeSha256(content);
    const QString llmModel = cfg.llmIdentifier();
    m_projectMgr->setFileSummaryPending(relPath, true, sha256, llmModel);

    const QString initialUserPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                                      + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(content));

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = TaskTargetType::FileSummary;
    task.filePath = relPath;
    task.fileSha256 = sha256;
    task.llmModel = llmModel;
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = buildSystemPromptForNode(ownerSubRel);
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

void SummaryWorker::enqueueProjectSummaryNow(const QString& subRel,
                                             int maxOutputTokensOverride,
                                             int contextWindowOverride) {
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;
    const QString digest = m_projectMgr->buildAggregatedFilesDigest(subRel);
    if (digest.trimmed().isEmpty()) return;

    const auto& cfg = ConfigManager::instance().aiConfig();
    const QString taskKey = makeProjectSummaryTaskKey(subRel);

    AiTask task;
    task.priority = TaskPriority::Low_BackgroundSummary;
    task.targetType = TaskTargetType::ProjectSummary;
    task.filePath = taskKey;
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = buildSystemPromptForNode(subRel);
    task.userPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void SummaryWorker::forceRebuildProjectSummary(int maxOutputTokensOverride,
                                               int contextWindowOverride,
                                               const QString& projectDirOrRel) {
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;

    QString subRel;
    if (!projectDirOrRel.trimmed().isEmpty()) {
        const QString absDir = m_projectMgr->projectDirPathForRel(projectDirOrRel);
        subRel = m_projectMgr->toRelativePath(absDir);
        if (subRel == ".") subRel.clear();
    }

    // 1. Проверяем прямые дочерние подпроекты: если их саммари устарели или ещё не созданы,
    // сначала запускаем их пересборку, а текущий родительский узел откладываем в m_deferredProjectScopes,
    // чтобы его дайджест был собран ПОСЛЕ готовности всех дочерних подпроектов и файлов.
    bool waitingForDependencies = hasPendingFileSummariesForScope(subRel);
    const QStringList directChildren = m_projectMgr->directChildSubprojects(subRel);
    for (const QString& childSub : directChildren) {
        const QString childTaskKey = makeProjectSummaryTaskKey(childSub);
        const bool childPending = m_taskQueue->hasPendingOrActiveTask(TaskTargetType::ProjectSummary, childTaskKey) ||
                                  hasPendingFileSummariesForScope(childSub);
        const bool childDirty = m_projectMgr->isProjectSummaryDirty(childSub) ||
                                (m_projectMgr->projectSummary(childSub).trimmed().isEmpty() &&
                                 m_projectMgr->indexedFilesCount(childSub, true) > 0);
        if (childPending || childDirty) {
            waitingForDependencies = true;
            if (!m_taskQueue->hasPendingOrActiveTask(TaskTargetType::ProjectSummary, childTaskKey)) {
                forceRebuildProjectSummary(maxOutputTokensOverride, contextWindowOverride, childSub);
            }
        }
    }

    if (waitingForDependencies) {
        m_projectMgr->markProjectSummaryDirty(subRel, false);
        m_deferredProjectScopes.insert(subRel, {maxOutputTokensOverride, contextWindowOverride});
        return;
    }

    m_deferredProjectScopes.remove(subRel);
    enqueueProjectSummaryNow(subRel, maxOutputTokensOverride, contextWindowOverride);
}

void SummaryWorker::continueProjectSummary(const QString& followUpInstruction,
                                           int maxOutputTokensOverride,
                                           int contextWindowOverride,
                                           const QString& projectDirOrRel) {
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;

    QString subRel;
    if (!projectDirOrRel.trimmed().isEmpty()) {
        const QString absDir = m_projectMgr->projectDirPathForRel(projectDirOrRel);
        subRel = m_projectMgr->toRelativePath(absDir);
        if (subRel == ".") subRel.clear();
    }

    const QString existingSummary = m_projectMgr->projectSummary(subRel);
    if (existingSummary.trimmed().isEmpty()) {
        forceRebuildProjectSummary(maxOutputTokensOverride, contextWindowOverride, subRel);
        return;
    }

    const QString digest = m_projectMgr->buildAggregatedFilesDigest(subRel);
    const auto& cfg = ConfigManager::instance().aiConfig();
    const QString initialUserPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = TaskTargetType::ProjectSummary;
    task.filePath = makeProjectSummaryTaskKey(subRel);
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = maxOutputTokensOverride > 0 ? maxOutputTokensOverride : cfg.maxOutputSummaryTokens;
    task.contextWindow = contextWindowOverride > 0 ? contextWindowOverride : cfg.contextWindow;
    task.systemPrompt = buildSystemPromptForNode(subRel);
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

void SummaryWorker::scheduleNextDirtyProjectSummaryIfIdle() {
    if (!m_projectMgr || !m_projectMgr->hasOpenProject() || !m_taskQueue) return;
    if (!m_taskQueue->isIdle()) return;

    // 1. Сначала проверяем явно запрошенные отложенные узлы снизу вверх
    if (!m_deferredProjectScopes.isEmpty()) {
        QStringList keys = m_deferredProjectScopes.keys();
        std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
            int depthA = a.isEmpty() ? 0 : (a.count('/') + 1);
            int depthB = b.isEmpty() ? 0 : (b.count('/') + 1);
            if (depthA != depthB) return depthA > depthB;
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        const QString nextSub = keys.first();
        const DeferredScopeLimits lim = m_deferredProjectScopes.take(nextSub);
        enqueueProjectSummaryNow(nextSub, lim.maxOutputTokens, lim.contextWindow);
        return;
    }

    // 2. Автоматическая пересборка грязных уровней снизу вверх в режиме FilesAndProject
    if (ConfigManager::instance().aiBehavior().autoSummaryMode != AutoSummaryMode::FilesAndProject) {
        return;
    }

    const QStringList dirtyNodes = m_projectMgr->dirtyProjectRelPathsBottomUp();
    for (const QString& subRel : dirtyNodes) {
        // Не запускаем авто-синтез саммари подпроекта по одному случайно открытому файлу,
        // если в этом подпроекте есть другие разрешённые файлы, которые ещё вообще не индексировались
        const int indexedDirect = m_projectMgr->indexedFilesCount(subRel, false);
        const QStringList unindexedDirect = m_projectMgr->unindexedAllowedFilePaths(subRel, false);
        if (indexedDirect <= 1 && !unindexedDirect.isEmpty()) {
            continue;
        }
        const QString digest = m_projectMgr->buildAggregatedFilesDigest(subRel);
        if (!digest.trimmed().isEmpty()) {
            enqueueProjectSummaryNow(subRel, 0, 0);
            return;
        }
    }
}

void SummaryWorker::onPeriodicTimerTimeout() {
    scheduleNextDirtyProjectSummaryIfIdle();
}

void SummaryWorker::onTaskStarted(const AiTask& task) {
    if (task.targetType == TaskTargetType::FileSummary) {
        emit summaryStreamingReset(task.filePath, false);
        if (task.appendToPreviousOutput && !task.previousOutput.isEmpty()) {
            emit summaryStreamingChunk(task.filePath, false, task.previousOutput + "\n");
        }
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        const QString subRel = extractSubprojectRelFromTaskKey(task.filePath);
        emit summaryStreamingReset(subRel, true);
        if (task.appendToPreviousOutput && !task.previousOutput.isEmpty()) {
            emit summaryStreamingChunk(subRel, true, task.previousOutput + "\n");
        }
    }
}

void SummaryWorker::onTaskStreamChunk(const AiTask& task, const QString& chunk) {
    if (task.targetType == TaskTargetType::FileSummary) {
        emit summaryStreamingChunk(task.filePath, false, chunk);
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        const QString subRel = extractSubprojectRelFromTaskKey(task.filePath);
        emit summaryStreamingChunk(subRel, true, chunk);
    }
}

void SummaryWorker::onTaskCompleted(const AiTask& task, const QString& resultText) {
    if (task.targetType == TaskTargetType::FileSummary) {
        const QString finalSummary = (task.appendToPreviousOutput && !task.previousOutput.isEmpty())
        ? (task.previousOutput + "\n" + resultText)
        : resultText;
        m_projectMgr->updateFileSummary(task.filePath, task.fileSha256, task.llmModel, finalSummary);

        if (m_taskQueue->lowQueueTasks().isEmpty()) {
            QTimer::singleShot(0, this, &SummaryWorker::scheduleNextDirtyProjectSummaryIfIdle);
        }
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        const QString subRel = extractSubprojectRelFromTaskKey(task.filePath);
        const QString finalSummary = (task.appendToPreviousOutput && !task.previousOutput.isEmpty())
                                         ? (task.previousOutput + "\n" + resultText)
                                         : resultText;
        m_projectMgr->setProjectSummary(finalSummary, task.llmModel, subRel);

        if (m_taskQueue->lowQueueTasks().isEmpty()) {
            QTimer::singleShot(0, this, &SummaryWorker::scheduleNextDirtyProjectSummaryIfIdle);
        }
    }
}

void SummaryWorker::onTaskFailed(const AiTask& task, const QString&) {
    if (task.targetType == TaskTargetType::FileSummary) {
        m_projectMgr->setFileSummaryPending(task.filePath, false);
        emit summaryStreamingReset(task.filePath, false);
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        const QString subRel = extractSubprojectRelFromTaskKey(task.filePath);
        emit summaryStreamingReset(subRel, true);
    }
}

void SummaryWorker::onTaskCancelled(const AiTask& task) {
    if (task.targetType == TaskTargetType::FileSummary) {
        m_projectMgr->setFileSummaryPending(task.filePath, false);
        emit summaryStreamingReset(task.filePath, false);
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        const QString subRel = extractSubprojectRelFromTaskKey(task.filePath);
        emit summaryStreamingReset(subRel, true);
    }
}
