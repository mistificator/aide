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

    // Если саммари намеренно очищено (Clear) и хэш файла не менялся — не пересчитываем автоматически
    if (fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && fc.sha256 == currentSha) {
        return;
    }

    // Если есть готовое саммари с тем же SHA-256 — повторно не пересчитываем
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
    QString relPath = m_projectMgr->toRelativePath(filePath);
    QString ownerSubRel = m_projectMgr->owningProjectRelPathForFile(filePath);
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
    task.systemPrompt = buildSystemPromptForNode(ownerSubRel);
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
    if (filePath.isEmpty() || content.trimmed().isEmpty() || !m_projectMgr) return;

    const auto& cfg = ConfigManager::instance().aiConfig();
    QString relPath = m_projectMgr->toRelativePath(filePath);
    auto fc = m_projectMgr->fileContext(relPath);
    if (fc.aiSummary.trimmed().isEmpty()) {
        forceRebuildFileSummary(filePath, content, maxOutputTokensOverride, contextWindowOverride);
        return;
    }

    QString ownerSubRel = m_projectMgr->owningProjectRelPathForFile(filePath);
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

void SummaryWorker::forceRebuildProjectSummary(int maxOutputTokensOverride,
                                               int contextWindowOverride,
                                               const QString& projectDirOrRel) {
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;

    QString subRel;
    if (!projectDirOrRel.trimmed().isEmpty()) {
        QString absDir = m_projectMgr->projectDirPathForRel(projectDirOrRel);
        subRel = m_projectMgr->toRelativePath(absDir);
        if (subRel == ".") subRel.clear();
    }

    // Если у этого проекта/подпроекта есть прямые дочерние подпроекты, чьи саммари устарели или ещё не созданы,
    // сначала ставим в очередь их пересборку (снизу вверх), чтобы родительский проект получил актуальные саммари подпроектов
    const QStringList directChildren = m_projectMgr->directChildSubprojects(subRel);
    for (const QString& childSub : directChildren) {
        if (m_projectMgr->isProjectSummaryDirty(childSub) ||
            (m_projectMgr->projectSummary(childSub).trimmed().isEmpty() && m_projectMgr->indexedFilesCount(childSub, true) > 0)) {
            const QString childTaskKey = makeProjectSummaryTaskKey(childSub);
            if (!m_taskQueue->hasPendingOrActiveTask(TaskTargetType::ProjectSummary, childTaskKey)) {
                forceRebuildProjectSummary(maxOutputTokensOverride, contextWindowOverride, childSub);
            }
        }
    }

    QString digest = m_projectMgr->buildAggregatedFilesDigest(subRel);
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

void SummaryWorker::continueProjectSummary(const QString& followUpInstruction,
                                           int maxOutputTokensOverride,
                                           int contextWindowOverride,
                                           const QString& projectDirOrRel) {
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;

    QString subRel;
    if (!projectDirOrRel.trimmed().isEmpty()) {
        QString absDir = m_projectMgr->projectDirPathForRel(projectDirOrRel);
        subRel = m_projectMgr->toRelativePath(absDir);
        if (subRel == ".") subRel.clear();
    }

    QString existingSummary = m_projectMgr->projectSummary(subRel);
    if (existingSummary.trimmed().isEmpty()) {
        forceRebuildProjectSummary(maxOutputTokensOverride, contextWindowOverride, subRel);
        return;
    }

    QString digest = m_projectMgr->buildAggregatedFilesDigest(subRel);
    const auto& cfg = ConfigManager::instance().aiConfig();
    QString initialUserPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;

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
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) return;
    if (ConfigManager::instance().aiBehavior().autoSummaryMode != AutoSummaryMode::FilesAndProject) return;
    if (!m_taskQueue->isIdle()) return;

    const QStringList dirtyNodes = m_projectMgr->dirtyProjectRelPathsBottomUp();
    for (const QString& subRel : dirtyNodes) {
        const QString digest = m_projectMgr->buildAggregatedFilesDigest(subRel);
        if (!digest.trimmed().isEmpty()) {
            forceRebuildProjectSummary(0, 0, subRel);
            return; // Запускаем по одному уровню снизу вверх; по завершении onTaskCompleted запустит родителя
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
        QString finalSummary = (task.appendToPreviousOutput && !task.previousOutput.isEmpty())
        ? (task.previousOutput + "\n" + resultText)
        : resultText;
        m_projectMgr->updateFileSummary(task.filePath, task.fileSha256, task.llmModel, finalSummary);

        // Если все задачи по файлам завершены и включён режим FilesAndProject —
        // запускаем каскадное построение саммари подпроектов/проекта снизу вверх
        if (ConfigManager::instance().aiBehavior().autoSummaryMode == AutoSummaryMode::FilesAndProject &&
            m_taskQueue->lowQueueTasks().isEmpty()) {
            const QString ownerSub = m_projectMgr->owningProjectRelPathForFile(task.filePath);
            if (m_projectMgr->projectSummary(ownerSub).trimmed().isEmpty() ||
                m_projectMgr->isProjectSummaryDirty(ownerSub)) {
                const QString taskKey = makeProjectSummaryTaskKey(ownerSub);
                if (!m_taskQueue->hasPendingOrActiveTask(TaskTargetType::ProjectSummary, taskKey)) {
                    forceRebuildProjectSummary(0, 0, ownerSub);
                }
            }
        }
    } else if (task.targetType == TaskTargetType::ProjectSummary) {
        const QString subRel = extractSubprojectRelFromTaskKey(task.filePath);
        QString finalSummary = (task.appendToPreviousOutput && !task.previousOutput.isEmpty())
                                   ? (task.previousOutput + "\n" + resultText)
                                   : resultText;
        m_projectMgr->setProjectSummary(finalSummary, task.llmModel, subRel);

        // Если завершился пересчёт вложенного подпроекта и очередь пуста,
        // при включённом режиме FilesAndProject переходим к следующему грязному родительскому уровню
        if (!subRel.isEmpty() &&
            ConfigManager::instance().aiBehavior().autoSummaryMode == AutoSummaryMode::FilesAndProject &&
            m_taskQueue->lowQueueTasks().isEmpty()) {
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
