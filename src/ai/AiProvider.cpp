// src/ai/AiProvider.cpp
#include "AiProvider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

namespace {
struct ThinkTagPair {
    const char* open;
    const char* close;
};

constexpr ThinkTagPair kThinkTagPairs[] = {
    {"<think>", "</think>"},
    {"<thinking>", "</thinking>"},
    {"<thought>", "</thought>"},
    {"<|begin_of_thought|>", "<|end_of_thought|>"}
};
} // namespace

AiProvider::AiProvider(QObject* parent) : QObject(parent) {
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &AiProvider::onTimeoutTriggered);
}

void AiProvider::cleanupActiveReply() {
    m_timeoutTimer.stop();
    if (m_currentReply) {
        m_currentReply->disconnect(this);
        if (m_currentReply->isRunning()) {
            m_currentReply->abort();
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

void AiProvider::setThinkingState(bool thinking) {
    if (m_isThinking == thinking) return;
    m_isThinking = thinking;
    if (!m_activeTaskId.isEmpty()) {
        emit thinkingStateChanged(m_activeTaskId, m_isThinking);
    }
}

void AiProvider::emitCleanChunk(const QString& cleanChunk) {
    if (cleanChunk.isEmpty()) return;
    if (!m_inThinkTag && m_isThinking) {
        setThinkingState(false);
    }
    m_accumulatedResponse += cleanChunk;
    emit streamChunkReceived(m_activeTaskId, cleanChunk);
}

void AiProvider::processStreamToken(const QString& tokenChunk, bool isExplicitReasoning) {
    if (isExplicitReasoning) {
        if (!tokenChunk.isEmpty()) {
            setThinkingState(true);
        }
        return;
    }
    if (tokenChunk.isEmpty()) return;

    m_thinkParseBuffer += tokenChunk;

    while (!m_thinkParseBuffer.isEmpty()) {
        if (m_inThinkTag) {
            int closeIdx = m_thinkParseBuffer.indexOf(m_activeThinkCloseTag, 0, Qt::CaseInsensitive);
            if (closeIdx >= 0) {
                m_thinkParseBuffer.remove(0, closeIdx + m_activeThinkCloseTag.length());
                m_inThinkTag = false;
                m_activeThinkCloseTag.clear();
                m_stripLeadingWhitespaceAfterThink = true;
                setThinkingState(false);
                continue;
            }

            // Проверяем, не оканчивается ли буфер на префикс закрывающего тега
            int keepSuffix = 0;
            int maxCheck = qMin(m_thinkParseBuffer.length(), m_activeThinkCloseTag.length() - 1);
            for (int len = maxCheck; len >= 1; --len) {
                if (m_activeThinkCloseTag.startsWith(m_thinkParseBuffer.right(len), Qt::CaseInsensitive)) {
                    keepSuffix = len;
                    break;
                }
            }
            m_thinkParseBuffer = (keepSuffix > 0) ? m_thinkParseBuffer.right(keepSuffix) : QString();
            break;
        } else {
            if (m_stripLeadingWhitespaceAfterThink) {
                int i = 0;
                while (i < m_thinkParseBuffer.length() &&
                       (m_thinkParseBuffer[i] == '\n' || m_thinkParseBuffer[i] == '\r' ||
                        m_thinkParseBuffer[i] == ' '  || m_thinkParseBuffer[i] == '\t')) {
                    ++i;
                }
                if (i > 0) m_thinkParseBuffer.remove(0, i);
                if (m_thinkParseBuffer.isEmpty()) break;
                m_stripLeadingWhitespaceAfterThink = false;
            }

            // Ищем ближайший открывающий тег размышлений
            int bestOpenIdx = -1;
            QString matchedOpenTag;
            QString matchedCloseTag;
            for (const auto& tp : kThinkTagPairs) {
                QString openStr = QString::fromLatin1(tp.open);
                int idx = m_thinkParseBuffer.indexOf(openStr, 0, Qt::CaseInsensitive);
                if (idx >= 0 && (bestOpenIdx < 0 || idx < bestOpenIdx)) {
                    bestOpenIdx = idx;
                    matchedOpenTag = openStr;
                    matchedCloseTag = QString::fromLatin1(tp.close);
                }
            }

            if (bestOpenIdx >= 0) {
                QString before = m_thinkParseBuffer.left(bestOpenIdx);
                if (!before.trimmed().isEmpty() || !m_accumulatedResponse.isEmpty()) {
                    emitCleanChunk(before);
                }
                m_thinkParseBuffer.remove(0, bestOpenIdx + matchedOpenTag.length());
                m_inThinkTag = true;
                m_activeThinkCloseTag = matchedCloseTag;
                setThinkingState(true);
                continue;
            }

            // На случай если модель закрыла </think>, открытый ещё в шаблоне промпта сервера
            for (const auto& tp : kThinkTagPairs) {
                QString closeStr = QString::fromLatin1(tp.close);
                int strayClose = m_thinkParseBuffer.indexOf(closeStr, 0, Qt::CaseInsensitive);
                if (strayClose >= 0) {
                    m_thinkParseBuffer.remove(0, strayClose + closeStr.length());
                    m_accumulatedResponse.clear();
                    m_stripLeadingWhitespaceAfterThink = true;
                    setThinkingState(false);
                    break;
                }
            }
            if (m_stripLeadingWhitespaceAfterThink) continue;

            // В самом начале ответа придерживаем ведущие пробелы/переводы строк на случай <think>
            if (m_accumulatedResponse.isEmpty() &&
                m_thinkParseBuffer.trimmed().isEmpty() &&
                m_thinkParseBuffer.length() < 16) {
                break;
            }

            // Проверяем, не оканчивается ли буфер на частичный префикс открывающего тега (например, "<th")
            int keepSuffix = 0;
            for (const auto& tp : kThinkTagPairs) {
                QString openStr = QString::fromLatin1(tp.open);
                int maxCheck = qMin(m_thinkParseBuffer.length(), openStr.length() - 1);
                for (int len = maxCheck; len >= 1; --len) {
                    if (openStr.startsWith(m_thinkParseBuffer.right(len), Qt::CaseInsensitive)) {
                        keepSuffix = qMax(keepSuffix, len);
                        break;
                    }
                }
            }

            int emitLen = m_thinkParseBuffer.length() - keepSuffix;
            if (emitLen > 0) {
                emitCleanChunk(m_thinkParseBuffer.left(emitLen));
                m_thinkParseBuffer.remove(0, emitLen);
            }
            break;
        }
    }
}

void AiProvider::flushRemainingParseBuffer() {
    if (!m_inThinkTag && !m_thinkParseBuffer.isEmpty()) {
        if (!m_accumulatedResponse.isEmpty() || !m_thinkParseBuffer.trimmed().isEmpty()) {
            emitCleanChunk(m_thinkParseBuffer);
        }
    }
    m_thinkParseBuffer.clear();
    m_inThinkTag = false;
    m_activeThinkCloseTag.clear();
    m_stripLeadingWhitespaceAfterThink = false;
    setThinkingState(false);
}

QString AiProvider::stripThinkingBlocks(const QString& text) {
    QString out = text;
    static const QRegularExpression rxThink(
        R"((?:<think>[\s\S]*?</think>|<thinking>[\s\S]*?</thinking>|<thought>[\s\S]*?</thought>|<\|begin_of_thought\|>[\s\S]*?<\|end_of_thought\|>)\s*)",
        QRegularExpression::CaseInsensitiveOption);
    out.remove(rxThink);
    return out.trimmed();
}

void AiProvider::abortActiveRequest() {
    m_aborted = true;
    QString cancelledTaskId = m_activeTaskId;
    setThinkingState(false);
    m_activeTaskId.clear();
    m_streamBuffer.clear();
    m_thinkParseBuffer.clear();
    m_inThinkTag = false;
    cleanupActiveReply();

    if (!cancelledTaskId.isEmpty()) {
        emit debugLog("CANCEL", QString("Task %1 cancelled by user").arg(cancelledTaskId), {});
    }
}

void AiProvider::onTimeoutTriggered() {
    if (m_activeTaskId.isEmpty()) return;
    QString timedOutTaskId = m_activeTaskId;
    emit debugLog("TIMEOUT", QString("Task %1 timed out (%2 ms) -> triggering Cancel logic")
                                 .arg(timedOutTaskId).arg(m_activeTimeoutMs), {});
    m_aborted = true;
    setThinkingState(false);
    m_activeTaskId.clear();
    m_streamBuffer.clear();
    m_thinkParseBuffer.clear();
    m_inThinkTag = false;
    cleanupActiveReply();
    emit requestTimedOut(timedOutTaskId);
}

void AiProvider::fetchAvailableModels(const AiConnectionConfig& cfg) {
    QString base = cfg.endpointUrl.trimmed();
    while (base.endsWith('/')) base.chop(1);

    QUrl url(base + (cfg.backend == AiBackendType::Ollama ? "/api/tags"
                     : cfg.backend == AiBackendType::KoboldCpp ? "/api/v1/model"
                                                               : "/v1/models"));
    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    if (!cfg.apiKey.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + cfg.apiKey).toUtf8());

    emit debugLog("REQUEST", "GET " + url.toString(), {});

    QNetworkReply* reply = m_netManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, backend = cfg.backend]() {
        QStringList models;
        QByteArray raw = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            emit debugLog("RESPONSE", "Models Fetched OK", QString::fromUtf8(raw));
            auto obj = QJsonDocument::fromJson(raw).object();
            if (backend == AiBackendType::Ollama) {
                for (const auto& v : obj["models"].toArray())
                    models.append(v.toObject()["name"].toString());
            } else if (backend == AiBackendType::KoboldCpp) {
                QString m = obj["result"].toString();
                if (!m.isEmpty()) models.append(m);
            } else {
                for (const auto& v : obj["data"].toArray())
                    models.append(v.toObject()["id"].toString());
            }
        } else {
            emit debugLog("ERROR", "Model Fetch Failed: " + reply->errorString(), QString::fromUtf8(raw));
        }
        reply->deleteLater();
        emit modelsFetched(models);
    });
}

void AiProvider::sendRequest(const QString& taskId,
                             const QString& systemPrompt,
                             const QString& userPrompt,
                             bool stream,
                             const AiConnectionConfig& cfg,
                             int maxOutputTokensOverride) {
    if (m_currentReply) {
        cleanupActiveReply();
    }

    m_activeTaskId = taskId;
    m_activeBackend = cfg.backend;
    m_isStreaming = stream;
    m_aborted = false;
    m_streamBuffer.clear();
    m_accumulatedResponse.clear();
    m_isThinking = false;
    m_inThinkTag = false;
    m_stripLeadingWhitespaceAfterThink = false;
    m_activeThinkCloseTag.clear();
    m_thinkParseBuffer.clear();
    m_activeTimeoutMs = qMax(5, cfg.timeoutSec) * 1000;

    int maxTokens = (maxOutputTokensOverride > 0) ? maxOutputTokensOverride : cfg.maxOutputCodeTokens;

    QString base = cfg.endpointUrl.trimmed();
    while (base.endsWith('/')) base.chop(1);

    QUrl url;
    QJsonObject payload;

    if (cfg.backend == AiBackendType::Ollama) {
        url = QUrl(base + "/api/generate");
        payload = {
            {"model", cfg.modelName},
            {"system", systemPrompt},
            {"prompt", userPrompt},
            {"stream", stream},
            {"options", QJsonObject{
                            {"temperature", cfg.temperature},
                            {"top_p", cfg.topP},
                            {"num_predict", maxTokens},
                            {"num_ctx", cfg.contextWindow}
                        }}
        };
    } else if (cfg.backend == AiBackendType::KoboldCpp && !base.endsWith("/v1")) {
        url = QUrl(base + (stream ? "/api/extra/generate/stream" : "/api/v1/generate"));
        payload = {
            {"prompt", systemPrompt.isEmpty() ? userPrompt : (systemPrompt + "\n\n" + userPrompt)},
            {"temperature", cfg.temperature},
            {"top_p", cfg.topP},
            {"max_length", maxTokens},
            {"max_context_length", cfg.contextWindow}
        };
    } else {
        url = QUrl(base + "/v1/chat/completions");
        QJsonArray msgs;
        if (!systemPrompt.isEmpty()) msgs.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        msgs.append(QJsonObject{{"role", "user"}, {"content", userPrompt}});
        payload = {
            {"model", cfg.modelName},
            {"messages", msgs},
            {"stream", stream},
            {"temperature", cfg.temperature},
            {"top_p", cfg.topP},
            {"max_tokens", maxTokens}
        };
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!cfg.apiKey.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + cfg.apiKey).toUtf8());

    QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Indented);
    emit debugLog("REQUEST",
                  QString("POST %1 [Task: %2, Stream: %3, MaxTokens: %4]")
                      .arg(url.toString(), taskId, stream ? "true" : "false").arg(maxTokens),
                  QString::fromUtf8(jsonBytes));

    m_currentReply = m_netManager.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_currentReply, &QNetworkReply::readyRead, this, &AiProvider::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &AiProvider::onReplyFinished);
    m_timeoutTimer.start(m_activeTimeoutMs);
}

void AiProvider::onReadyRead() {
    if (!m_currentReply || m_aborted) return;
    m_timeoutTimer.start(m_activeTimeoutMs);

    QByteArray chunk = m_currentReply->readAll();
    if (!m_isStreaming) {
        m_streamBuffer.append(chunk);
        return;
    }

    emit debugLog("STREAM", QString("Chunk (%1 bytes) [Task: %2]").arg(chunk.size()).arg(m_activeTaskId),
                  QString::fromUtf8(chunk));

    if (m_activeBackend == AiBackendType::Ollama)
        parseOllamaStreamChunk(chunk);
    else
        parseSseStreamChunk(chunk);
}

void AiProvider::parseOllamaStreamChunk(const QByteArray& rawData) {
    m_streamBuffer.append(rawData);
    while (true) {
        int nl = m_streamBuffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_streamBuffer.left(nl).trimmed();
        m_streamBuffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        auto obj = QJsonDocument::fromJson(line).object();
        QString thinkTok = obj.contains("message")
                               ? obj["message"].toObject()["thinking"].toString()
                               : obj["thinking"].toString();
        if (!thinkTok.isEmpty()) {
            processStreamToken(thinkTok, true);
        }

        QString tok = obj.contains("message")
                          ? obj["message"].toObject()["content"].toString()
                          : obj["response"].toString();
        if (!tok.isEmpty()) {
            processStreamToken(tok, false);
        }
    }
}

void AiProvider::parseSseStreamChunk(const QByteArray& rawData) {
    m_streamBuffer.append(rawData);
    while (true) {
        int nl = m_streamBuffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = m_streamBuffer.left(nl).trimmed();
        m_streamBuffer.remove(0, nl + 1);
        if (!line.startsWith("data:")) continue;
        QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]") continue;

        auto obj = QJsonDocument::fromJson(payload).object();
        QString thinkTok;
        QString tok;
        if (obj.contains("token")) {
            tok = obj["token"].toString();
        } else if (obj.contains("choices")) {
            auto ch = obj["choices"].toArray();
            if (!ch.isEmpty()) {
                auto first = ch[0].toObject();
                if (first.contains("delta")) {
                    auto delta = first["delta"].toObject();
                    thinkTok = delta["reasoning_content"].toString();
                    if (thinkTok.isEmpty()) thinkTok = delta["reasoning"].toString();
                    if (thinkTok.isEmpty()) thinkTok = delta["thinking"].toString();
                    tok = delta["content"].toString();
                }
                if (tok.isEmpty()) tok = first["text"].toString();
            }
        }
        if (!thinkTok.isEmpty()) {
            processStreamToken(thinkTok, true);
        }
        if (!tok.isEmpty()) {
            processStreamToken(tok, false);
        }
    }
}

QString AiProvider::extractNonStreamText(AiBackendType backend, const QJsonObject& obj) {
    if (backend == AiBackendType::Ollama) return stripThinkingBlocks(obj["response"].toString());
    if (obj.contains("results")) {
        auto arr = obj["results"].toArray();
        if (!arr.isEmpty()) return stripThinkingBlocks(arr[0].toObject()["text"].toString());
    }
    if (obj.contains("choices")) {
        auto arr = obj["choices"].toArray();
        if (!arr.isEmpty()) {
            auto c = arr[0].toObject();
            if (c.contains("message")) return stripThinkingBlocks(c["message"].toObject()["content"].toString());
            return stripThinkingBlocks(c["text"].toString());
        }
    }
    return {};
}

void AiProvider::onReplyFinished() {
    m_timeoutTimer.stop();
    if (!m_currentReply) return;

    QNetworkReply* reply = m_currentReply;
    m_currentReply = nullptr;

    if (m_aborted || m_activeTaskId.isEmpty()) {
        setThinkingState(false);
        reply->deleteLater();
        return;
    }

    QString taskId = m_activeTaskId;

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        setThinkingState(false);
        m_activeTaskId.clear();
        emit debugLog("ERROR", QString("Task %1 failed: %2").arg(taskId, errStr),
                      QString::fromUtf8(m_streamBuffer));
        reply->deleteLater();
        emit requestError(taskId, errStr);
        return;
    }

    QByteArray remaining = reply->readAll();
    if (!m_isStreaming) {
        m_streamBuffer.append(remaining);
        emit debugLog("RESPONSE", QString("Task %1 completed (%2 bytes)").arg(taskId).arg(m_streamBuffer.size()),
                      QString::fromUtf8(m_streamBuffer));
        auto obj = QJsonDocument::fromJson(m_streamBuffer).object();
        m_accumulatedResponse = extractNonStreamText(m_activeBackend, obj);
    } else {
        if (!remaining.isEmpty()) {
            if (m_activeBackend == AiBackendType::Ollama) parseOllamaStreamChunk(remaining);
            else parseSseStreamChunk(remaining);
        }
        if (!m_streamBuffer.trimmed().isEmpty()) {
            if (m_activeBackend == AiBackendType::Ollama) parseOllamaStreamChunk("\n");
            else parseSseStreamChunk("\n");
        }
        flushRemainingParseBuffer();
        emit debugLog("RESPONSE", QString("Stream Task %1 finished (%2 chars)")
                                      .arg(taskId).arg(m_accumulatedResponse.size()),
                      m_accumulatedResponse);
    }

    setThinkingState(false);
    m_activeTaskId.clear();
    reply->deleteLater();
    emit requestFinished(taskId, m_accumulatedResponse);
}
