// src/ui/RecursiveSummaryDialog.h
#pragma once
#include <QDialog>
#include <QWidget>
#include <QString>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QCheckBox;
class ProjectManager;

enum class FileSelectorPreset {
    MissingSummariesDefault, // По умолчанию выбраны неиндексированные разрешённые файлы
    IndexedFilesDefault      // По умолчанию выбраны проиндексированные файлы (для промпта проекта)
};

class ProjectFileSelectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit ProjectFileSelectorWidget(ProjectManager* projMgr,
                                       const QString& rootDirPath,
                                       FileSelectorPreset preset,
                                       QWidget* parent = nullptr);

    QStringList selectedFilePaths() const;
    int selectedFilesCount() const;

public slots:
    void rebuildTree();
    void checkIndexedOnly();
    void checkMissingOnly();
    void checkAllAllowed();
    void uncheckAll();

signals:
    void selectionChanged(int selectedCount);

private:
    bool populateDirectoryItem(const QString& dirPath, QTreeWidgetItem* parentItem);
    void collectCheckedFiles(QTreeWidgetItem* item, QStringList& outPaths) const;
    void applyCheckFilter(QTreeWidgetItem* item, int mode);
    void notifySelectionChanged();

    ProjectManager* m_projectMgr = nullptr;
    QString m_rootDirPath;
    FileSelectorPreset m_preset = FileSelectorPreset::MissingSummariesDefault;
    QTreeWidget* m_tree = nullptr;
    QCheckBox* m_showSkippedCheck = nullptr;
    bool m_updatingChecks = false;
};

class RecursiveSummaryDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecursiveSummaryDialog(ProjectManager* projMgr,
                                    const QString& rootDirPath,
                                    QWidget* parent = nullptr);

    QStringList selectedFilePaths() const;

private slots:
    void updateSelectionCountLabel(int count);

private:
    ProjectFileSelectorWidget* m_selector = nullptr;
    QLabel* m_countLabel = nullptr;
};
