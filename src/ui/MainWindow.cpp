// src/ui/MainWindow.cpp
#include "MainWindow.h"
#include "AppIcon.h"
#include "ContextInspectorDock.h"
#include "AiPreferencesDialog.h"
#include "EditorPreferencesDialog.h"
#include "RecursiveSummaryDialog.h"
#include "AiDebugConsoleDock.h"
#include "AiTaskQueueDock.h"
#include "DiffViewerDialog.h"
#include "MarkdownRenderer.h"
#include "PromptHistoryDock.h"
#include "../core/ConfigManager.h"
#include "../core/ProjectManager.h"
#include "../editor/EditorTabWidget.h"
#include "../ai/AiProvider.h"
#include "../ai/AiTaskQueue.h"
#include "../ai/SummaryWorker.h"
#include "../syntax/ThemeAndSyntaxDownloader.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QTreeView>
#include <QFileSystemModel>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QStackedWidget>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QHeaderView>
#include <QApplication>

class ProjectTreeModel : public QFileSystemModel {
public:
    ProjectTreeModel(ProjectManager* pm, QObject* parent = nullptr) : QFileSystemModel(parent), m_pm(pm) {}
    QVariant data(const QModelIndex& index, int role) const override {
        if (index.column() == 0 && !isDir(index)) {
            QString path = filePath(index);
            auto fc = m_pm->fileContext(path);
            bool hasCreatedSummary = fc.summaryCreated || !fc.aiSummary.isEmpty();
            if (role == Qt::ForegroundRole) {
                if (fc.isSummaryPending) return QColor("#ffb86c");
                if (hasCreatedSummary) return QColor("#50fa7b");
            } else if (role == Qt::ToolTipRole) {
                if (fc.isSummaryPending) return "AI Summary: Indexing in background...";
                if (!fc.aiSummary.isEmpty())
                    return QString("AI Summary Ready (%1):\n%2").arg(fc.llmModel, fc.aiSummary.left(300));
                if (fc.summaryCreated)
                    return "AI Summary: Created (intentionally empty / cleared)";
                if (!ConfigManager::instance().shouldGenerateSummaryForFile(path))
                    return "AI Summary: Not required according to AI Behavior settings";
                return "AI Summary: Not indexed yet (open file or run recursive summary)";
            }
        }
        return QFileSystemModel::data(index, role);
    }
private:
    ProjectManager* m_pm;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
    m_projectMgr(new ProjectManager(this)),
    m_aiProvider(new AiProvider(this)),
    m_taskQueue(new AiTaskQueue(m_aiProvider, this)),
    m_summaryWorker(new SummaryWorker(m_projectMgr, m_taskQueue, this)),
    m_downloader(new ThemeAndSyntaxDownloader(this)) {
    setWindowTitle("AIDE - AI Development Environment");
    setWindowIcon(AppIcon::appIcon());
    resize(1400, 860);
    setAcceptDrops(true);

    setupUiAndDocks();
    setupMenus();
    setupConnections();

    // Восстановление геометрии главного окна и состояния док-панелей
    const QByteArray savedGeom = ConfigManager::instance().windowGeometry();
    if (!savedGeom.isEmpty()) {
        restoreGeometry(savedGeom);
    }
    const QByteArray savedState = ConfigManager::instance().windowState();
    if (!savedState.isEmpty()) {
        restoreState(savedState);
    }

    // Гарантируем адекватную начальную ширину правых док-панелей (не более ~32% ширины окна)
    int maxRightDockW = qMax(340, width() / 3);
    if (m_inspectorDock->width() > maxRightDockW || savedState.isEmpty()) {
        resizeDocks({m_inspectorDock}, {360}, Qt::Horizontal);
    }

    m_downloader->ensureResourcesPresent();

    QString lastProj = ConfigManager::instance().lastProjectPath();
    if (!lastProj.isEmpty() && QDir(lastProj).exists()) {
        openProjectAtPath(lastProj);
    } else {
        updateCentralViewState();
    }
}

void MainWindow::setupUiAndDocks() {
    m_centralStack = new QStackedWidget(this);

    m_welcomeWidget = new QWidget(m_centralStack);
    auto* welcomeLay = new QVBoxLayout(m_welcomeWidget);
    auto* welcomeIconLabel = new QLabel(m_welcomeWidget);
    welcomeIconLabel->setPixmap(AppIcon::renderIconPixmap(72));
    welcomeIconLabel->setAlignment(Qt::AlignCenter);

    auto* welcomeLabel = new QLabel(
        "<div align='center'>"
        "<h2 style='color:#bd93f9;'>No Project or File open</h2>"
        "<p style='color:#888899; font-size:14px;'>"
        "Open a project folder via <b>File &rarr; Open Folder / Project... (Ctrl+Shift+O)</b><br>"
        "or open a file via <b>File &rarr; Open File... (Ctrl+O)</b><br>"
        "or <b>drag and drop</b> a project folder or source files into this area."
        "</p></div>",
        m_welcomeWidget);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLay->addStretch();
    welcomeLay->addWidget(welcomeIconLabel);
    welcomeLay->addWidget(welcomeLabel);
    welcomeLay->addStretch();

    m_tabWidget = new EditorTabWidget(m_centralStack);
    m_centralStack->addWidget(m_welcomeWidget);
    m_centralStack->addWidget(m_tabWidget);
    setCentralWidget(m_centralStack);

    // Док 1: Project Explorer с отображением пути к проекту
    m_projectDock = new QDockWidget("Project Explorer", this);
    m_projectDock->setObjectName("ProjectExplorerDock");
    m_projectDockStack = new QStackedWidget(m_projectDock);
    auto* noProjExplorerLabel = new QLabel("No project loaded.\nUse File -> Open Folder / Project...", m_projectDockStack);
    noProjExplorerLabel->setAlignment(Qt::AlignCenter);
    noProjExplorerLabel->setStyleSheet("color: #777;");

    auto* projExplorerPage = new QWidget(m_projectDockStack);
    auto* projExplorerLay = new QVBoxLayout(projExplorerPage);
    projExplorerLay->setContentsMargins(0, 0, 0, 0);
    projExplorerLay->setSpacing(0);

    auto* pathHeaderBox = new QWidget(projExplorerPage);
    pathHeaderBox->setStyleSheet(
        "QWidget { background-color: rgba(128, 128, 150, 0.12); border-bottom: 1px solid rgba(128, 128, 150, 0.25); }");
    auto* pathHeaderLay = new QVBoxLayout(pathHeaderBox);
    pathHeaderLay->setContentsMargins(8, 5, 8, 5);
    pathHeaderLay->setSpacing(1);

    auto* pathTitleLbl = new QLabel("PROJECT PATH:", pathHeaderBox);
    pathTitleLbl->setStyleSheet("color: #888899; font-size: 9px; font-weight: bold; border: none; background: transparent;");
    m_projectPathLabel = new QLabel("-", pathHeaderBox);
    m_projectPathLabel->setStyleSheet("color: #bd93f9; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    m_projectPathLabel->setWordWrap(true);
    m_projectPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathHeaderLay->addWidget(pathTitleLbl);
    pathHeaderLay->addWidget(m_projectPathLabel);

    m_projectTree = new QTreeView(projExplorerPage);
    m_fsModel = new ProjectTreeModel(m_projectMgr, this);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_projectTree->setModel(m_fsModel);
    for (int col = 1; col < 4; ++col) m_projectTree->hideColumn(col);
    m_projectTree->header()->hide();
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);

    projExplorerLay->addWidget(pathHeaderBox);
    projExplorerLay->addWidget(m_projectTree, 1);

    m_projectDockStack->addWidget(noProjExplorerLabel);
    m_projectDockStack->addWidget(projExplorerPage);
    m_projectDock->setWidget(m_projectDockStack);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);

    // Док 2: AI Context Inspector
    m_inspectorDock = new ContextInspectorDock(m_projectMgr, this);
    m_inspectorDock->setObjectName("ContextInspectorDock");
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    // Док 2b: AI Prompt History
    m_promptHistoryDock = new PromptHistoryDock(this);
    m_promptHistoryDock->setObjectName("PromptHistoryDock");
    addDockWidget(Qt::RightDockWidgetArea, m_promptHistoryDock);
    tabifyDockWidget(m_inspectorDock, m_promptHistoryDock);
    m_inspectorDock->raise();

    // Док 3: AI Task Queue
    m_taskQueueDock = new AiTaskQueueDock(m_taskQueue, this);
    m_taskQueueDock->setObjectName("AiTaskQueueDock");
    addDockWidget(Qt::BottomDockWidgetArea, m_taskQueueDock);

    // Док 4: AI API Debug Console
    m_debugConsoleDock = new AiDebugConsoleDock(this);
    m_debugConsoleDock->setObjectName("AiDebugConsoleDock");
    addDockWidget(Qt::BottomDockWidgetArea, m_debugConsoleDock);

    tabifyDockWidget(m_taskQueueDock, m_debugConsoleDock);
    m_debugConsoleDock->hide();

    m_queueStatusBtn = new QPushButton("AI: Idle", this);
    m_queueStatusBtn->setFlat(true);
    m_queueStatusBtn->setToolTip("Click to open AI Task Queue");
    connect(m_queueStatusBtn, &QPushButton::clicked, this, [this]() {
        m_taskQueueDock->show();
        m_taskQueueDock->raise();
    });

    m_langStatusLabel = new QLabel("Lang: -", this);
    statusBar()->addPermanentWidget(m_langStatusLabel);
    statusBar()->addPermanentWidget(m_queueStatusBtn);
}

void MainWindow::setupMenus() {
    // 1. FILE
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("New File", QKeySequence::New, this, [this]() {
        m_tabWidget->newDocument();
        updateCentralViewState();
    });
    fileMenu->addAction("Open File...", QKeySequence::Open, this, &MainWindow::onOpenFile);
    fileMenu->addAction("Open Folder / Project...", QKeySequence("Ctrl+Shift+O"), this, &MainWindow::onOpenFolder);

    m_recentProjectsMenu = fileMenu->addMenu("Recent Projects");
    connect(m_recentProjectsMenu, &QMenu::aboutToShow, this, &MainWindow::populateRecentProjectsMenu);

    fileMenu->addAction("Close All Projects", QKeySequence("Ctrl+Shift+W"), this, &MainWindow::onCloseAllProjects);
    fileMenu->addSeparator();
    fileMenu->addAction("Save", QKeySequence::Save, m_tabWidget, &EditorTabWidget::saveCurrentDocument);
    fileMenu->addAction("Save As...", QKeySequence::SaveAs, m_tabWidget, &EditorTabWidget::saveCurrentDocumentAs);
    fileMenu->addAction("Save All", QKeySequence("Ctrl+Shift+S"), m_tabWidget, &EditorTabWidget::saveAllDocuments);
    fileMenu->addAction("Save Project (.aide)", this, [this]() {
        if (m_projectMgr->saveProjectFile()) statusBar()->showMessage("Project .aide file saved.", 3000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction("Compare Unsaved Changes (Diff)...", QKeySequence("Ctrl+D"), this, [this]() {
        onCompareUnsavedDiff(m_tabWidget->currentEditor());
    });
    fileMenu->addSeparator();
    fileMenu->addAction("Close Document", QKeySequence::Close, this, [this]() {
        if (m_tabWidget->currentIndex() != -1) {
            m_tabWidget->closeDocument(m_tabWidget->currentIndex());
            updateCentralViewState();
        }
    });
    fileMenu->addAction("Exit", QKeySequence::Quit, this, &QWidget::close);

    // 2. EDIT
    QMenu* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("Undo", QKeySequence::Undo, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->undo(); });
    editMenu->addAction("Redo", QKeySequence::Redo, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->redo(); });
    editMenu->addSeparator();
    editMenu->addAction("Cut", QKeySequence::Cut, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->cut(); });
    editMenu->addAction("Copy", QKeySequence::Copy, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->copy(); });
    editMenu->addAction("Paste", QKeySequence::Paste, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->paste(); });
    editMenu->addAction("Select All", QKeySequence::SelectAll, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->selectAll(); });
    editMenu->addSeparator();
    editMenu->addAction("Find / Replace...", QKeySequence::Find, this, &MainWindow::onFindReplaceDialog);
    editMenu->addSeparator();
    editMenu->addAction("Preferences...", this, &MainWindow::onEditorPreferences);

    // 3. VIEW
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_projectDock->toggleViewAction());
    viewMenu->addAction(m_inspectorDock->toggleViewAction());
    viewMenu->addAction(m_promptHistoryDock->toggleViewAction());
    viewMenu->addAction(m_taskQueueDock->toggleViewAction());
    viewMenu->addAction(m_debugConsoleDock->toggleViewAction());
    viewMenu->addSeparator();
    m_themeMenu = viewMenu->addMenu("Color Scheme");
    connect(m_themeMenu, &QMenu::aboutToShow, this, &MainWindow::populateThemeMenu);

    m_langMenu = viewMenu->addMenu("Syntax Highlighting");
    connect(m_langMenu, &QMenu::aboutToShow, this, &MainWindow::populateSyntaxMenu);

    viewMenu->addSeparator();
    viewMenu->addAction("Zoom In", QKeySequence::ZoomIn, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->zoomIn(1); });
    viewMenu->addAction("Zoom Out", QKeySequence::ZoomOut, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->zoomOut(1); });

    // 4. AI
    QMenu* aiMenu = menuBar()->addMenu("&AI");
    const QList<QPair<QString, AiEditAction>> aiActs = {
        {"Explain Code", AiEditAction::ExplainCode},
        {"Refactor && Clean Up", AiEditAction::RefactorCode},
        {"Find Bugs && Edge Cases", AiEditAction::FindBugs},
        {"Optimize Performance", AiEditAction::OptimizePerformance},
        {"Add Documentation / Comments", AiEditAction::AddDocumentation},
        {"Generate Unit Tests", AiEditAction::GenerateUnitTests},
        {"Complete / Continue Code", AiEditAction::CompleteCode},
        {"Custom Prompt...", AiEditAction::CustomPrompt}
    };
    for (const auto& [label, act] : aiActs)
        aiMenu->addAction(label, this, [this, act]() { if (auto* e = m_tabWidget->currentEditor()) e->triggerAiAction(act); });

    aiMenu->addSeparator();
    aiMenu->addAction("Rebuild File Summary", this, [this]() {
        if (auto* e = m_tabWidget->currentEditor(); e && !e->filePath().isEmpty())
            m_summaryWorker->forceRebuildFileSummary(e->filePath(), e->toPlainText().toUtf8());
    });
    aiMenu->addAction("Generate Summaries for Project Files (Recursive)...", this, [this]() {
        onRecursiveProjectSummary();
    });
    aiMenu->addAction("Rebuild Project Summary", m_summaryWorker, &SummaryWorker::forceRebuildProjectSummary);
    aiMenu->addAction("Cancel All Background Tasks", m_taskQueue, &AiTaskQueue::cancelAllBackgroundTasks);
    aiMenu->addAction("Download / Update Syntax && Themes", this, [this]() {
        statusBar()->showMessage("Downloading syntax & theme files...", 3000);
        m_downloader->forceDownloadAll();
    });
    aiMenu->addAction("Show AI Task Queue", this, [this]() {
        m_taskQueueDock->show();
        m_taskQueueDock->raise();
    });
    aiMenu->addAction("Show AI API Debug Console", this, [this]() {
        m_debugConsoleDock->show();
        m_debugConsoleDock->raise();
    });
    aiMenu->addAction("Show AI Prompt History", this, [this]() {
        m_promptHistoryDock->show();
        m_promptHistoryDock->raise();
    });
    aiMenu->addSeparator();
    aiMenu->addAction("Preferences...", this, &MainWindow::onAiPreferences);

    // 5. WINDOW
    m_windowMenu = menuBar()->addMenu("&Window");
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::populateWindowMenu);

    // 6. HELP
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About AIDE", this, [this]() {
        QMessageBox aboutBox(this);
        aboutBox.setWindowTitle("About AIDE");
        aboutBox.setIconPixmap(AppIcon::renderIconPixmap(64));
        aboutBox.setText(
            QString("<b>AIDE (AI Development Environment)</b><br>"
                    "<b>Version:</b> %1<br><br>"
                    "Qt6 Multi-Document Code Editor with Streaming LLM Integration, "
                    "Dynamic KDE Syntax Highlighting, Side-by-Side Diff, and Two-Tier Priority Queue.")
                .arg(QApplication::applicationVersion()));
        aboutBox.exec();
    });
    helpMenu->addAction("About Qt", qApp, &QApplication::aboutQt);
}

void MainWindow::setupConnections() {
    connect(m_projectTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!m_fsModel->isDir(idx)) {
            m_tabWidget->openFile(m_fsModel->filePath(idx));
            updateCentralViewState();
        }
    });

    connect(m_projectTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onProjectTreeContextMenu);

    connect(m_tabWidget, &EditorTabWidget::activeEditorChanged, this, [this](CodeEditor* ed) {
        onActiveEditorChanged(ed);
        updateCentralViewState();
    });
    connect(m_tabWidget, &EditorTabWidget::documentOpened, m_summaryWorker, &SummaryWorker::checkAndScheduleFileSummary);
    connect(m_tabWidget, &EditorTabWidget::documentSaved, m_summaryWorker, &SummaryWorker::checkAndScheduleFileSummary);
    connect(m_tabWidget, &EditorTabWidget::aiActionTriggered, this, &MainWindow::onEditorAiAction);
    connect(m_tabWidget, &EditorTabWidget::cancelAiTriggered, m_taskQueue, &AiTaskQueue::cancelTaskForEditor);
    connect(m_tabWidget, &EditorTabWidget::diffRequested, this, &MainWindow::onCompareUnsavedDiff);

    connect(m_aiProvider, &AiProvider::debugLog, m_debugConsoleDock, &AiDebugConsoleDock::appendLog);

    connect(m_inspectorDock, &ContextInspectorDock::rebuildFileSummaryClicked, this, &MainWindow::rebuildSingleFileSummary);
    connect(m_inspectorDock, &ContextInspectorDock::clearFileSummaryClicked, this, &MainWindow::clearSingleFileSummary);
    connect(m_inspectorDock, &ContextInspectorDock::rebuildProjectSummaryClicked, m_summaryWorker, &SummaryWorker::forceRebuildProjectSummary);

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString&) {
        m_projectTree->viewport()->update();
    });

    connect(m_taskQueue, &AiTaskQueue::queueStatusChanged, this, [this](int high, int low, bool busy) {
        if (!busy && high == 0 && low == 0)
            m_queueStatusBtn->setText("AI: Idle");
        else
            m_queueStatusBtn->setText(QString("AI: Busy (User: %1 | Background: %2)").arg(high).arg(low));
    });

    connect(m_taskQueue, &AiTaskQueue::taskCompleted, this, &MainWindow::onTaskCompleted);
    connect(m_taskQueue, &AiTaskQueue::taskFailed, this, &MainWindow::onTaskFailed);

    connect(m_downloader, &ThemeAndSyntaxDownloader::allDownloadsFinished, this, [this]() {
        statusBar()->showMessage("Syntax definitions and color schemes are up to date.", 4000);
        if (auto* e = m_tabWidget->currentEditor()) e->setSyntaxLanguage(e->syntaxLanguage());
    });

    connect(m_summaryWorker, &SummaryWorker::summaryStreamingChunk,
            m_inspectorDock, &ContextInspectorDock::appendLiveSummaryChunk);
    connect(m_summaryWorker, &SummaryWorker::summaryStreamingReset,
            m_inspectorDock, &ContextInspectorDock::resetLiveSummary);

    connect(m_promptHistoryDock, &PromptHistoryDock::usePromptRequested, this, [this](const QString& promptText) {
        CodeEditor* ed = m_tabWidget->currentEditor();
        if (!ed) {
            ed = m_tabWidget->newDocument();
            updateCentralViewState();
        }
        if (ed) {
            ed->triggerCustomPromptWithText(promptText);
        }
    });
}

void MainWindow::rebuildSingleFileSummary(const QString& filePath) {
    if (filePath.isEmpty()) return;
    if (auto* ed = m_tabWidget->findEditorByPath(filePath)) {
        m_summaryWorker->forceRebuildFileSummary(filePath, ed->toPlainText().toUtf8());
        return;
    }
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly)) {
        m_summaryWorker->forceRebuildFileSummary(filePath, f.readAll());
    }
}

void MainWindow::clearSingleFileSummary(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QString relPath = m_projectMgr->toRelativePath(filePath);
    m_taskQueue->cancelFileSummaryTask(relPath);

    QString sha256;
    if (auto* ed = m_tabWidget->findEditorByPath(filePath)) {
        sha256 = ProjectManager::computeSha256(ed->toPlainText().toUtf8());
    } else {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly)) {
            sha256 = ProjectManager::computeSha256(f.readAll());
        }
    }
    m_projectMgr->clearFileSummary(filePath, sha256);
    statusBar()->showMessage("File summary cleared (marked as created & empty).", 3000);
}

void MainWindow::onRecursiveProjectSummary(const QString& startDir) {
    if (!m_projectMgr->hasOpenProject()) {
        QMessageBox::information(this, "Recursive Summary", "Please open a project folder first.");
        return;
    }

    QString targetDir = startDir.isEmpty() ? m_projectMgr->projectRootPath() : startDir;
    RecursiveSummaryDialog dlg(m_projectMgr, targetDir, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QStringList files = dlg.selectedFilePaths();
    int scheduled = 0;
    for (const QString& path : files) {
        rebuildSingleFileSummary(path);
        ++scheduled;
    }

    if (scheduled > 0) {
        statusBar()->showMessage(QString("Queued %1 file(s) for background AI summary generation.").arg(scheduled), 5000);
    }
}

void MainWindow::onProjectTreeContextMenu(const QPoint& pos) {
    if (!m_projectMgr->hasOpenProject()) return;

    QModelIndex idx = m_projectTree->indexAt(pos);
    QString clickedPath = idx.isValid() ? m_fsModel->filePath(idx) : m_projectMgr->projectRootPath();
    bool isDir = !idx.isValid() || m_fsModel->isDir(idx);
    QString folderPath = isDir ? clickedPath : QFileInfo(clickedPath).absolutePath();

    QMenu menu(this);
    if (!isDir) {
        menu.addAction("Open File", this, [this, clickedPath]() {
            m_tabWidget->openFile(clickedPath);
            updateCentralViewState();
        });
        menu.addAction("Generate / Regenerate File Summary", this, [this, clickedPath]() {
            rebuildSingleFileSummary(clickedPath);
        });
        menu.addAction("Clear File Summary (Mark Empty)", this, [this, clickedPath]() {
            clearSingleFileSummary(clickedPath);
        });
        menu.addSeparator();
    }

    if (isDir && clickedPath != m_projectMgr->projectRootPath()) {
        menu.addAction(QString("Generate Summaries in '%1' (Recursive)...").arg(QFileInfo(clickedPath).fileName()),
                       this, [this, clickedPath]() { onRecursiveProjectSummary(clickedPath); });
    }
    menu.addAction("Generate Summaries for Entire Project (Recursive)...", this, [this]() {
        onRecursiveProjectSummary(m_projectMgr->projectRootPath());
    });

    menu.addSeparator();
    menu.addAction("Open Folder in Explorer / File Manager", this, [clickedPath]() {
        EditorTabWidget::openContainingFolder(clickedPath);
    });
    menu.addAction("Open Console Terminal Here", this, [folderPath]() {
        EditorTabWidget::openTerminalAtFolder(folderPath);
    });

    menu.exec(m_projectTree->viewport()->mapToGlobal(pos));
}

void MainWindow::updateCentralViewState() {
    bool hasTabs = (m_tabWidget->count() > 0);
    bool hasProj = m_projectMgr->hasOpenProject();
    m_projectDockStack->setCurrentIndex(hasProj ? 1 : 0);
    m_centralStack->setCurrentWidget(hasTabs ? static_cast<QWidget*>(m_tabWidget) : m_welcomeWidget);
}

void MainWindow::openProjectAtPath(const QString& dirPath) {
    if (dirPath.isEmpty() || !QDir(dirPath).exists()) return;
    if (m_projectMgr->openProjectFolder(dirPath)) {
        QString nativePath = QDir::toNativeSeparators(m_projectMgr->projectRootPath());
        m_projectPathLabel->setText(nativePath);
        m_projectPathLabel->setToolTip(nativePath);

        m_fsModel->setRootPath(dirPath);
        m_projectTree->setRootIndex(m_fsModel->index(dirPath));
        m_inspectorDock->refreshDisplay();
        ConfigManager::instance().addRecentProject(dirPath);
        updateCentralViewState();
        statusBar()->showMessage("Opened project: " + dirPath, 4000);
    }
}

void MainWindow::onOpenFile() {
    QString preferred;
    if (auto* ed = m_tabWidget->currentEditor(); ed && !ed->filePath().isEmpty()) {
        preferred = QFileInfo(ed->filePath()).absolutePath();
    } else if (m_projectMgr->hasOpenProject()) {
        preferred = m_projectMgr->projectRootPath();
    }
    QString startDir = ConfigManager::instance().resolveInitialDialogDir(preferred);
    QString path = QFileDialog::getOpenFileName(this, "Open File", startDir);
    if (!path.isEmpty()) {
        ConfigManager::instance().setLastDialogDir(path);
        m_tabWidget->openFile(path);
        updateCentralViewState();
    }
}

void MainWindow::onOpenFolder() {
    QString preferred = m_projectMgr->hasOpenProject() ? m_projectMgr->projectRootPath() : QString();
    QString startDir = ConfigManager::instance().resolveInitialDialogDir(preferred);
    QString dir = QFileDialog::getExistingDirectory(this, "Open Project Folder", startDir);
    if (!dir.isEmpty()) {
        ConfigManager::instance().setLastDialogDir(dir);
        openProjectAtPath(dir);
    }
}

void MainWindow::onCloseAllProjects() {
    m_taskQueue->cancelAllBackgroundTasks();
    if (!m_tabWidget->closeAllDocuments()) return;
    m_projectMgr->closeProject();
    m_projectPathLabel->setText("-");
    m_projectPathLabel->setToolTip({});
    ConfigManager::instance().setLastProjectPath({});
    m_inspectorDock->setActiveFile({});
    updateCentralViewState();
    statusBar()->showMessage("All projects closed.", 3000);
}

void MainWindow::onCompareUnsavedDiff(CodeEditor* targetEditor) {
    CodeEditor* ed = targetEditor ? targetEditor : m_tabWidget->currentEditor();
    if (!ed) {
        QMessageBox::information(this, "Diff Viewer", "No active document to compare.");
        return;
    }
    DiffViewerDialog dlg(ed->displayName(), ed->savedDiskContent(), ed->toPlainText(), ed->syntaxLanguage(), this);
    dlg.exec();
}

void MainWindow::onEditorPreferences() {
    EditorPreferencesDialog dlg(m_downloader, this);
    if (dlg.exec() == QDialog::Accepted) {
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            if (auto* ed = m_tabWidget->editorAt(i)) {
                if (!ed->filePath().isEmpty())
                    ed->setSyntaxLanguage(ConfigManager::instance().languageIdForFile(ed->filePath()));
            }
        }
    }
}

void MainWindow::populateRecentProjectsMenu() {
    m_recentProjectsMenu->clear();
    const QStringList recents = ConfigManager::instance().recentProjects();
    if (recents.isEmpty()) {
        QAction* emptyAct = m_recentProjectsMenu->addAction("(No recent projects)");
        emptyAct->setEnabled(false);
        return;
    }
    for (const QString& path : recents) {
        m_recentProjectsMenu->addAction(path, this, [this, path]() { openProjectAtPath(path); });
    }
    m_recentProjectsMenu->addSeparator();
    m_recentProjectsMenu->addAction("Clear Recent Projects", this, []() {
        ConfigManager::instance().clearRecentProjects();
    });
}

void MainWindow::populateSyntaxMenu() {
    m_langMenu->clear();
    const auto rules = ConfigManager::instance().fileTypeRules();
    for (const auto& r : rules) {
        QAction* act = m_langMenu->addAction(QString("%1 (%2)").arg(r.displayName, r.langId));
        if (auto* e = m_tabWidget->currentEditor()) {
            act->setCheckable(true);
            act->setChecked(e->syntaxLanguage() == r.langId);
        }
        connect(act, &QAction::triggered, this, [this, lang = r.langId]() {
            if (auto* e = m_tabWidget->currentEditor()) {
                e->setSyntaxLanguage(lang);
                m_langStatusLabel->setText("Lang: " + lang);
            }
        });
    }
}

void MainWindow::onActiveEditorChanged(CodeEditor* editor) {
    if (!editor) {
        m_inspectorDock->setActiveFile({});
        m_langStatusLabel->setText("Lang: -");
        return;
    }
    m_inspectorDock->setActiveFile(editor->filePath());
    m_langStatusLabel->setText("Lang: " + editor->syntaxLanguage());
}

QString MainWindow::actionKeyForEnum(AiEditAction action) {
    switch (action) {
    case AiEditAction::ExplainCode:         return "ExplainCode";
    case AiEditAction::RefactorCode:        return "RefactorCode";
    case AiEditAction::FindBugs:            return "FindBugs";
    case AiEditAction::OptimizePerformance: return "OptimizePerformance";
    case AiEditAction::AddDocumentation:    return "AddDocumentation";
    case AiEditAction::GenerateUnitTests:   return "GenerateUnitTests";
    case AiEditAction::CompleteCode:        return "CompleteCode";
    case AiEditAction::CustomPrompt:        return "CustomPrompt";
    }
    return "RefactorCode";
}

void MainWindow::onEditorAiAction(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction) {
    if (!editor) return;
    QString sysCtx = m_projectMgr->buildEffectiveSystemContext(editor->filePath());
    QString customSuffix = ConfigManager::instance().promptTemplate("CustomPrompt");
    QString instruction = (action == AiEditAction::CustomPrompt)
                              ? (customSuffix.isEmpty() ? customInstruction : (customInstruction + "\n" + customSuffix))
                              : ConfigManager::instance().promptTemplate(actionKeyForEnum(action));

    const auto& cfg = ConfigManager::instance().aiConfig();
    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = (action == AiEditAction::ExplainCode) ? TaskTargetType::ExplainToDialog : TaskTargetType::ModifyEditorCode;
    task.targetEditor = editor;
    task.filePath = editor->filePath();
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = cfg.maxOutputCodeTokens;
    task.systemPrompt = sysCtx;
    task.userPrompt = instruction + "\n\n" + selectedText;
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void MainWindow::onTaskCompleted(const AiTask& task, const QString& resultText) {
    if (task.targetType != TaskTargetType::ExplainToDialog) return;
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("AI Code Explanation");
    dlg->resize(740, 560);
    auto* lay = new QVBoxLayout(dlg);
    auto* view = new QTextEdit(dlg);
    view->setReadOnly(true);
    MarkdownRenderer::configureViewer(view);
    view->setHtml(MarkdownRenderer::toHtml(resultText));
    lay->addWidget(view);
    dlg->show();
}

void MainWindow::onTaskFailed(const AiTask& task, const QString& errorText) {
    statusBar()->showMessage("AI Request Error: " + errorText, 6000);
    if (task.priority == TaskPriority::High_UserAction)
        QMessageBox::warning(this, "AI Request Failed", "Error communicating with LLM endpoint:\n" + errorText);
}

void MainWindow::onAiPreferences() {
    AiPreferencesDialog dlg(m_aiProvider, this);
    if (dlg.exec() == QDialog::Accepted) {
        auto& cfgMgr = ConfigManager::instance();
        cfgMgr.setAiProfiles(dlg.resultProfiles(), dlg.resultActiveProfileName());
        cfgMgr.setAiConfig(dlg.resultConfig());
        cfgMgr.setAiBehavior(dlg.resultBehavior());
        cfgMgr.setPromptTemplates(dlg.resultPrompts());
        m_projectMgr->saveProjectFile();
        m_inspectorDock->refreshDisplay();
        m_projectTree->viewport()->update();
    }
}

void MainWindow::onFindReplaceDialog() {
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Find & Replace");
    auto* lay = new QVBoxLayout(dlg);
    auto* findEdit = new QLineEdit(dlg); findEdit->setPlaceholderText("Find text...");
    auto* replEdit = new QLineEdit(dlg); replEdit->setPlaceholderText("Replace with...");
    auto* btnRow = new QHBoxLayout();
    auto* findNextBtn = new QPushButton("Find Next", dlg);
    auto* replBtn = new QPushButton("Replace", dlg);
    auto* replAllBtn = new QPushButton("Replace All", dlg);
    btnRow->addWidget(findNextBtn); btnRow->addWidget(replBtn); btnRow->addWidget(replAllBtn);
    lay->addWidget(findEdit); lay->addWidget(replEdit); lay->addLayout(btnRow);

    connect(findNextBtn, &QPushButton::clicked, dlg, [this, findEdit]() {
        if (auto* e = m_tabWidget->currentEditor()) e->find(findEdit->text());
    });
    connect(replBtn, &QPushButton::clicked, dlg, [this, findEdit, replEdit]() {
        if (auto* e = m_tabWidget->currentEditor(); e && !e->isReadOnly()) {
            if (e->textCursor().selectedText() == findEdit->text()) e->textCursor().insertText(replEdit->text());
            e->find(findEdit->text());
        }
    });
    connect(replAllBtn, &QPushButton::clicked, dlg, [this, findEdit, replEdit]() {
        auto* e = m_tabWidget->currentEditor();
        if (!e || e->isReadOnly() || findEdit->text().isEmpty()) return;
        e->moveCursor(QTextCursor::Start);
        e->textCursor().beginEditBlock();
        while (e->find(findEdit->text())) e->textCursor().insertText(replEdit->text());
        e->textCursor().endEditBlock();
    });
    dlg->show();
}

void MainWindow::populateThemeMenu() {
    m_themeMenu->clear();
    QDir dir(ConfigManager::instance().themesDirPath());
    QStringList themes = dir.entryList({"*.json"}, QDir::Files);
    if (themes.isEmpty()) themes = {"dracula.json", "monokai.json", "github-light.json"};

    for (const QString& file : themes) {
        QString name = QFileInfo(file).baseName();
        QAction* act = m_themeMenu->addAction(name);
        act->setCheckable(true);
        act->setChecked(ConfigManager::instance().activeTheme() == name);
        connect(act, &QAction::triggered, this, [name]() { ConfigManager::instance().setActiveTheme(name); });
    }
}

void MainWindow::populateWindowMenu() {
    m_windowMenu->clear();
    m_windowMenu->addAction("Next Document", QKeySequence("Ctrl+Tab"), this, [this]() {
        if (m_tabWidget->count() > 1) m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() + 1) % m_tabWidget->count());
    });
    m_windowMenu->addAction("Previous Document", QKeySequence("Ctrl+Shift+Tab"), this, [this]() {
        if (m_tabWidget->count() > 1) m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() - 1 + m_tabWidget->count()) % m_tabWidget->count());
    });
    m_windowMenu->addAction("Close All Documents", this, [this]() {
        m_tabWidget->closeAllDocuments();
        updateCentralViewState();
    });
    m_windowMenu->addSeparator();

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* ed = m_tabWidget->editorAt(i);
        if (!ed) continue;
        QAction* act = m_windowMenu->addAction(QString("&%1 %2").arg(i + 1).arg(m_tabWidget->tabText(i)));
        act->setCheckable(true);
        act->setChecked(i == m_tabWidget->currentIndex());
        connect(act, &QAction::triggered, this, [this, i]() { m_tabWidget->setCurrentIndex(i); });
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        QString localPath = url.toLocalFile();
        if (localPath.isEmpty()) continue;
        QFileInfo fi(localPath);
        if (fi.isDir()) {
            openProjectAtPath(localPath);
        } else if (fi.suffix().compare("aide", Qt::CaseInsensitive) == 0) {
            openProjectAtPath(fi.absolutePath());
        } else if (fi.isFile()) {
            m_tabWidget->openFile(localPath);
            updateCentralViewState();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    ConfigManager::instance().setWindowGeometryAndState(saveGeometry(), saveState());
    m_projectMgr->saveProjectFile();
    if (m_tabWidget->closeAllDocuments()) event->accept();
    else event->ignore();
}
