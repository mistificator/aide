// src/ai/AiTaskQueue.h
#pragma once
#include <QObject>
#include <QQueue>
#include <QList>
#include <QPointer>
#include <QElapsedTimer>
#include "../editor/CodeEditor.h"

enum class TaskPriority { High_UserAction, Low_BackgroundSummary };
enum class TaskTargetType {
    ModifyEditorCode,
    ExplainToDialog,
    ProjectCustomPrompt,
    FileSummary,
    ProjectSummary
};

struct AiTask {
    QString taskId, filePath, fileSha256, llmModel, systemPrompt, userPrompt;
    TaskPriority priority = TaskPriority::High_UserAction;
    TaskTargetType targetType = TaskTargetType::ModifyEditorCode;
    QPointer<CodeEditor> targetEditor;
    int maxOutputTokens = 0;
    int contextWindow = 0;
    int expectedTokens = 512;
    bool stream = true;
    bool isThinking = false;

    // Метрики выполнения в реальном времени
    int generatedTokens = 0;
    int thinkingTokens = 0;
    qint64 elapsedMs = 0;
    double tokensPerSec = 0.0;
};

class AiProvider;

class AiTaskQueue : public QObject {
    Q_OBJECT
public:
    explicit AiTaskQueue(AiProvider* provider, QObject* parent = nullptr);

    QString enqueueTask(const AiTask& task);
    void cancelTaskForEditor(CodeEditor* editor);
    void cancelTaskById(const QString& taskId);
    void cancelFileSummaryTask(const QString& relFilePath);
    void cancelAllBackgroundTasks();
    void cancelAllTasks();

    bool isIdle() const { return !m_isProcessing && m_highQueue.isEmpty() && m_lowQueue.isEmpty(); }
    bool isProcessing() const { return m_isProcessing; }
    AiTask currentTask() const;
    QList<AiTask> highQueueTasks() const { return m_highQueue; }
    QList<AiTask> lowQueueTasks() const { return m_lowQueue; }

    qint64 currentTaskElapsedMs() const;
    qint64 totalBatchElapsedMs() const;
    int batchCompletedCount() const { return m_batchCompletedTasks; }
    int batchTotalCount() const;

    bool hasPendingOrActiveTask(TaskTargetType targetType,
                                const QString& filePath,
                                const QString& fileSha256 = {},
                                const QString& llmModel = {}) const;

signals:
    void taskStarted(const AiTask& task);
    void taskStreamChunk(const AiTask& task, const QString& chunk);
    void taskProgressChanged(const AiTask& task);
    void taskCompleted(const AiTask& task, const QString& resultText);
    void taskFailed(const AiTask& task, const QString& errorText);
    void taskCancelled(const AiTask& task);
    void queueStatusChanged(int highCount, int lowCount, bool busy);
    void queueChanged();

private slots:
    void processNext();
    void onProviderThinkingChanged(const QString& taskId, bool isThinking);
    void onProviderProgressUpdated(const QString& taskId,
                                   int generatedTokens,
                                   int thinkingTokens,
                                   qint64 elapsedMs,
                                   double tokensPerSec);
    void onProviderChunk(const QString& taskId, const QString& chunk);
    void onProviderFinished(const QString& taskId, const QString& fullText);
    void onProviderError(const QString& taskId, const QString& error);
    void onProviderTimedOut(const QString& taskId);

private:
    void emitStatus();
    void recordFinishedTaskTiming();
    static int estimateExpectedTokens(const AiTask& task);

    AiProvider* m_provider = nullptr;
    QQueue<AiTask> m_highQueue, m_lowQueue;
    bool m_isProcessing = false;
    AiTask m_currentTask;

    QElapsedTimer m_activeTaskTimer;
    qint64 m_completedBatchElapsedMs = 0;
    int m_batchCompletedTasks = 0;
};
