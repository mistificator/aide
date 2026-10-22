// src/ai/AiTaskQueue.h
#pragma once
#include <QObject>
#include <QQueue>
#include <QPointer>
#include "../editor/CodeEditor.h"

enum class TaskPriority { High_UserAction, Low_BackgroundSummary };
enum class TaskTargetType { ModifyEditorCode, ExplainToDialog, FileSummary, ProjectSummary };

struct AiTask {
    QString taskId, filePath, fileSha256, systemPrompt, userPrompt;
    TaskPriority priority = TaskPriority::High_UserAction;
    TaskTargetType targetType = TaskTargetType::ModifyEditorCode;
    QPointer<CodeEditor> targetEditor;
    bool stream = true;
};

class AiProvider;

class AiTaskQueue : public QObject {
    Q_OBJECT
public:
    explicit AiTaskQueue(AiProvider* provider, QObject* parent = nullptr);

    QString enqueueTask(const AiTask& task);
    void cancelTaskForEditor(CodeEditor* editor);
    void cancelCurrentTaskByTimeout();
    bool isIdle() const { return !m_isProcessing && m_highQueue.isEmpty() && m_lowQueue.isEmpty(); }

signals:
    void taskStarted(const AiTask& task);
    void taskStreamChunk(const AiTask& task, const QString& chunk);
    void taskCompleted(const AiTask& task, const QString& resultText);
    void taskFailed(const AiTask& task, const QString& errorText);
    void queueStatusChanged(int highCount, int lowCount, bool busy);

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
