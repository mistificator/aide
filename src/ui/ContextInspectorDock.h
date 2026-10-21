// src/ui/ContextInspectorDock.h
#pragma once
#include <QDockWidget>

class QPlainTextEdit;
class QLabel;
class QPushButton;
class ProjectManager;

class ContextInspectorDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ContextInspectorDock(ProjectManager* projMgr, QWidget* parent = nullptr);

    void setActiveFile(const QString& filePath);
    void refreshDisplay();

signals:
    void rebuildFileSummaryClicked(const QString& filePath);
    void rebuildProjectSummaryClicked();

private:
    void updateEffectivePreview();

    ProjectManager* m_projectMgr = nullptr;
    QString m_activeFilePath;
    bool m_updatingUi = false;

    QLabel* m_fileHeaderLabel = nullptr;
    QLabel* m_tokenEstimateLabel = nullptr;
    QPlainTextEdit* m_projectPromptEdit = nullptr;
    QPlainTextEdit* m_projectSummaryView = nullptr;
    QPlainTextEdit* m_filePromptEdit = nullptr;
    QPlainTextEdit* m_fileSummaryView = nullptr;
    QPlainTextEdit* m_effectiveContextPreview = nullptr;
    QPushButton* m_rebuildFileBtn = nullptr;
    QPushButton* m_rebuildProjBtn = nullptr;
};