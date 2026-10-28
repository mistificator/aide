// src/ui/MainWindow.h
#pragma once
#include <QMainWindow>
#include <QMap>
#include <QList>
#include <QPointer>
#include "../editor/CodeEditor.h"

class QTreeView;
class QLabel;
class QPushButton;
class QComboBox;
class QMenu;
class QToolBar;
class QAction;
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
class PromptHistoryDock;
class ProjectPromptResultDialog;
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
    void onRecursiveProjectSummary(const QString& startDir = {});
    void onRebuildProjectSummary();
    void onProjectCustomPrompt(const QString& initialPromptText = {});
    void onProjectTreeContextMenu(const QPoint& pos);

    void onActiveEditorChanged(CodeEditor* editor);
    void onEditorAiAction(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction);
    void onTaskStreamChunk(const AiTask& task, const QString& chunk);
    void onTaskCompleted(const AiTask& task, const QString& resultText);
    void onTaskFailed(const AiTask& task, const QString& errorText);
    void onTaskCancelled(const AiTask& task);

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
    void updateAiActionsState();
    void applyProjectExplorerFilter();
    void rebuildSingleFileSummary(const QString& filePath,
                                  bool checkLimits = true,
                                  int maxTokensOverride = 0,
                                  int ctxWindowOverride = 0);
    void clearSingleFileSummary(const QString& filePath);
    QMap<QString, QString> collectOpenEditorBuffers() const;
    bool verifyAndAdjustContextLimits(const QString& taskTitle,
                                      const QString& systemPrompt,
                                      const QString& userPrompt,
                                      int minRecommendedOutputTokens,
                                      bool isSummaryTask,
                                      int& inOutMaxOutputTokens,
                                      int& inOutContextWindow);
    static int roundUpTokenStep(int needed, int maxCap);
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
    QLabel* m_projectPathLabel = nullptr;
    QComboBox* m_projectFilterCombo = nullptr;
    QTreeView* m_projectTree = nullptr;
    ProjectTreeModel* m_fsModel = nullptr;

    ContextInspectorDock* m_inspectorDock = nullptr;
    PromptHistoryDock* m_promptHistoryDock = nullptr;
    AiDebugConsoleDock* m_debugConsoleDock = nullptr;
    AiTaskQueueDock* m_taskQueueDock = nullptr;

    QMap<QString, QPointer<ProjectPromptResultDialog>> m_projectPromptWindows;

    QToolBar* m_aiToolBar = nullptr;
    QAction* m_actProjCustomPrompt = nullptr;
    QAction* m_actProjSummaryRebuild = nullptr;
    QAction* m_actProjRecursiveSummary = nullptr;
    QAction* m_actFileCustomPrompt = nullptr;
    QAction* m_actFileSummaryRebuild = nullptr;
    QAction* m_actCodeExplain = nullptr;
    QAction* m_actAddDocs = nullptr;
    QList<QAction*> m_otherFileAiActions;

    QMenu* m_recentProjectsMenu = nullptr;
    QMenu* m_windowMenu = nullptr;
    QMenu* m_themeMenu = nullptr;
    QMenu* m_langMenu = nullptr;

    QPushButton* m_queueStatusBtn = nullptr;
    QLabel* m_langStatusLabel = nullptr;
};
