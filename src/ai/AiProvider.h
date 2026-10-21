// src/ai/AiProvider.h
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include "../core/ConfigManager.h"

class AiProvider : public QObject {
    Q_OBJECT
public:
    explicit AiProvider(QObject* parent = nullptr);

    void fetchAvailableModels(const AiConnectionConfig& cfg);
    void sendRequest(const QString& taskId, const QString& systemPrompt, const QString& userPrompt, bool stream, const AiConnectionConfig& cfg);
    void abortActiveRequest();

signals:
    void modelsFetched(const QStringList& models);
    void streamChunkReceived(const QString& taskId, const QString& tokenChunk);
    void requestFinished(const QString& taskId, const QString& fullResponse);
    void requestError(const QString& taskId, const QString& errorMessage);

private slots:
    void onReadyRead();
    void onReplyFinished();

private:
    void parseOllamaStreamChunk(const QByteArray& rawData);
    void parseSseStreamChunk(const QByteArray& rawData);
    static QString extractNonStreamText(AiBackendType backend, const QJsonObject& obj);

    QNetworkAccessManager m_netManager;
    QPointer<QNetworkReply> m_currentReply;
    QString m_activeTaskId, m_accumulatedResponse;
    AiBackendType m_activeBackend = AiBackendType::Ollama;
    bool m_isStreaming = false, m_aborted = false;
    QByteArray m_streamBuffer;
};