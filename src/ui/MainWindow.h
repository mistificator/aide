// src/ui/MainWindow.h
#pragma once
#include <QMainWindow>
#include "../editor/CodeEditor.h"

class QTreeView;
class QLabel;
class QMenu;
class ProjectManager;
class ProjectTreeModel;
class EditorTabWidget;
class ContextInspectorDock;
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

private slots:
    void onOpenFile();
    void onOpenFolder();
    void onFindReplaceDialog();
    void onAiPreferences();
    void onActiveEditorChanged(CodeEditor* editor);
    void onEditorAiAction(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction);
    void onTaskCompleted(const AiTask& task, const QString& resultText);
    void onTaskFailed(const AiTask& task, const QString& errorText);
    void populateWindowMenu();
    void populateThemeMenu();

private:
    void setupUiAndDocks();
    void setupMenus();
    void setupConnections();
    static QString actionKeyForEnum(AiEditAction action);

    ProjectManager* m_projectMgr = nullptr;
    AiProvider* m_aiProvider = nullptr;
    AiTaskQueue* m_taskQueue = nullptr;
    SummaryWorker* m_summaryWorker = nullptr;
    ThemeAndSyntaxDownloader* m_downloader = nullptr;

    QDockWidget* m_projectDock = nullptr;
    QTreeView* m_projectTree = nullptr;
    ProjectTreeModel* m_fsModel = nullptr;
    EditorTabWidget* m_tabWidget = nullptr;
    ContextInspectorDock* m_inspectorDock = nullptr;

    QMenu* m_windowMenu = nullptr;
    QMenu* m_themeMenu = nullptr;
    QLabel* m_queueStatusLabel = nullptr;
    QLabel* m_langStatusLabel = nullptr;
};