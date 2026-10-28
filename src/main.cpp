// src/main.cpp
#include <QApplication>
#include "core/ConfigManager.h"
#include "syntax/ColorScheme.h"
#include "ui/AppIcon.h"
#include "ui/MainWindow.h"

#ifndef AIDE_VERSION_STRING
#define AIDE_VERSION_STRING "0.0.0.0"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("AIDE");
    QApplication::setOrganizationName("AIDE");
    QApplication::setApplicationVersion(QStringLiteral(AIDE_VERSION_STRING));
    app.setWindowIcon(AppIcon::appIcon());

    auto& cfg = ConfigManager::instance();
    cfg.initializeStorage();
    cfg.loadConfig();

    ColorScheme::installWindowThemeFilter(&app);
    ColorScheme::applyApplicationTheme(cfg.activeTheme());
    QObject::connect(&cfg, &ConfigManager::themeChanged, &app, [](const QString& themeName) {
        ColorScheme::applyApplicationTheme(themeName);
    });

    MainWindow win;
    win.show();
    ColorScheme::applyWindowDarkTitleBar(&win, ColorScheme::isDarkTheme(cfg.activeTheme()));

    return app.exec();
}
