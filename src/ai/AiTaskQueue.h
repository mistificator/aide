// src/ai/AiTaskQueue.h
#pragma once
#include <QObject>
#include <QQueue>
#include <QList>
#include <QPointer>
#include "../editor/CodeEditor.h"

enum class TaskPriority { High_UserAction, Low_BackgroundSummary };
enum class TaskTargetType { ModifyEditorCode, ExplainToDialog, FileSummary, ProjectSummary };

struct AiTask {
    QString taskId, filePath, fileSha256, llmModel, systemPrompt, userPrompt;
    TaskPriority priority = TaskPriority::High_UserAction;
    TaskTargetType targetType = TaskTargetType::ModifyEditorCode;
    QPointer<CodeEditor> targetEditor;
    int maxOutputTokens = 0;
    bool stream = true;
};

class AiProvider;

class AiTaskQueue : public QObject {
    Q_OBJECT
public:
    explicit AiTaskQueue(AiProvider* provider, QObject* parent = nullptr);

    QString enqueueTask(const AiTask& task);
    void cancelTaskForEditor(CodeEditor* editor);
    void cancelTaskById(const QString& taskId);
    void cancelAllBackgroundTasks();
    void cancelAllTasks();

    bool isIdle() const { return !m_isProcessing && m_highQueue.isEmpty() && m_lowQueue.isEmpty(); }
    bool isProcessing() const { return m_isProcessing; }
    AiTask currentTask() const { return m_currentTask; }
    QList<AiTask> highQueueTasks() const { return m_highQueue; }
    QList<AiTask> lowQueueTasks() const { return m_lowQueue; }

    bool hasPendingOrActiveTask(TaskTargetType targetType,
                                const QString& filePath,
                                const QString& fileSha256 = {},
                                const QString& llmModel = {}) const;

signals:
    void taskStarted(const AiTask& task);
    void taskStreamChunk(const AiTask& task, const QString& chunk);
    void taskCompleted(const AiTask& task, const QString& resultText);
    void taskFailed(const AiTask& task, const QString& errorText);
    void taskCancelled(const AiTask& task);
    void queueStatusChanged(int highCount, int lowCount, bool busy);
    void queueChanged();

private slots:
    void processNext();
    void onProviderChunk(const QString& taskId, const QString& chunk);
    void onProviderFinished(const QString& taskId, const QString& fullText);
    void onProviderError(const QString& taskId, const QString& error);
    void onProviderTimedOut(const QString& taskId);

private:
    void emitStatus();

    AiProvider* m_provider = nullptr;
    QQueue<AiTask> m_highQueue, m_lowQueue;
    bool m_isProcessing = false;
    AiTask m_currentTask;
};
