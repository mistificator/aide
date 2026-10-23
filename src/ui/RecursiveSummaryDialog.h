// src/ui/RecursiveSummaryDialog.h
#pragma once
#include <QDialog>
#include <QString>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QCheckBox;
class ProjectManager;

class RecursiveSummaryDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecursiveSummaryDialog(ProjectManager* projMgr,
                                    const QString& rootDirPath,
                                    QWidget* parent = nullptr);

    QStringList selectedFilePaths() const;

private slots:
    void rebuildTree();
    void updateSelectionCountLabel();
    void checkMissingOnly();
    void checkAllAllowed();
    void uncheckAll();

private:
    bool populateDirectoryItem(const QString& dirPath, QTreeWidgetItem* parentItem);
    void collectCheckedFiles(QTreeWidgetItem* item, QStringList& outPaths) const;
    void applyCheckFilter(QTreeWidgetItem* item, int mode);

    ProjectManager* m_projectMgr = nullptr;
    QString m_rootDirPath;
    QTreeWidget* m_tree = nullptr;
    QCheckBox* m_showSkippedCheck = nullptr;
    QLabel* m_countLabel = nullptr;
    bool m_updatingChecks = false;
};
