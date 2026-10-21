// src/syntax/ThemeAndSyntaxDownloader.cpp
#include "ThemeAndSyntaxDownloader.h"
#include "../core/ConfigManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>

ThemeAndSyntaxDownloader::ThemeAndSyntaxDownloader(QObject* parent) : QObject(parent) {}

void ThemeAndSyntaxDownloader::ensureResourcesPresent() {
    generateBuiltInThemesOnDisk();
    enqueueSyntaxFiles(false);
    if (!m_isBusy && !m_downloadQueue.isEmpty()) startNextDownload();
}

void ThemeAndSyntaxDownloader::forceDownloadAll() {
    generateBuiltInThemesOnDisk();
    enqueueSyntaxFiles(true);
    if (!m_isBusy && !m_downloadQueue.isEmpty()) startNextDownload();
}

void ThemeAndSyntaxDownloader::generateBuiltInThemesOnDisk() {
    QString dir = ConfigManager::instance().themesDirPath();
    auto writeTheme = [&](const QString& filename, const QJsonObject& json) {
        QString path = dir + "/" + filename;
        if (QFile::exists(path)) return;
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(json).toJson(QJsonDocument::Indented)); f.commit(); }
    };

    writeTheme("dracula.json", {
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

    writeTheme("monokai.json", {
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

    writeTheme("github-light.json", {
        {"name", "github-light"},
        {"editor", QJsonObject{{"background", "#ffffff"}, {"foreground", "#24292e"}, {"currentLine", "#f6f8fa"}, {"selection", "#c8e1ff"}, {"lineNumberBg", "#fafbfc"}, {"lineNumberFg", "#959da5"}}},
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

void ThemeAndSyntaxDownloader::enqueueSyntaxFiles(bool overwrite) {
    const QString baseUrl = "https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/";
    const QString syntaxDir = ConfigManager::instance().syntaxDirPath();
    const QStringList langs = {"cpp", "c", "python", "bash", "sql", "javascript"};

    for (const auto& lang : langs) {
        QString targetPath = syntaxDir + "/" + lang + ".xml";
        if (overwrite || !QFile::exists(targetPath))
            m_downloadQueue.enqueue({QUrl(baseUrl + lang + ".xml"), targetPath});
    }
    m_totalTasks = m_downloadQueue.size();
    m_completedTasks = 0;
}

void ThemeAndSyntaxDownloader::startNextDownload() {
    if (m_downloadQueue.isEmpty()) {
        m_isBusy = false;
        emit allDownloadsFinished();
        return;
    }
    m_isBusy = true;
    auto [url, destPath] = m_downloadQueue.dequeue();

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, destPath]() {
        if (reply->error() == QNetworkReply::NoError) {
            QSaveFile f(destPath);
            if (f.open(QIODevice::WriteOnly)) { f.write(reply->readAll()); f.commit(); }
        }
        reply->deleteLater();
        emit downloadProgress(++m_completedTasks, m_totalTasks, destPath);
        startNextDownload();
    });
}