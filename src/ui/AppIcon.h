// src/ui/AppIcon.h
#pragma once
#include <QIcon>
#include <QPixmap>

class AppIcon {
public:
    enum class ActionIcon {
        ProjectCustomPrompt,
        ProjectSummaryRebuild,
        ProjectRecursiveSummary,
        FileCustomPrompt,
        FileSummaryRebuild,
        CodeExplain,
        AddDocs
    };

    static QIcon appIcon();
    static QPixmap renderIconPixmap(int size);

    static QIcon actionIcon(ActionIcon type);
    static QPixmap renderActionPixmap(ActionIcon type, int size);
};
