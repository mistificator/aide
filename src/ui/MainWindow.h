// src/ui/MainWindow.h
#pragma once
#include <QMainWindow>
#include "../editor/CodeEditor.h"

class QTreeView;
class QLabel;
class QPushButton;
class QMenu;
class QStackedWidget;
class ProjectManager;
class ProjectTreeModel;
class EditorTabWidget;
class ContextInspectorDock;
class AiDebugConsoleDock;
class AiTaskQueueDock;
class AiProvider;
class AiTaskQueue;
class SummaryWorker;
class ThemeAndSyntaxDownloader;
struct AiTask;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpenFile();
    void onOpenFolder();
    void onCloseAllProjects();
    void onCompareUnsavedDiff(CodeEditor* targetEditor = nullptr);
    void onFindReplaceDialog();
    void onEditorPreferences();
    void onAiPreferences();
    void onActiveEditorChanged(CodeEditor* editor);
    void onEditorAiAction(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction);
    void onTaskCompleted(const AiTask& task, const QString& resultText);
    void onTaskFailed(const AiTask& task, const QString& errorText);
    void populateRecentProjectsMenu();
    void populateWindowMenu();
    void populateThemeMenu();
    void populateSyntaxMenu();

private:
    void setupUiAndDocks();
    void setupMenus();
    void setupConnections();
    void openProjectAtPath(const QString& dirPath);
    void updateCentralViewState();
    static QString actionKeyForEnum(AiEditAction action);

    ProjectManager* m_projectMgr = nullptr;
    AiProvider* m_aiProvider = nullptr;
    AiTaskQueue* m_taskQueue = nullptr;
    SummaryWorker* m_summaryWorker = nullptr;
    ThemeAndSyntaxDownloader* m_downloader = nullptr;

    QStackedWidget* m_centralStack = nullptr;
    QWidget* m_welcomeWidget = nullptr;
    EditorTabWidget* m_tabWidget = nullptr;

    QDockWidget* m_projectDock = nullptr;
    QStackedWidget* m_projectDockStack = nullptr;
    QTreeView* m_projectTree = nullptr;
    ProjectTreeModel* m_fsModel = nullptr;
    ContextInspectorDock* m_inspectorDock = nullptr;
    AiDebugConsoleDock* m_debugConsoleDock = nullptr;
    AiTaskQueueDock* m_taskQueueDock = nullptr;

    QMenu* m_recentProjectsMenu = nullptr;
    QMenu* m_windowMenu = nullptr;
    QMenu* m_themeMenu = nullptr;
    QMenu* m_langMenu = nullptr;
    QPushButton* m_queueStatusBtn = nullptr;
    QLabel* m_langStatusLabel = nullptr;
};
