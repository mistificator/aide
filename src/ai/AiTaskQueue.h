// src/ai/AiTaskQueue.h
#pragma once
#include <QObject>
#include <QQueue>
#include <QVector>
#include <QPointer>
#include <QElapsedTimer>
#include "../editor/CodeEditor.h"
#include "../core/ConfigManager.h"

class AiProvider;

enum class TaskPriority {
    High_UserAction,
    Low_BackgroundSummary
};

enum class TaskTargetType {
    ModifyEditorCode,
    ExplainToDialog,
    ProjectCustomPrompt,
    FileSummary,
    ProjectSummary
};

struct AiTask {
    QString taskId;
    TaskPriority priority = TaskPriority::High_UserAction;
    TaskTargetType targetType = TaskTargetType::ModifyEditorCode;
    QPointer<CodeEditor> targetEditor;
    QString filePath;
    QString fileSha256;
    QString llmModel;
    int maxOutputTokens = 0;
    int contextWindow = 0;
    QString systemPrompt;
    QVector<AiChatMessage> conversationHistory;
    QString userPrompt;
    bool isContinuation = false;
    bool appendToPreviousOutput = false;
    QString previousOutput;
    bool stream = true;

    // Метрики выполнения в реальном времени
    bool isThinking = false;
    int generatedTokens = 0;
    int thinkingTokens = 0;
    int expectedTokens = 512;
    qint64 elapsedMs = 0;
    double tokensPerSec = 0.0;
};

class AiTaskQueue : public QObject {
    Q_OBJECT
public:
    explicit AiTaskQueue(AiProvider* provider, QObject* parent = nullptr);

    QString enqueueTask(const AiTask& inputTask);
    void enqueueTasksBatch(const QVector<AiTask>& inputTasks);

    void cancelTaskForEditor(CodeEditor* editor);
    void cancelTaskById(const QString& taskId);
    void cancelFileSummaryTask(const QString& relFilePath);
    void cancelAllBackgroundTasks();
    void cancelAllTasks();

    bool isIdle() const { return !m_isProcessing && m_highQueue.isEmpty() && m_lowQueue.isEmpty(); }
    bool isProcessing() const { return m_isProcessing; }
    bool hasPendingOrActiveTask(TaskTargetType targetType, const QString& filePath) const;

    AiTask currentTask() const { return m_currentTask; }
    QList<AiTask> highQueueTasks() const { return m_highQueue; }
    QList<AiTask> lowQueueTasks() const { return m_lowQueue; }

    qint64 totalBatchElapsedMs() const;
    int batchCompletedCount() const { return m_batchCompletedCount; }
    int batchTotalCount() const;

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
    void onProviderChunk(const QString& taskId, const QString& chunk);
    void onProviderProgress(const QString& taskId, int outputChunks, int thinkingChunks, qint64 elapsedMs, double tokPerSec);
    void onProviderThinkingState(const QString& taskId, bool isThinking);
    void onProviderTimedOut(const QString& taskId);
    void onProviderFinished(const QString& taskId, const QString& fullText);
    void onProviderError(const QString& taskId, const QString& error);

private:
    static int estimateExpectedTokens(const AiTask& task);
    void prepareTaskDefaults(AiTask& task) const;
    void startBatchTimerIfNeeded();
    void finalizeBatchTimerIfIdle();
    void emitStatus();

    AiProvider* m_provider = nullptr;
    QQueue<AiTask> m_highQueue;
    QQueue<AiTask> m_lowQueue;
    bool m_isProcessing = false;
    AiTask m_currentTask;

    QElapsedTimer m_batchTimer;
    qint64 m_lastBatchElapsedMs = 0;
    int m_batchCompletedCount = 0;
};
