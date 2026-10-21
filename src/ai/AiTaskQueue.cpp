// src/ai/AiTaskQueue.cpp
#include "AiTaskQueue.h"
#include "AiProvider.h"
#include "../core/ConfigManager.h"
#include <QUuid>
#include <QTimer>

AiTaskQueue::AiTaskQueue(AiProvider* provider, QObject* parent)
    : QObject(parent), m_provider(provider) {
    connect(m_provider, &AiProvider::streamChunkReceived, this, &AiTaskQueue::onProviderChunk);
    connect(m_provider, &AiProvider::requestFinished, this, &AiTaskQueue::onProviderFinished);
    connect(m_provider, &AiProvider::requestError, this, &AiTaskQueue::onProviderError);
}

void AiTaskQueue::emitStatus() {
    emit queueStatusChanged(m_highQueue.size(), m_lowQueue.size(), m_isProcessing);
}

QString AiTaskQueue::enqueueTask(const AiTask& inputTask) {
    AiTask task = inputTask;
    if (task.taskId.isEmpty()) task.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (task.priority == TaskPriority::High_UserAction) {
        if (task.targetEditor)
            task.targetEditor->setAiBusy(true, (m_isProcessing || !m_highQueue.isEmpty()) ? "Queued..." : "Generating...");
        m_highQueue.enqueue(task);
    } else {
        // Дедупликация фоновых задач в очереди по filePath и targetType
        for (auto& existing : m_lowQueue) {
            if (existing.targetType == task.targetType && existing.filePath == task.filePath) {
                existing.fileSha256 = task.fileSha256;
                existing.systemPrompt = task.systemPrompt;
                existing.userPrompt = task.userPrompt;
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

void AiTaskQueue::cancelTaskForEditor(CodeEditor* editor) {
    if (!editor) return;
    for (int i = m_highQueue.size() - 1; i >= 0; --i) {
        if (m_highQueue[i].targetEditor == editor) m_highQueue.removeAt(i);
    }
    if (m_isProcessing && m_currentTask.targetEditor == editor) {
        m_provider->abortActiveRequest();
        editor->cancelAndRollbackAiEdit();
        m_isProcessing = false;
        emitStatus();
        QTimer::singleShot(0, this, &AiTaskQueue::processNext);
    } else {
        editor->setAiBusy(false);
        emitStatus();
    }
}

void AiTaskQueue::processNext() {
    if (m_isProcessing) return;

    // Пропускаем закрытые пользователем вкладки
    while (!m_highQueue.isEmpty() && m_highQueue.head().priority == TaskPriority::High_UserAction && !m_highQueue.head().targetEditor)
        m_highQueue.dequeue();

    if (!m_highQueue.isEmpty()) m_currentTask = m_highQueue.dequeue();
    else if (!m_lowQueue.isEmpty()) m_currentTask = m_lowQueue.dequeue();
    else { emitStatus(); return; }

    m_isProcessing = true;
    emitStatus();

    if (m_currentTask.targetEditor) {
        m_currentTask.targetEditor->setAiBusy(true, "Generating...");
        if (m_currentTask.targetType == TaskTargetType::ModifyEditorCode)
            m_currentTask.targetEditor->beginAiStreamEdit();
    }

    emit taskStarted(m_currentTask);
    m_provider->sendRequest(m_currentTask.taskId, m_currentTask.systemPrompt, m_currentTask.userPrompt,
                            m_currentTask.stream, ConfigManager::instance().aiConfig());
}

void AiTaskQueue::onProviderChunk(const QString& taskId, const QString& chunk) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
    if (m_currentTask.targetType == TaskTargetType::ModifyEditorCode && m_currentTask.targetEditor)
        m_currentTask.targetEditor->appendAiStreamChunk(chunk);
}

void AiTaskQueue::onProviderFinished(const QString& taskId, const QString& fullText) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
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
    processNext();
}

void AiTaskQueue::onProviderError(const QString& taskId, const QString& error) {
    if (!m_isProcessing || m_currentTask.taskId != taskId) return;
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
    processNext();
}