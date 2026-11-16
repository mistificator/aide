// src/ai/AiTaskQueue.cpp
#include "AiTaskQueue.h"
#include "AiProvider.h"
#include <QUuid>
#include <QTimer>
#include <QHash>

AiTaskQueue::AiTaskQueue(AiProvider* provider, QObject* parent)
    : QObject(parent), m_provider(provider) {
    connect(m_provider, &AiProvider::streamChunkReceived, this, &AiTaskQueue::onProviderChunk);
    connect(m_provider, &AiProvider::progressUpdated, this, &AiTaskQueue::onProviderProgress);
    connect(m_provider, &AiProvider::thinkingStateChanged, this, &AiTaskQueue::onProviderThinkingState);
    connect(m_provider, &AiProvider::requestTimedOut, this, &AiTaskQueue::onProviderTimedOut);
    connect(m_provider, &AiProvider::requestFinished, this, &AiTaskQueue::onProviderFinished);
    connect(m_provider, &AiProvider::requestError, this, &AiTaskQueue::onProviderError);
}

void AiTaskQueue::setLlmConnected(bool connected) {
    if (m_isLlmConnected == connected) return;
    m_isLlmConnected = connected;
    emitStatus();

    if (m_isLlmConnected && !m_isProcessing && (!m_highQueue.isEmpty() || !m_lowQueue.isEmpty())) {
        startBatchTimerIfNeeded();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    }
}

int AiTaskQueue::estimateExpectedTokens(const AiTask& task) {
    const int promptTok = (task.userPrompt.length() + 3) / 4;
    int est = 512;
    switch (task.targetType) {
    case TaskTargetType::FileSummary:
        est = qBound(192, promptTok / 4, 1024);
        break;
    case TaskTargetType::ProjectSummary:
        est = qBound(384, promptTok / 3, 2048);
        break;
    case TaskTargetType::ExplainToDialog:
    case TaskTargetType::ProjectCustomPrompt:
        est = 1024;
        break;
    case TaskTargetType::ModifyEditorCode:
        est = qBound(256, promptTok, 8192);
        break;
    }
    if (task.maxOutputTokens > 0) {
        est = qMin(est, task.maxOutputTokens);
    }
    return qMax(64, est);
}

void AiTaskQueue::prepareTaskDefaults(AiTask& task) const {
    if (task.taskId.isEmpty()) {
        task.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto& cfg = ConfigManager::instance().aiConfig();
    if (task.llmModel.isEmpty()) {
        task.llmModel = cfg.llmIdentifier();
    }
    if (task.maxOutputTokens <= 0) {
        const bool isSum = (task.targetType == TaskTargetType::FileSummary ||
                            task.targetType == TaskTargetType::ProjectSummary);
        task.maxOutputTokens = isSum ? cfg.maxOutputSummaryTokens : cfg.maxOutputCodeTokens;
    }
    if (task.contextWindow <= 0) {
        task.contextWindow = cfg.contextWindow;
    }
    task.expectedTokens = estimateExpectedTokens(task);
}

void AiTaskQueue::startBatchTimerIfNeeded() {
    if (!m_batchTimer.isValid()) {
        m_batchCompletedCount = 0;
        m_lastBatchElapsedMs = 0;
        m_batchTimer.start();
    }
}

void AiTaskQueue::finalizeBatchTimerIfIdle() {
    if (!m_isProcessing && m_highQueue.isEmpty() && m_lowQueue.isEmpty()) {
        if (m_batchTimer.isValid()) {
            m_lastBatchElapsedMs = m_batchTimer.elapsed();
            m_batchTimer.invalidate();
        }
    }
}

qint64 AiTaskQueue::totalBatchElapsedMs() const {
    if (m_batchTimer.isValid()) {
        return m_batchTimer.elapsed();
    }
    return m_lastBatchElapsedMs;
}

int AiTaskQueue::batchTotalCount() const {
    int active = m_isProcessing ? 1 : 0;
    return m_batchCompletedCount + active + m_highQueue.size() + m_lowQueue.size();
}

void AiTaskQueue::emitStatus() {
    emit queueStatusChanged(m_highQueue.size(), m_lowQueue.size(), m_isProcessing);
    emit queueChanged();
}

bool AiTaskQueue::hasPendingOrActiveTask(TaskTargetType targetType, const QString& filePath) const {
    if (m_isProcessing && m_currentTask.targetType == targetType &&
        m_currentTask.filePath.compare(filePath, Qt::CaseInsensitive) == 0) {
        return true;
    }
    for (const auto& t : m_highQueue) {
        if (t.targetType == targetType && t.filePath.compare(filePath, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    for (const auto& t : m_lowQueue) {
        if (t.targetType == targetType && t.filePath.compare(filePath, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString AiTaskQueue::enqueueTask(const AiTask& inputTask) {
    AiTask task = inputTask;
    prepareTaskDefaults(task);
    if (m_isLlmConnected) {
        startBatchTimerIfNeeded();
    }

    if (task.priority == TaskPriority::High_UserAction) {
        if (task.targetEditor) {
            QString waitMsg = !m_isLlmConnected
                                  ? "Waiting for LLM connection..."
                                  : ((m_isProcessing || !m_highQueue.isEmpty()) ? "Queued..." : "Generating...");
            task.targetEditor->setAiBusy(true, waitMsg);
        }
        m_highQueue.enqueue(task);
    } else {
        for (auto& existing : m_lowQueue) {
            if (existing.targetType == task.targetType &&
                existing.filePath.compare(task.filePath, Qt::CaseInsensitive) == 0) {
                existing.fileSha256 = task.fileSha256;
                existing.llmModel = task.llmModel;
                existing.maxOutputTokens = task.maxOutputTokens;
                existing.contextWindow = task.contextWindow;
                existing.systemPrompt = task.systemPrompt;
                existing.userPrompt = task.userPrompt;
                existing.expectedTokens = task.expectedTokens;
                emitStatus();
                return existing.taskId;
            }
        }
        m_lowQueue.enqueue(task);
    }

    emitStatus();
    if (!m_isProcessing && m_isLlmConnected) {
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    }
    return task.taskId;
}

void AiTaskQueue::enqueueTasksBatch(const QVector<AiTask>& inputTasks) {
    if (inputTasks.isEmpty()) return;
    if (m_isLlmConnected) {
        startBatchTimerIfNeeded();
    }

    QHash<QString, int> lowQueueIndexByKey;
    lowQueueIndexByKey.reserve(m_lowQueue.size() + inputTasks.size());
    for (int i = 0; i < m_lowQueue.size(); ++i) {
        const QString key = QString::number(static_cast<int>(m_lowQueue[i].targetType))
        + QLatin1Char(':') + m_lowQueue[i].filePath.toLower();
        lowQueueIndexByKey.insert(key, i);
    }

    for (const AiTask& rawTask : inputTasks) {
        AiTask task = rawTask;
        prepareTaskDefaults(task);

        if (task.priority == TaskPriority::High_UserAction) {
            if (task.targetEditor) {
                task.targetEditor->setAiBusy(
                    true, m_isLlmConnected ? "Queued..." : "Waiting for LLM connection...");
            }
            m_highQueue.enqueue(std::move(task));
        } else {
            const QString key = QString::number(static_cast<int>(task.targetType))
            + QLatin1Char(':') + task.filePath.toLower();
            auto it = lowQueueIndexByKey.find(key);
            if (it != lowQueueIndexByKey.end()) {
                AiTask& existing = m_lowQueue[it.value()];
                existing.fileSha256 = task.fileSha256;
                existing.llmModel = task.llmModel;
                existing.maxOutputTokens = task.maxOutputTokens;
                existing.contextWindow = task.contextWindow;
                existing.systemPrompt = task.systemPrompt;
                existing.userPrompt = task.userPrompt;
                existing.expectedTokens = task.expectedTokens;
            } else {
                const int newIdx = m_lowQueue.size();
                m_lowQueue.enqueue(std::move(task));
                lowQueueIndexByKey.insert(key, newIdx);
            }
        }
    }

    emitStatus();
    if (!m_isProcessing && m_isLlmConnected) {
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    }
}

void AiTaskQueue::cancelTaskForEditor(CodeEditor* editor) {
    if (!editor) return;
    QVector<AiTask> cancelledList;
    for (int i = m_highQueue.size() - 1; i >= 0; --i) {
        if (m_highQueue[i].targetEditor == editor) {
            cancelledList.append(m_highQueue.takeAt(i));
        }
    }
    for (const auto& t : cancelledList) {
        emit taskCancelled(t);
    }

    if (m_isProcessing && m_currentTask.targetEditor == editor) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        editor->cancelAndRollbackAiEdit();
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        finalizeBatchTimerIfIdle();
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    } else {
        editor->setAiBusy(false);
        finalizeBatchTimerIfIdle();
        emitStatus();
    }
}

void AiTaskQueue::cancelTaskById(const QString& taskId) {
    if (taskId.isEmpty()) return;

    if (m_isProcessing && m_currentTask.taskId == taskId) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        if (cancelled.targetEditor) {
            if (cancelled.targetType == TaskTargetType::ModifyEditorCode) {
                cancelled.targetEditor->cancelAndRollbackAiEdit();
            } else {
                cancelled.targetEditor->setAiBusy(false);
            }
        }
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        finalizeBatchTimerIfIdle();
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
        return;
    }

    for (int i = 0; i < m_highQueue.size(); ++i) {
        if (m_highQueue[i].taskId == taskId) {
            AiTask t = m_highQueue.takeAt(i);
            if (t.targetEditor) t.targetEditor->setAiBusy(false);
            emit taskCancelled(t);
            finalizeBatchTimerIfIdle();
            emitStatus();
            return;
        }
    }

    for (int i = 0; i < m_lowQueue.size(); ++i) {
        if (m_lowQueue[i].taskId == taskId) {
            AiTask t = m_lowQueue.takeAt(i);
            emit taskCancelled(t);
            finalizeBatchTimerIfIdle();
            emitStatus();
            return;
        }
    }
}

void AiTaskQueue::cancelFileSummaryTask(const QString& relFilePath) {
    if (relFilePath.isEmpty()) return;

    for (int i = m_lowQueue.size() - 1; i >= 0; --i) {
        if (m_lowQueue[i].targetType == TaskTargetType::FileSummary &&
            m_lowQueue[i].filePath.compare(relFilePath, Qt::CaseInsensitive) == 0) {
            AiTask t = m_lowQueue.takeAt(i);
            emit taskCancelled(t);
        }
    }

    if (m_isProcessing &&
        m_currentTask.targetType == TaskTargetType::FileSummary &&
        m_currentTask.filePath.compare(relFilePath, Qt::CaseInsensitive) == 0) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        finalizeBatchTimerIfIdle();
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
        return;
    }

    finalizeBatchTimerIfIdle();
    emitStatus();
}

void AiTaskQueue::cancelAllBackgroundTasks() {
    QQueue<AiTask> takenLow;
    takenLow.swap(m_lowQueue);
    for (const AiTask& t : takenLow) {
        emit taskCancelled(t);
    }

    if (m_isProcessing && m_currentTask.priority == TaskPriority::Low_BackgroundSummary) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        finalizeBatchTimerIfIdle();
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
        return;
    }

    finalizeBatchTimerIfIdle();
    emitStatus();
}

void AiTaskQueue::cancelAllTasks() {
    QQueue<AiTask> takenHigh;
    QQueue<AiTask> takenLow;
    takenHigh.swap(m_highQueue);
    takenLow.swap(m_lowQueue);

    for (const AiTask& t : takenHigh) {
        if (t.targetEditor) t.targetEditor->setAiBusy(false);
        emit taskCancelled(t);
    }
    for (const AiTask& t : takenLow) {
        emit taskCancelled(t);
    }

    if (m_isProcessing) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        if (cancelled.targetEditor) {
            if (cancelled.targetType == TaskTargetType::ModifyEditorCode) {
                cancelled.targetEditor->cancelAndRollbackAiEdit();
            } else {
                cancelled.targetEditor->setAiBusy(false);
            }
        }
        m_isProcessing = false;
        emit taskCancelled(cancelled);
    }

    finalizeBatchTimerIfIdle();
    emitStatus();
}

void AiTaskQueue::processNext() {
    // Не обрабатываем очередь, если уже идёт запрос или нет подтверждённого подключения к LLM
    if (m_isProcessing || !m_isLlmConnected) return;

    while (!m_highQueue.isEmpty()) {
        const AiTask& head = m_highQueue.head();
        const bool needsEditor = (head.targetType == TaskTargetType::ModifyEditorCode ||
                                  head.targetType == TaskTargetType::ExplainToDialog);
        if (needsEditor && !head.targetEditor) {
            m_highQueue.dequeue();
            continue;
        }
        break;
    }

    if (!m_highQueue.isEmpty()) {
        m_currentTask = m_highQueue.dequeue();
    } else if (!m_lowQueue.isEmpty()) {
        m_currentTask = m_lowQueue.dequeue();
    } else {
        finalizeBatchTimerIfIdle();
        emitStatus();
        return;
    }

    startBatchTimerIfNeeded();
    m_isProcessing = true;
    m_currentTask.isThinking = false;
    m_currentTask.generatedTokens = 0;
    m_currentTask.thinkingTokens = 0;
    m_currentTask.elapsedMs = 0;
    m_currentTask.tokensPerSec = 0.0;
    emitStatus();

    if (m_currentTask.targetEditor) {
        m_currentTask.targetEditor->setAiBusy(true, "Generating...");
        if (m_currentTask.targetType == TaskTargetType::ModifyEditorCode) {
            m_currentTask.targetEditor->beginAiStreamEdit();
        }
    }

    emit taskStarted(m_currentTask);

    AiConnectionConfig cfg = ConfigManager::instance().aiConfig();
    if (m_currentTask.contextWindow > 0) {
        cfg.contextWindow = m_currentTask.contextWindow;
    }

    m_provider->sendRequest(m_currentTask.taskId,
                            m_currentTask.systemPrompt,
                            m_currentTask.userPrompt,
                            m_currentTask.stream,
                            cfg,
                            m_currentTask.maxOutputTokens,
                            m_currentTask.conversationHistory);
}

void AiTaskQueue::onProviderChunk(const QString& taskId, const QString& chunk) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    if (m_currentTask.targetType == TaskTargetType::ModifyEditorCode && m_currentTask.targetEditor) {
        m_currentTask.targetEditor->appendAiStreamChunk(chunk);
    }
    emit taskStreamChunk(m_currentTask, chunk);
}

void AiTaskQueue::onProviderProgress(const QString& taskId,
                                     int outputChunks,
                                     int thinkingChunks,
                                     qint64 elapsedMs,
                                     double tokPerSec) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    m_currentTask.generatedTokens = outputChunks;
    m_currentTask.thinkingTokens = thinkingChunks;
    m_currentTask.elapsedMs = elapsedMs;
    m_currentTask.tokensPerSec = tokPerSec;
    emit taskProgressChanged(m_currentTask);
}

void AiTaskQueue::onProviderThinkingState(const QString& taskId, bool isThinking) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    m_currentTask.isThinking = isThinking;
    emit taskProgressChanged(m_currentTask);
}

void AiTaskQueue::onProviderTimedOut(const QString& taskId) {
    onProviderError(taskId, QStringLiteral("Request timed out."));
}

void AiTaskQueue::onProviderFinished(const QString& taskId, const QString& fullText) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    AiTask finished = m_currentTask;
    m_isProcessing = false;
    ++m_batchCompletedCount;

    if (finished.targetEditor) {
        if (finished.targetType == TaskTargetType::ModifyEditorCode) {
            finished.targetEditor->finishAiStreamEdit();
        } else {
            finished.targetEditor->setAiBusy(false);
        }
    }

    emit taskCompleted(finished, fullText.trimmed());
    finalizeBatchTimerIfIdle();
    emitStatus();
    processNext();
}

void AiTaskQueue::onProviderError(const QString& taskId, const QString& error) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    AiTask failed = m_currentTask;
    m_isProcessing = false;

    if (failed.targetEditor) {
        if (failed.targetType == TaskTargetType::ModifyEditorCode) {
            failed.targetEditor->cancelAndRollbackAiEdit();
        } else {
            failed.targetEditor->setAiBusy(false);
        }
    }

    emit taskFailed(failed, error);
    finalizeBatchTimerIfIdle();
    emitStatus();
    processNext();
}
