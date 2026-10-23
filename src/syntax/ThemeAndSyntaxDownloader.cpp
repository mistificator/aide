// src/syntax/ThemeAndSyntaxDownloader.cpp
#include "ThemeAndSyntaxDownloader.h"
#include "LexicalHighlighter.h"
#include "../core/ConfigManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ThemeAndSyntaxDownloader::ThemeAndSyntaxDownloader(QObject* parent) : QObject(parent) {}

void ThemeAndSyntaxDownloader::ensureResourcesPresent() {
    generateBuiltInThemesOnDisk();
    QStringList missing;
    for (const auto& rule : ConfigManager::instance().fileTypeRules()) {
        if (!rule.syntaxFile.isEmpty()) {
            QString localPath = ConfigManager::instance().syntaxDirPath() + "/" + rule.syntaxFile;
            if (!QFile::exists(localPath)) missing.append(rule.syntaxFile);
        }
    }
    if (!missing.isEmpty()) downloadSpecificSyntaxFiles(missing, false);
}

void ThemeAndSyntaxDownloader::forceDownloadAll() {
    generateBuiltInThemesOnDisk();
    QStringList allFiles;
    for (const auto& rule : ConfigManager::instance().fileTypeRules()) {
        if (!rule.syntaxFile.isEmpty() && !allFiles.contains(rule.syntaxFile))
            allFiles.append(rule.syntaxFile);
    }
    downloadSpecificSyntaxFiles(allFiles, true);
}

void ThemeAndSyntaxDownloader::downloadSpecificSyntaxFiles(const QStringList& fileNames, bool overwrite) {
    QString baseUrl = ConfigManager::instance().syntaxBaseUrl();
    if (!baseUrl.endsWith('/')) baseUrl += '/';

    for (const QString& fname : fileNames) {
        QString localPath = ConfigManager::instance().syntaxDirPath() + "/" + fname;
        if (!overwrite && QFile::exists(localPath)) continue;
        m_downloadQueue.enqueue({QUrl(baseUrl + fname), localPath});
    }

    m_totalTasks = m_downloadQueue.size();
    m_completedTasks = 0;
    if (!m_isBusy && !m_downloadQueue.isEmpty()) {
        m_isBusy = true;
        startNextDownload();
    } else if (m_downloadQueue.isEmpty()) {
        emit allDownloadsFinished();
    }
}

void ThemeAndSyntaxDownloader::fetchKdeSyntaxRepositoryIndex() {
    // Получаем полный индекс всех XML-файлов синтаксиса из репозитория KDE
    QUrl apiUrl("https://api.github.com/repos/KDE/syntax-highlighting/contents/data/syntax");
    QNetworkRequest req(apiUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, "AIDE-Qt6-Editor");
    req.setTransferTimeout(20000);

    QNetworkReply* reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit kdeSyntaxIndexFetched({}, reply->errorString());
            return;
        }
        QStringList xmlFiles;
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        for (const auto& item : arr) {
            QString name = item.toObject()["name"].toString();
            if (name.endsWith(".xml", Qt::CaseInsensitive))
                xmlFiles.append(name);
        }
        xmlFiles.sort(Qt::CaseInsensitive);
        emit kdeSyntaxIndexFetched(xmlFiles, {});
    });
}

void ThemeAndSyntaxDownloader::startNextDownload() {
    if (m_downloadQueue.isEmpty()) {
        m_isBusy = false;
        emit allDownloadsFinished();
        return;
    }

    auto [url, destPath] = m_downloadQueue.dequeue();
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);

    QNetworkReply* reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, destPath]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (!data.isEmpty()) {
                QSaveFile f(destPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.commit();
                }
                registerDownloadedSyntaxMetadata(destPath);
            }
        }
        reply->deleteLater();
        ++m_completedTasks;
        emit downloadProgress(m_completedTasks, m_totalTasks, QFileInfo(destPath).fileName());
        startNextDownload();
    });
}

void ThemeAndSyntaxDownloader::registerDownloadedSyntaxMetadata(const QString& localXmlPath) {
    auto meta = LexicalHighlighter::inspectSyntaxXml(localXmlPath);
    if (!meta.valid) return;

    QString fileName = QFileInfo(localXmlPath).fileName();
    QString langId = QFileInfo(localXmlPath).baseName().toLower();

    auto existing = ConfigManager::instance().ruleForLanguage(langId);
    FileTypeRule rule;
    rule.langId = langId;
    rule.displayName = meta.name;
    rule.syntaxFile = fileName;
    rule.extensions = !existing.extensions.isEmpty() && existing.syntaxFile == fileName
                          ? existing.extensions
                          : meta.extensions;
    if (rule.extensions.isEmpty()) rule.extensions = {langId};
    ConfigManager::instance().upsertFileTypeRule(rule);
}

void ThemeAndSyntaxDownloader::generateBuiltInThemesOnDisk() {
    QString themesDir = ConfigManager::instance().themesDirPath();
    auto writeTheme = [&](const QString& filename, const QJsonObject& obj) {
        QString p = themesDir + "/" + filename;
        if (QFile::exists(p)) return;
        QSaveFile f(p);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(obj).toJson());
            f.commit();
        }
    };

    writeTheme("dracula.json", QJsonObject{
                                   {"name", "dracula"},
                                   {"editor", QJsonObject{{"background", "#282a36"}, {"foreground", "#f8f8f2"}, {"currentLine", "#44475a"}, {"selection", "#6272a4"}, {"lineNumberBg", "#21222c"}, {"lineNumberFg", "#6272a4"}}},
                                   {"tokens", QJsonObject{
                                                  {"keyword", QJsonObject{{"color", "#ff79c6"}, {"bold", true}}},
                                                  {"datatype", QJsonObject{{"color", "#8be9fd"}, {"italic", true}}},
                                                  {"function", QJsonObject{{"color", "#50fa7b"}}},
                                                  {"string", QJsonObject{{"color", "#f1fa8c"}}},
                                                  {"comment", QJsonObject{{"color", "#6272a4"}, {"italic", true}}},
                                                  {"number", QJsonObject{{"color", "#bd93f9"}}},
                                                  {"preprocessor", QJsonObject{{"color", "#ffb86c"}}},
                                                  {"operator", QJsonObject{{"color", "#ff79c6"}}},
                                                  {"error", QJsonObject{{"color", "#ff5555"}, {"bold", true}}}
                                              }}
                               });

    writeTheme("monokai.json", QJsonObject{
                                   {"name", "monokai"},
                                   {"editor", QJsonObject{{"background", "#272822"}, {"foreground", "#f8f8f2"}, {"currentLine", "#3e3d32"}, {"selection", "#49483e"}, {"lineNumberBg", "#1e1f1c"}, {"lineNumberFg", "#90908a"}}},
                                   {"tokens", QJsonObject{
                                                  {"keyword", QJsonObject{{"color", "#f92672"}, {"bold", true}}},
                                                  {"datatype", QJsonObject{{"color", "#66d9ef"}, {"italic", true}}},
                                                  {"function", QJsonObject{{"color", "#a6e22e"}}},
                                                  {"string", QJsonObject{{"color", "#e6db74"}}},
                                                  {"comment", QJsonObject{{"color", "#75715e"}, {"italic", true}}},
                                                  {"number", QJsonObject{{"color", "#ae81ff"}}},
                                                  {"preprocessor", QJsonObject{{"color", "#fd971f"}}},
                                                  {"operator", QJsonObject{{"color", "#f92672"}}},
                                                  {"error", QJsonObject{{"color", "#f92672"}, {"bold", true}}}
                                              }}
                               });

    writeTheme("github-light.json", QJsonObject{
                                        {"name", "github-light"},
                                        {"editor", QJsonObject{{"background", "#ffffff"}, {"foreground", "#24292e"}, {"currentLine", "#f6f8fa"}, {"selection", "#c8e1ff"}, {"lineNumberBg", "#f6f8fa"}, {"lineNumberFg", "#959da5"}}},
                                        {"tokens", QJsonObject{
                                                       {"keyword", QJsonObject{{"color", "#d73a49"}, {"bold", true}}},
                                                       {"datatype", QJsonObject{{"color", "#005cc5"}}},
                                                       {"function", QJsonObject{{"color", "#6f42c1"}}},
                                                       {"string", QJsonObject{{"color", "#032f62"}}},
                                                       {"comment", QJsonObject{{"color", "#6a737d"}, {"italic", true}}},
                                                       {"number", QJsonObject{{"color", "#005cc5"}}},
                                                       {"preprocessor", QJsonObject{{"color", "#e36209"}}},
                                                       {"operator", QJsonObject{{"color", "#d73a49"}}},
                                                       {"error", QJsonObject{{"color", "#cb2431"}, {"bold", true}}}
                                                   }}
                                    });
}
