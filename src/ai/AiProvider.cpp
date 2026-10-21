// src/ai/AiProvider.cpp
#include "AiProvider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

AiProvider::AiProvider(QObject* parent) : QObject(parent) {}

static QString cleanEndpoint(const QString& url) {
    return url.endsWith('/') ? url.left(url.size() - 1) : url;
}

void AiProvider::fetchAvailableModels(const AiConnectionConfig& cfg) {
    QString base = cleanEndpoint(cfg.endpointUrl);
    QString path = (cfg.backend == AiBackendType::Ollama) ? "/api/tags" :
                   (cfg.backend == AiBackendType::KoboldCpp) ? "/api/v1/model" : "/v1/models";

    QNetworkRequest req(QUrl(base + path));
    req.setTransferTimeout(10000);
    if (!cfg.apiKey.isEmpty()) req.setRawHeader("Authorization", ("Bearer " + cfg.apiKey).toUtf8());

    auto* reply = m_netManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, backend = cfg.backend]() {
        QStringList models;
        if (reply->error() == QNetworkReply::NoError) {
            auto obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (backend == AiBackendType::Ollama) {
                for (const auto& v : obj["models"].toArray()) models << v.toObject()["name"].toString();
            } else if (backend == AiBackendType::KoboldCpp) {
                if (QString res = obj["result"].toString(); !res.isEmpty() && res != "ReadOnly") models << res;
            } else {
                for (const auto& v : obj["data"].toArray()) models << v.toObject()["id"].toString();
            }
        }
        reply->deleteLater();
        emit modelsFetched(models);
    });
}

void AiProvider::sendRequest(const QString& taskId, const QString& systemPrompt, const QString& userPrompt, bool stream, const AiConnectionConfig& cfg) {
    abortActiveRequest();
    m_activeTaskId = taskId;
    m_activeBackend = cfg.backend;
    m_isStreaming = stream;
    m_aborted = false;
    m_streamBuffer.clear();
    m_accumulatedResponse.clear();

    QString base = cleanEndpoint(cfg.endpointUrl), path;
    QJsonObject payload;

    if (cfg.backend == AiBackendType::Ollama) {
        path = "/api/chat";
        QJsonArray msgs;
        if (!systemPrompt.trimmed().isEmpty()) msgs.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        msgs.append(QJsonObject{{"role", "user"}, {"content", userPrompt}});
        payload = {
            {"model", cfg.modelName}, {"messages", msgs}, {"stream", stream},
            {"options", QJsonObject{{"temperature", cfg.temperature}, {"top_p", cfg.topP}, {"num_predict", cfg.maxOutputTokens}, {"num_ctx", cfg.contextWindow}}}
        };
    } else if (cfg.backend == AiBackendType::KoboldCpp) {
        path = stream ? "/api/extra/generate/stream" : "/api/v1/generate";
        QString combined = systemPrompt.trimmed().isEmpty() ? userPrompt : (systemPrompt + "\n\n" + userPrompt);
        payload = {
            {"prompt", combined}, {"temperature", cfg.temperature}, {"top_p", cfg.topP},
            {"max_length", cfg.maxOutputTokens}, {"max_context_length", cfg.contextWindow}
        };
    } else {
        path = "/v1/chat/completions";
        QJsonArray msgs;
        if (!systemPrompt.trimmed().isEmpty()) msgs.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        msgs.append(QJsonObject{{"role", "user"}, {"content", userPrompt}});
        payload = {
            {"model", cfg.modelName}, {"messages", msgs}, {"stream", stream},
            {"temperature", cfg.temperature}, {"top_p", cfg.topP}, {"max_tokens", cfg.maxOutputTokens}
        };
    }

    QNetworkRequest req(QUrl(base + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(cfg.timeoutSec * 1000);
    if (!cfg.apiKey.isEmpty()) req.setRawHeader("Authorization", ("Bearer " + cfg.apiKey).toUtf8());

    m_currentReply = m_netManager.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_currentReply, &QNetworkReply::readyRead, this, &AiProvider::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &AiProvider::onReplyFinished);
}

void AiProvider::abortActiveRequest() {
    if (!m_currentReply) return;
    m_aborted = true;
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void AiProvider::onReadyRead() {
    if (!m_currentReply || !m_isStreaming || m_aborted) return;
    QByteArray chunk = m_currentReply->readAll();
    if (m_activeBackend == AiBackendType::Ollama) parseOllamaStreamChunk(chunk);
    else parseSseStreamChunk(chunk);
}

void AiProvider::parseOllamaStreamChunk(const QByteArray& rawData) {
    m_streamBuffer.append(rawData);
    while (true) {
        int nl = m_streamBuffer.indexOf('\n');
        if (nl == -1) break;
        QByteArray line = m_streamBuffer.left(nl).trimmed();
        m_streamBuffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        auto obj = QJsonDocument::fromJson(line).object();
        QString token = obj.contains("message") ? obj["message"].toObject()["content"].toString() : obj["response"].toString();
        if (!token.isEmpty()) {
            m_accumulatedResponse += token;
            emit streamChunkReceived(m_activeTaskId, token);
        }
    }
}

void AiProvider::parseSseStreamChunk(const QByteArray& rawData) {
    m_streamBuffer.append(rawData);
    while (true) {
        int nl = m_streamBuffer.indexOf('\n');
        if (nl == -1) break;
        QByteArray line = m_streamBuffer.left(nl).trimmed();
        m_streamBuffer.remove(0, nl + 1);
        if (!line.startsWith("data:")) continue;

        QByteArray jsonPart = line.mid(5).trimmed();
        if (jsonPart == "[DONE]" || jsonPart.isEmpty()) continue;

        auto obj = QJsonDocument::fromJson(jsonPart).object();
        QString token;
        if (obj.contains("token")) token = obj["token"].toString();
        else if (obj.contains("choices")) {
            auto choices = obj["choices"].toArray();
            if (!choices.isEmpty()) {
                auto first = choices[0].toObject();
                token = first.contains("delta") ? first["delta"].toObject()["content"].toString() : first["text"].toString();
            }
        }
        if (!token.isEmpty()) {
            m_accumulatedResponse += token;
            emit streamChunkReceived(m_activeTaskId, token);
        }
    }
}

QString AiProvider::extractNonStreamText(AiBackendType backend, const QJsonObject& obj) {
    if (backend == AiBackendType::Ollama)
        return obj.contains("message") ? obj["message"].toObject()["content"].toString() : obj["response"].toString();
    if (backend == AiBackendType::KoboldCpp) {
        auto res = obj["results"].toArray();
        return res.isEmpty() ? QString() : res[0].toObject()["text"].toString();
    }
    auto choices = obj["choices"].toArray();
    return choices.isEmpty() ? QString() : choices[0].toObject()["message"].toObject()["content"].toString();
}

void AiProvider::onReplyFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (m_aborted || reply->error() == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        return;
    }

    QString taskId = m_activeTaskId;
    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
        emit requestError(taskId, err);
        return;
    }

    if (!m_isStreaming) {
        auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_accumulatedResponse = extractNonStreamText(m_activeBackend, obj);
    } else if (!m_streamBuffer.trimmed().isEmpty()) {
        if (m_activeBackend == AiBackendType::Ollama) parseOllamaStreamChunk("\n");
        else parseSseStreamChunk("\n");
    }

    reply->deleteLater();
    m_currentReply = nullptr;
    emit requestFinished(taskId, m_accumulatedResponse);
}