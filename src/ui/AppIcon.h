// src/ui/AppIcon.h
#pragma once
#include <QIcon>
#include <QPixmap>

class AppIcon {
public:
    static QIcon appIcon();
    static QPixmap renderIconPixmap(int size);
};
