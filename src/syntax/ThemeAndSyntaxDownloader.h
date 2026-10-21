// src/syntax/ThemeAndSyntaxDownloader.h
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QPair>
#include <QUrl>

class ThemeAndSyntaxDownloader : public QObject {
    Q_OBJECT
public:
    explicit ThemeAndSyntaxDownloader(QObject* parent = nullptr);

    void ensureResourcesPresent();
    void forceDownloadAll();

signals:
    void downloadProgress(int completed, int total, const QString& currentFile);
    void allDownloadsFinished();

private slots:
    void startNextDownload();

private:
    void generateBuiltInThemesOnDisk();
    void enqueueSyntaxFiles(bool overwrite);

    QNetworkAccessManager m_net;
    QQueue<QPair<QUrl, QString>> m_downloadQueue;
    int m_totalTasks = 0, m_completedTasks = 0;
    bool m_isBusy = false;
};