// src/ui/RecursiveSummaryDialog.h
#pragma once
#include <QDialog>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QIcon>
#include <QSet>
#include <QVector>
#include <QRegularExpression>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QCheckBox;
class QProgressBar;
class ProjectManager;

enum class FileSelectorPreset {
    MissingSummariesDefault,
    IndexedFilesDefault
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
    int totalFilesCount() const { return m_totalFilesFound; }
    qint64 selectedFilesTotalBytes() const;
    bool isLoading() const { return m_isLoading; }

public slots:
    void rebuildTree();
    void checkIndexedOnly();
    void checkMissingOnly();
    void checkAllAllowed();
    void uncheckAll();

signals:
    void selectionChanged(int selectedCount);
    void loadingFinished();

private:
    struct FastFilterRules {
        QSet<QString> allowedExtensions;
        QSet<QString> exactAllowedNames;
        QVector<QRegularExpression> wildcardAllowedNames;
        QSet<QString> exactIgnoredDirs;
        QVector<QRegularExpression> wildcardIgnoredDirs;

        bool isDirIgnored(const QString& dirName) const;
        bool isFileAllowed(const QString& fileName, const QString& suffix) const;
    };

    FastFilterRules buildFastFilterRules() const;
    int countDirectoriesQuick(const QString& dirPath, const FastFilterRules& rules, int& abortCounter) const;
    bool populateDirectoryItem(const QString& dirPath,
                               const QString& relDirPrefix,
                               QTreeWidgetItem* parentItem,
                               const FastFilterRules& rules,
                               int& processedDirs,
                               int totalDirs,
                               int& totalFilesFound);

    void collectCheckedFiles(QTreeWidgetItem* item, QStringList& outPaths) const;
    void collectCheckedBytes(QTreeWidgetItem* item, qint64& outBytes) const;
    void applyCheckFilter(QTreeWidgetItem* item, int mode);
    void notifySelectionChanged();

    ProjectManager* m_projectMgr = nullptr;
    QString m_rootDirPath;
    FileSelectorPreset m_preset = FileSelectorPreset::MissingSummariesDefault;
    QProgressBar* m_progressBar = nullptr;
    QTreeWidget* m_tree = nullptr;
    QCheckBox* m_showSkippedCheck = nullptr;
    QIcon m_dirIcon;
    QIcon m_fileIcon;
    QIcon m_subprojectIcon;
    bool m_updatingChecks = false;
    bool m_isLoading = false;
    int m_buildGeneration = 0;
    int m_totalFilesFound = 0;
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
