// src/ai/AiTaskQueue.cpp
#include "AiTaskQueue.h"
#include "AiProvider.h"
#include "../core/ConfigManager.h"
#include <QUuid>
#include <QTimer>

AiTaskQueue::AiTaskQueue(AiProvider* provider, QObject* parent)
    : QObject(parent), m_provider(provider) {
    connect(m_provider, &AiProvider::thinkingStateChanged, this, &AiTaskQueue::onProviderThinkingChanged);
    connect(m_provider, &AiProvider::progressUpdated, this, &AiTaskQueue::onProviderProgressUpdated);
    connect(m_provider, &AiProvider::streamChunkReceived, this, &AiTaskQueue::onProviderChunk);
    connect(m_provider, &AiProvider::requestFinished, this, &AiTaskQueue::onProviderFinished);
    connect(m_provider, &AiProvider::requestError, this, &AiTaskQueue::onProviderError);
    connect(m_provider, &AiProvider::requestTimedOut, this, &AiTaskQueue::onProviderTimedOut);
}

int AiTaskQueue::estimateExpectedTokens(const AiTask& task) {
    if (task.targetType == TaskTargetType::FileSummary || task.targetType == TaskTargetType::ProjectSummary) {
        return qBound(128, task.maxOutputTokens > 0 ? task.maxOutputTokens / 2 : 512, 1024);
    }
    int inputEst = qMax(128, task.userPrompt.length() / 4);
    int limit = task.maxOutputTokens > 0 ? task.maxOutputTokens : 65536;
    return qBound(128, inputEst, limit);
}

AiTask AiTaskQueue::currentTask() const {
    AiTask copy = m_currentTask;
    if (m_isProcessing && m_activeTaskTimer.isValid()) {
        copy.elapsedMs = m_activeTaskTimer.elapsed();
    }
    return copy;
}

qint64 AiTaskQueue::currentTaskElapsedMs() const {
    if (m_isProcessing && m_activeTaskTimer.isValid()) {
        return m_activeTaskTimer.elapsed();
    }
    return 0;
}

qint64 AiTaskQueue::totalBatchElapsedMs() const {
    return m_completedBatchElapsedMs + currentTaskElapsedMs();
}

int AiTaskQueue::batchTotalCount() const {
    return m_batchCompletedTasks + (m_isProcessing ? 1 : 0) + m_highQueue.size() + m_lowQueue.size();
}

void AiTaskQueue::recordFinishedTaskTiming() {
    if (m_activeTaskTimer.isValid()) {
        m_completedBatchElapsedMs += m_activeTaskTimer.elapsed();
    }
    ++m_batchCompletedTasks;
}

void AiTaskQueue::emitStatus() {
    int activeHigh = (m_isProcessing && m_currentTask.priority == TaskPriority::High_UserAction) ? 1 : 0;
    int activeLow  = (m_isProcessing && m_currentTask.priority == TaskPriority::Low_BackgroundSummary) ? 1 : 0;
    emit queueStatusChanged(m_highQueue.size() + activeHigh,
                            m_lowQueue.size() + activeLow,
                            m_isProcessing);
    emit queueChanged();
}

bool AiTaskQueue::hasPendingOrActiveTask(TaskTargetType targetType,
                                         const QString& filePath,
                                         const QString& fileSha256,
                                         const QString& llmModel) const {
    auto matches = [&](const AiTask& t) {
        if (t.targetType != targetType || t.filePath != filePath) return false;
        if (!fileSha256.isEmpty() && t.fileSha256 != fileSha256) return false;
        if (!llmModel.isEmpty() && t.llmModel != llmModel) return false;
        return true;
    };
    if (m_isProcessing && matches(m_currentTask)) return true;
    for (const auto& t : m_lowQueue) {
        if (matches(t)) return true;
    }
    return false;
}

QString AiTaskQueue::enqueueTask(const AiTask& inputTask) {
    if (isIdle()) {
        m_completedBatchElapsedMs = 0;
        m_batchCompletedTasks = 0;
    }

    AiTask task = inputTask;
    task.isThinking = false;
    task.generatedTokens = 0;
    task.thinkingTokens = 0;
    task.elapsedMs = 0;
    task.tokensPerSec = 0.0;

    if (task.taskId.isEmpty()) task.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (task.llmModel.isEmpty()) task.llmModel = ConfigManager::instance().aiConfig().llmIdentifier();
    if (task.maxOutputTokens <= 0) {
        const auto& cfg = ConfigManager::instance().aiConfig();
        bool isSummary = (task.targetType == TaskTargetType::FileSummary || task.targetType == TaskTargetType::ProjectSummary);
        task.maxOutputTokens = isSummary ? cfg.maxOutputSummaryTokens : cfg.maxOutputCodeTokens;
    }
    task.expectedTokens = estimateExpectedTokens(task);

    if (task.priority == TaskPriority::High_UserAction) {
        if (task.targetEditor)
            task.targetEditor->setAiBusy(true, (m_isProcessing || !m_highQueue.isEmpty()) ? "Queued..." : "Generating...");
        m_highQueue.enqueue(task);
    } else {
        if (m_isProcessing &&
            m_currentTask.priority == TaskPriority::Low_BackgroundSummary &&
            m_currentTask.targetType == task.targetType &&
            m_currentTask.filePath == task.filePath &&
            m_currentTask.fileSha256 == task.fileSha256 &&
            m_currentTask.llmModel == task.llmModel) {
            return m_currentTask.taskId;
        }
        for (auto& existing : m_lowQueue) {
            if (existing.targetType == task.targetType && existing.filePath == task.filePath) {
                existing.fileSha256 = task.fileSha256;
                existing.llmModel = task.llmModel;
                existing.systemPrompt = task.systemPrompt;
                existing.userPrompt = task.userPrompt;
                existing.maxOutputTokens = task.maxOutputTokens;
                existing.expectedTokens = task.expectedTokens;
                emitStatus();
                return existing.taskId;
            }
        }
        m_lowQueue.enqueue(task);
    }

    emitStatus();
    if (!m_isProcessing) QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    return task.taskId;
}

void AiTaskQueue::cancelFileSummaryTask(const QString& relFilePath) {
    if (relFilePath.isEmpty()) return;
    for (int i = m_lowQueue.size() - 1; i >= 0; --i) {
        if (m_lowQueue[i].targetType == TaskTargetType::FileSummary && m_lowQueue[i].filePath == relFilePath) {
            AiTask cancelled = m_lowQueue.takeAt(i);
            emit taskCancelled(cancelled);
        }
    }
    if (m_isProcessing && m_currentTask.targetType == TaskTargetType::FileSummary && m_currentTask.filePath == relFilePath) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        recordFinishedTaskTiming();
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    } else {
        emitStatus();
    }
}

void AiTaskQueue::cancelTaskForEditor(CodeEditor* editor) {
    if (!editor) return;
    for (int i = m_highQueue.size() - 1; i >= 0; --i) {
        if (m_highQueue[i].targetEditor == editor) {
            AiTask removed = m_highQueue.takeAt(i);
            emit taskCancelled(removed);
        }
    }
    if (m_isProcessing && m_currentTask.targetEditor == editor) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        editor->cancelAndRollbackAiEdit();
        recordFinishedTaskTiming();
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    } else {
        editor->setAiBusy(false);
        emitStatus();
    }
}

void AiTaskQueue::cancelTaskById(const QString& taskId) {
    if (taskId.isEmpty()) return;
    for (int i = 0; i < m_highQueue.size(); ++i) {
        if (m_highQueue[i].taskId == taskId) {
            AiTask cancelled = m_highQueue.takeAt(i);
            if (cancelled.targetEditor) cancelled.targetEditor->setAiBusy(false);
            emit taskCancelled(cancelled);
            emitStatus();
            return;
        }
    }
    for (int i = 0; i < m_lowQueue.size(); ++i) {
        if (m_lowQueue[i].taskId == taskId) {
            AiTask cancelled = m_lowQueue.takeAt(i);
            emit taskCancelled(cancelled);
            emitStatus();
            return;
        }
    }
    if (m_isProcessing && m_currentTask.taskId == taskId) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        recordFinishedTaskTiming();
        m_isProcessing = false;
        if (cancelled.targetEditor) {
            if (cancelled.targetType == TaskTargetType::ModifyEditorCode)
                cancelled.targetEditor->cancelAndRollbackAiEdit();
            else
                cancelled.targetEditor->setAiBusy(false);
        }
        emit taskCancelled(cancelled);
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    }
}

void AiTaskQueue::cancelAllBackgroundTasks() {
    while (!m_lowQueue.isEmpty()) {
        AiTask cancelled = m_lowQueue.dequeue();
        emit taskCancelled(cancelled);
    }
    if (m_isProcessing && m_currentTask.priority == TaskPriority::Low_BackgroundSummary) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        recordFinishedTaskTiming();
        m_isProcessing = false;
        emit taskCancelled(cancelled);
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    } else {
        emitStatus();
    }
}

void AiTaskQueue::cancelAllTasks() {
    while (!m_highQueue.isEmpty()) {
        AiTask cancelled = m_highQueue.dequeue();
        if (cancelled.targetEditor) cancelled.targetEditor->setAiBusy(false);
        emit taskCancelled(cancelled);
    }
    while (!m_lowQueue.isEmpty()) {
        AiTask cancelled = m_lowQueue.dequeue();
        emit taskCancelled(cancelled);
    }
    if (m_isProcessing) {
        AiTask cancelled = m_currentTask;
        m_provider->abortActiveRequest();
        recordFinishedTaskTiming();
        m_isProcessing = false;
        if (cancelled.targetEditor) {
            if (cancelled.targetType == TaskTargetType::ModifyEditorCode)
                cancelled.targetEditor->cancelAndRollbackAiEdit();
            else
                cancelled.targetEditor->setAiBusy(false);
        }
        emit taskCancelled(cancelled);
    }
    emitStatus();
}

void AiTaskQueue::onProviderTimedOut(const QString& taskId) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    AiTask timedOutTask = m_currentTask;
    recordFinishedTaskTiming();
    m_isProcessing = false;
    if (timedOutTask.targetEditor) {
        if (timedOutTask.targetType == TaskTargetType::ModifyEditorCode)
            timedOutTask.targetEditor->cancelAndRollbackAiEdit();
        else
            timedOutTask.targetEditor->setAiBusy(false);
    }
    emit taskFailed(timedOutTask, "Request timed out (automatically cancelled and rolled back).");
    emitStatus();
    QTimer::singleShot(0, this, &AiTaskQueue::processNext);
}

void AiTaskQueue::processNext() {
    if (m_isProcessing) return;
    while (!m_highQueue.isEmpty() &&
           m_highQueue.head().priority == TaskPriority::High_UserAction &&
           m_highQueue.head().targetType != TaskTargetType::ProjectCustomPrompt &&
           !m_highQueue.head().targetEditor) {
        m_highQueue.dequeue();
    }
    if (!m_highQueue.isEmpty()) m_currentTask = m_highQueue.dequeue();
    else if (!m_lowQueue.isEmpty()) m_currentTask = m_lowQueue.dequeue();
    else { emitStatus(); return; }

    m_currentTask.isThinking = false;
    m_currentTask.generatedTokens = 0;
    m_currentTask.thinkingTokens = 0;
    m_currentTask.elapsedMs = 0;
    m_currentTask.tokensPerSec = 0.0;
    m_activeTaskTimer.start();

    m_isProcessing = true;
    emitStatus();

    if (m_currentTask.targetEditor) {
        m_currentTask.targetEditor->setAiBusy(true, "Generating...");
        if (m_currentTask.targetType == TaskTargetType::ModifyEditorCode)
            m_currentTask.targetEditor->beginAiStreamEdit();
    }

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = m_currentTask.maxOutputTokens > 0
                        ? m_currentTask.maxOutputTokens
                        : ((m_currentTask.targetType == TaskTargetType::FileSummary ||
                            m_currentTask.targetType == TaskTargetType::ProjectSummary)
                               ? cfg.maxOutputSummaryTokens
                               : cfg.maxOutputCodeTokens);

    emit taskStarted(m_currentTask);
    m_provider->sendRequest(m_currentTask.taskId,
                            m_currentTask.systemPrompt,
                            m_currentTask.userPrompt,
                            m_currentTask.stream,
                            cfg,
                            maxTokens);
}

void AiTaskQueue::onProviderThinkingChanged(const QString& taskId, bool isThinking) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    if (m_currentTask.isThinking == isThinking) return;
    m_currentTask.isThinking = isThinking;
    if (m_currentTask.targetEditor) {
        m_currentTask.targetEditor->setAiBusy(true, isThinking ? "Thinking..." : "Generating...");
    }
    emitStatus();
}

void AiTaskQueue::onProviderProgressUpdated(const QString& taskId,
                                            int generatedTokens,
                                            int thinkingTokens,
                                            qint64 elapsedMs,
                                            double tokensPerSec) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    m_currentTask.generatedTokens = generatedTokens;
    m_currentTask.thinkingTokens = thinkingTokens;
    m_currentTask.elapsedMs = elapsedMs;
    m_currentTask.tokensPerSec = tokensPerSec;
    emit taskProgressChanged(m_currentTask);
}

void AiTaskQueue::onProviderChunk(const QString& taskId, const QString& chunk) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    if (m_currentTask.targetType == TaskTargetType::ModifyEditorCode && m_currentTask.targetEditor)
        m_currentTask.targetEditor->appendAiStreamChunk(chunk);

    emit taskStreamChunk(m_currentTask, chunk);
}

void AiTaskQueue::onProviderFinished(const QString& taskId, const QString& fullText) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    recordFinishedTaskTiming();
    AiTask finished = m_currentTask;
    m_isProcessing = false;

    if (finished.targetEditor) {
        if (finished.targetType == TaskTargetType::ModifyEditorCode)
            finished.targetEditor->finishAiStreamEdit();
        else
            finished.targetEditor->setAiBusy(false);
    }

    emit taskCompleted(finished, fullText.trimmed());
    emitStatus();
    QTimer::singleShot(0, this, &AiTaskQueue::processNext);
}

void AiTaskQueue::onProviderError(const QString& taskId, const QString& error) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    recordFinishedTaskTiming();
    AiTask failed = m_currentTask;
    m_isProcessing = false;

    if (failed.targetEditor) {
        if (failed.targetType == TaskTargetType::ModifyEditorCode)
            failed.targetEditor->cancelAndRollbackAiEdit();
        else
            failed.targetEditor->setAiBusy(false);
    }

    emit taskFailed(failed, error);
    emitStatus();
    QTimer::singleShot(0, this, &AiTaskQueue::processNext);
}
