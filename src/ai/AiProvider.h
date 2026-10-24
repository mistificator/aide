// src/ai/AiProvider.h
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include "../core/ConfigManager.h"

class AiProvider : public QObject {
    Q_OBJECT
public:
    explicit AiProvider(QObject* parent = nullptr);

    void fetchAvailableModels(const AiConnectionConfig& cfg);
    void sendRequest(const QString& taskId,
                     const QString& systemPrompt,
                     const QString& userPrompt,
                     bool stream,
                     const AiConnectionConfig& cfg,
                     int maxOutputTokensOverride = -1);
    void abortActiveRequest();

signals:
    void modelsFetched(const QStringList& models);
    void thinkingStateChanged(const QString& taskId, bool isThinking);
    void streamChunkReceived(const QString& taskId, const QString& tokenChunk);
    void requestFinished(const QString& taskId, const QString& fullResponse);
    void requestError(const QString& taskId, const QString& errorMessage);
    void requestTimedOut(const QString& taskId);
    void debugLog(const QString& category, const QString& summary, const QString& payload);

private slots:
    void onReadyRead();
    void onReplyFinished();
    void onTimeoutTriggered();

private:
    void parseOllamaStreamChunk(const QByteArray& rawData);
    void parseSseStreamChunk(const QByteArray& rawData);
    void setThinkingState(bool thinking);
    void processStreamToken(const QString& tokenChunk, bool isExplicitReasoning = false);
    void emitCleanChunk(const QString& cleanChunk);
    void flushRemainingParseBuffer();
    static QString extractNonStreamText(AiBackendType backend, const QJsonObject& obj);
    static QString stripThinkingBlocks(const QString& text);
    void cleanupActiveReply();

    QNetworkAccessManager m_netManager;
    QPointer<QNetworkReply> m_currentReply;
    QTimer m_timeoutTimer;
    int m_activeTimeoutMs = 120000;
    QString m_activeTaskId, m_accumulatedResponse;
    AiBackendType m_activeBackend = AiBackendType::Ollama;
    bool m_isStreaming = false, m_aborted = false;
    QByteArray m_streamBuffer;

    // Состояние фильтрации размышлений (Thinking models)
    bool m_isThinking = false;
    bool m_inThinkTag = false;
    bool m_stripLeadingWhitespaceAfterThink = false;
    QString m_activeThinkCloseTag;
    QString m_thinkParseBuffer;
};
