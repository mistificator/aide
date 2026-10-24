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

QString AiProvider::buildOpenAiEndpoint(QString base, const QString& subPathAfterV1) {
    while (base.endsWith('/')) base.chop(1);
    if (base.endsWith("/v1", Qt::CaseInsensitive)) {
        return base + subPathAfterV1;
    }
    return base + "/v1" + subPathAfterV1;
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

void AiProvider::recordFirstTokenIfNeeded() {
    if (m_ttftMs < 0 && m_requestTimer.isValid()) {
        m_ttftMs = m_requestTimer.elapsed();
    }
}

void AiProvider::emitLiveProgress() {
    if (m_activeTaskId.isEmpty() || !m_requestTimer.isValid()) return;
    qint64 elapsedMs = m_requestTimer.elapsed();
    int totalGenTok = (m_apiCompletionTokens > 0) ? m_apiCompletionTokens : (m_outputChunks + m_thinkingChunks);
    qint64 genWindowMs = (m_ttftMs >= 0 && elapsedMs > m_ttftMs) ? (elapsedMs - m_ttftMs) : elapsedMs;
    double tokPerSec = (m_apiTokensPerSec > 0.0)
                           ? m_apiTokensPerSec
                           : ((genWindowMs > 50 && totalGenTok > 0) ? (totalGenTok * 1000.0 / genWindowMs) : 0.0);

    emit progressUpdated(m_activeTaskId, m_outputChunks, m_thinkingChunks, elapsedMs, tokPerSec);
}

void AiProvider::extractApiUsageAndTimings(const QJsonObject& obj) {
    // Формат Ollama
    if (obj.contains("prompt_eval_count")) {
        m_apiPromptTokens = obj["prompt_eval_count"].toInt(m_apiPromptTokens);
    }
    if (obj.contains("eval_count")) {
        m_apiCompletionTokens = obj["eval_count"].toInt(m_apiCompletionTokens);
    }
    if (obj.contains("eval_duration")) {
        qint64 evalNs = obj["eval_duration"].toVariant().toLongLong();
        if (evalNs > 0 && m_apiCompletionTokens > 0) {
            m_apiTokensPerSec = (m_apiCompletionTokens * 1e9) / static_cast<double>(evalNs);
        }
    }

    // Формат OpenAI / KoboldCpp / vLLM
    if (obj.contains("usage") && obj["usage"].isObject()) {
        auto u = obj["usage"].toObject();
        m_apiPromptTokens = u["prompt_tokens"].toInt(m_apiPromptTokens);
        m_apiCompletionTokens = u["completion_tokens"].toInt(m_apiCompletionTokens);
    }

    // Формат llama.cpp / KoboldCpp timings
    if (obj.contains("timings") && obj["timings"].isObject()) {
        auto t = obj["timings"].toObject();
        if (t.contains("prompt_n")) m_apiPromptTokens = t["prompt_n"].toInt(m_apiPromptTokens);
        if (t.contains("predicted_n")) m_apiCompletionTokens = t["predicted_n"].toInt(m_apiCompletionTokens);
        if (t.contains("predicted_per_second")) m_apiTokensPerSec = t["predicted_per_second"].toDouble(m_apiTokensPerSec);
    }
}

QString AiProvider::formatRequestStatsSummary(const QString& statusLabel) const {
    qint64 elapsedMs = m_requestTimer.isValid() ? m_requestTimer.elapsed() : 0;
    double elapsedSec = elapsedMs / 1000.0;
    int totalTok = (m_apiCompletionTokens > 0) ? m_apiCompletionTokens : (m_outputChunks + m_thinkingChunks);
    qint64 genWindowMs = (m_ttftMs >= 0 && elapsedMs > m_ttftMs) ? (elapsedMs - m_ttftMs) : elapsedMs;
    double tokPerSec = (m_apiTokensPerSec > 0.0)
                           ? m_apiTokensPerSec
                           : ((genWindowMs > 0 && totalTok > 0) ? (totalTok * 1000.0 / genWindowMs) : 0.0);

    QString ttftStr = (m_ttftMs >= 0) ? QString("%1 s").arg(m_ttftMs / 1000.0, 0, 'f', 2) : "n/a";
    return QString("Task %1 %2 in %3 s (TTFT: %4) | Generated: %5 tok (%6 tok/s)")
        .arg(m_activeTaskId, statusLabel)
        .arg(elapsedSec, 0, 'f', 2)
        .arg(ttftStr)
        .arg(totalTok)
        .arg(tokPerSec, 0, 'f', 1);
}

QString AiProvider::formatRequestStatsDetails() const {
    qint64 elapsedMs = m_requestTimer.isValid() ? m_requestTimer.elapsed() : 0;
    int promptTok = (m_apiPromptTokens > 0) ? m_apiPromptTokens : m_estimatedPromptTokens;
    QString promptTokSuffix = (m_apiPromptTokens > 0) ? "exact" : "est";
    int totalOutTok = (m_apiCompletionTokens > 0) ? m_apiCompletionTokens : (m_outputChunks + m_thinkingChunks);
    int outChars = m_accumulatedResponse.length();
    double charsPerSec = (elapsedMs > 0) ? ((outChars + m_thinkingChars) * 1000.0 / elapsedMs) : 0.0;

    return QString("Prompt: %1 tok (%2, %3 chars) | Output: %4 tok [Answer: %5 chunks / %6 chars, Thinking: %7 chunks / %8 chars] | Throughput: %9 chars/s | Max Limit: %10 tok")
        .arg(promptTok)
        .arg(promptTokSuffix)
        .arg(m_promptChars)
        .arg(totalOutTok)
        .arg(m_outputChunks)
        .arg(outChars)
        .arg(m_thinkingChunks)
        .arg(m_thinkingChars)
        .arg(charsPerSec, 0, 'f', 1)
        .arg(m_maxOutputTokens);
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
    ++m_outputChunks;
    m_accumulatedResponse += cleanChunk;
    emit streamChunkReceived(m_activeTaskId, cleanChunk);
    emitLiveProgress();
}

void AiProvider::processStreamToken(const QString& tokenChunk, bool isExplicitReasoning) {
    if (!tokenChunk.isEmpty()) {
        recordFirstTokenIfNeeded();
    }

    if (isExplicitReasoning) {
        if (!tokenChunk.isEmpty()) {
            ++m_thinkingChunks;
            m_thinkingChars += tokenChunk.length();
            setThinkingState(true);
            emitLiveProgress();
        }
        return;
    }
    if (tokenChunk.isEmpty()) return;

    m_thinkParseBuffer += tokenChunk;

    while (!m_thinkParseBuffer.isEmpty()) {
        if (m_inThinkTag) {
            int closeIdx = m_thinkParseBuffer.indexOf(m_activeThinkCloseTag, 0, Qt::CaseInsensitive);
            if (closeIdx >= 0) {
                m_thinkingChars += closeIdx;
                ++m_thinkingChunks;
                m_thinkParseBuffer.remove(0, closeIdx + m_activeThinkCloseTag.length());
                m_inThinkTag = false;
                m_activeThinkCloseTag.clear();
                m_stripLeadingWhitespaceAfterThink = true;
                setThinkingState(false);
                emitLiveProgress();
                continue;
            }

            int keepSuffix = 0;
            int maxCheck = qMin(m_thinkParseBuffer.length(), m_activeThinkCloseTag.length() - 1);
            for (int len = maxCheck; len >= 1; --len) {
                if (m_activeThinkCloseTag.startsWith(m_thinkParseBuffer.right(len), Qt::CaseInsensitive)) {
                    keepSuffix = len;
                    break;
                }
            }
            int consumedThinkChars = m_thinkParseBuffer.length() - keepSuffix;
            if (consumedThinkChars > 0) {
                m_thinkingChars += consumedThinkChars;
                ++m_thinkingChunks;
                emitLiveProgress();
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
                emitLiveProgress();
                continue;
            }

            for (const auto& tp : kThinkTagPairs) {
                QString closeStr = QString::fromLatin1(tp.close);
                int strayClose = m_thinkParseBuffer.indexOf(closeStr, 0, Qt::CaseInsensitive);
                if (strayClose >= 0) {
                    m_thinkingChars += strayClose;
                    ++m_thinkingChunks;
                    m_thinkParseBuffer.remove(0, strayClose + closeStr.length());
                    m_accumulatedResponse.clear();
                    m_stripLeadingWhitespaceAfterThink = true;
                    setThinkingState(false);
                    emitLiveProgress();
                    break;
                }
            }
            if (m_stripLeadingWhitespaceAfterThink) continue;

            if (m_accumulatedResponse.isEmpty() &&
                m_thinkParseBuffer.trimmed().isEmpty() &&
                m_thinkParseBuffer.length() < 16) {
                break;
            }

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
    if (!cancelledTaskId.isEmpty()) {
        emit debugLog("CANCEL", formatRequestStatsSummary("cancelled by user"), formatRequestStatsDetails());
    }
    setThinkingState(false);
    m_activeTaskId.clear();
    m_streamBuffer.clear();
    m_thinkParseBuffer.clear();
    m_inThinkTag = false;
    cleanupActiveReply();
}

void AiProvider::onTimeoutTriggered() {
    if (m_activeTaskId.isEmpty()) return;
    QString timedOutTaskId = m_activeTaskId;
    emit debugLog("TIMEOUT",
                  formatRequestStatsSummary(QString("timed out (%1 ms)").arg(m_activeTimeoutMs)),
                  formatRequestStatsDetails());
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

    QString fullUrlStr;
    if (cfg.backend == AiBackendType::Ollama) {
        fullUrlStr = base + "/api/tags";
    } else if (cfg.backend == AiBackendType::KoboldCpp && !base.endsWith("/v1", Qt::CaseInsensitive)) {
        fullUrlStr = base + "/api/v1/model";
    } else {
        fullUrlStr = buildOpenAiEndpoint(base, "/models");
    }

    QUrl url(fullUrlStr);
    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    if (!cfg.apiKey.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + cfg.apiKey).toUtf8());

    QElapsedTimer fetchTimer;
    fetchTimer.start();
    emit debugLog("REQUEST", "GET " + url.toString(), {});

    QNetworkReply* reply = m_netManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fetchTimer]() {
        QStringList models;
        QByteArray raw = reply->readAll();
        qint64 ms = fetchTimer.elapsed();
        if (reply->error() == QNetworkReply::NoError) {
            emit debugLog("RESPONSE", QString("Models Fetched OK (%1 ms)").arg(ms), QString::fromUtf8(raw));
            auto obj = QJsonDocument::fromJson(raw).object();
            if (obj.contains("models") && obj["models"].isArray()) {
                for (const auto& v : obj["models"].toArray())
                    models.append(v.toObject()["name"].toString());
            } else if (obj.contains("data") && obj["data"].isArray()) {
                for (const auto& v : obj["data"].toArray())
                    models.append(v.toObject()["id"].toString());
            } else if (obj.contains("result")) {
                QString m = obj["result"].toString();
                if (!m.isEmpty()) models.append(m);
            }
        } else {
            emit debugLog("ERROR",
                          QString("Model Fetch Failed (%1 ms): %2").arg(ms).arg(reply->errorString()),
                          QString::fromUtf8(raw));
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
    m_maxOutputTokens = maxTokens;
    m_promptChars = systemPrompt.length() + userPrompt.length();
    m_estimatedPromptTokens = (m_promptChars + 3) / 4;
    m_ttftMs = -1;
    m_thinkingChunks = 0;
    m_thinkingChars = 0;
    m_outputChunks = 0;
    m_apiPromptTokens = 0;
    m_apiCompletionTokens = 0;
    m_apiTokensPerSec = 0.0;
    m_requestTimer.start();

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
    } else if (cfg.backend == AiBackendType::KoboldCpp && !base.endsWith("/v1", Qt::CaseInsensitive)) {
        url = QUrl(base + (stream ? "/api/extra/generate/stream" : "/api/v1/generate"));
        payload = {
            {"prompt", systemPrompt.isEmpty() ? userPrompt : (systemPrompt + "\n\n" + userPrompt)},
            {"temperature", cfg.temperature},
            {"top_p", cfg.topP},
            {"max_length", maxTokens},
            {"max_context_length", cfg.contextWindow}
        };
    } else {
        url = QUrl(buildOpenAiEndpoint(base, "/chat/completions"));
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
                  QString("POST %1 [Task: %2, Stream: %3, Prompt: ~%4 tok, MaxTokens: %5]")
                      .arg(url.toString(), taskId, stream ? "true" : "false")
                      .arg(m_estimatedPromptTokens)
                      .arg(maxTokens),
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
        extractApiUsageAndTimings(obj);

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
        extractApiUsageAndTimings(obj);

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
        emit debugLog("ERROR",
                      formatRequestStatsSummary("failed: " + errStr),
                      formatRequestStatsDetails() + "\n" + QString::fromUtf8(m_streamBuffer));
        setThinkingState(false);
        m_activeTaskId.clear();
        reply->deleteLater();
        emit requestError(taskId, errStr);
        return;
    }

    QByteArray remaining = reply->readAll();
    if (!m_isStreaming) {
        recordFirstTokenIfNeeded();
        m_streamBuffer.append(remaining);
        auto obj = QJsonDocument::fromJson(m_streamBuffer).object();
        extractApiUsageAndTimings(obj);
        m_accumulatedResponse = extractNonStreamText(m_activeBackend, obj);
        m_outputChunks = qMax(1, (m_accumulatedResponse.length() + 3) / 4);
        emit debugLog("RESPONSE",
                      QString("Task %1 completed (%2 bytes)").arg(taskId).arg(m_streamBuffer.size()),
                      QString::fromUtf8(m_streamBuffer));
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
        emit debugLog("RESPONSE",
                      QString("Stream Task %1 finished (%2 chars)")
                          .arg(taskId).arg(m_accumulatedResponse.size()),
                      m_accumulatedResponse);
    }

    emit debugLog("STATS", formatRequestStatsSummary("completed"), formatRequestStatsDetails());

    setThinkingState(false);
    m_activeTaskId.clear();
    reply->deleteLater();
    emit requestFinished(taskId, m_accumulatedResponse);
}
