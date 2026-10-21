/*
// src/main.cpp
#include <QApplication>
#include <QMainWindow>
#include "core/ConfigManager.h"
#include "syntax/ThemeAndSyntaxDownloader.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("AIDE");
    QApplication::setOrganizationName("AIDE");

    auto& cfg = ConfigManager::instance();
    cfg.initializeStorage();
    cfg.loadConfig();

    ThemeAndSyntaxDownloader downloader;
    downloader.ensureResourcesPresent();

    // Временное окно до подключения полного MainWindow на Этапе 4
    QMainWindow win;
    win.setWindowTitle("AIDE - AI Development Environment");
    win.resize(1280, 800);
    win.show();

    return app.exec();
}
*/

// src/main.cpp
#include <QApplication>
#include "core/ConfigManager.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("AIDE");
    QApplication::setOrganizationName("AIDE");

    auto& cfg = ConfigManager::instance();
    cfg.initializeStorage();
    cfg.loadConfig();

    MainWindow win;
    win.show();

    return app.exec();
}