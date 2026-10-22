// src/ui/ContextInspectorDock.h
#pragma once
#include <QDockWidget>
#include <QString>

class QPlainTextEdit;
class QTextEdit;
class QLabel;
class QPushButton;
class ProjectManager;

class ContextInspectorDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ContextInspectorDock(ProjectManager* projMgr, QWidget* parent = nullptr);

    void setActiveFile(const QString& filePath);
    void refreshDisplay();

public slots:
    void appendLiveSummaryChunk(const QString& relFilePath, bool isProjectSummary, const QString& chunk);

signals:
    void rebuildFileSummaryClicked(const QString& filePath);
    void rebuildProjectSummaryClicked();

private:
    void updateEffectivePreview();
    static void setupMarkdownViewerStyle(QTextEdit* viewer);
    static void renderMarkdownPreservingScroll(QTextEdit* viewer, const QString& markdownText, bool scrollToBottom = false);

    ProjectManager* m_projectMgr = nullptr;
    QString m_activeFilePath;
    QString m_liveFileMarkdown;
    QString m_liveProjectMarkdown;
    bool m_updatingUi = false;
    bool m_editingProjectMarkdown = false;

    QLabel* m_fileHeaderLabel = nullptr;
    QLabel* m_tokenEstimateLabel = nullptr;
    QPlainTextEdit* m_projectPromptEdit = nullptr;
    QTextEdit* m_projectSummaryView = nullptr;
    QPlainTextEdit* m_filePromptEdit = nullptr;
    QTextEdit* m_fileSummaryView = nullptr;
    QTextEdit* m_effectiveContextPreview = nullptr;
    QPushButton* m_rebuildFileBtn = nullptr;
    QPushButton* m_rebuildProjBtn = nullptr;
    QPushButton* m_editProjSummaryBtn = nullptr;
};
