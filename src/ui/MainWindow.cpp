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
#include "ProjectPromptResultDialog.h"
#include "../core/ConfigManager.h"
#include "../core/ProjectManager.h"
#include "../editor/EditorTabWidget.h"
#include "../ai/AiProvider.h"
#include "../ai/AiTaskQueue.h"
#include "../ai/SummaryWorker.h"
#include "../syntax/ThemeAndSyntaxDownloader.h"

#include <QComboBox>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
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
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>

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

    // Док 1: Project Explorer
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
    pathHeaderLay->setSpacing(3);
    auto* pathTitleLbl = new QLabel("PROJECT PATH:", pathHeaderBox);
    pathTitleLbl->setStyleSheet("color: #888899; font-size: 9px; font-weight: bold; border: none; background: transparent;");
    m_projectPathLabel = new QLabel("-", pathHeaderBox);
    m_projectPathLabel->setStyleSheet("color: #bd93f9; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    m_projectPathLabel->setWordWrap(true);
    m_projectPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(0, 2, 0, 0);
    auto* filterLbl = new QLabel("Files:", pathHeaderBox);
    filterLbl->setStyleSheet("color: #888899; font-size: 10px; font-weight: bold; border: none; background: transparent;");
    m_projectFilterCombo = new QComboBox(pathHeaderBox);
    m_projectFilterCombo->addItem("All Files (*)", false);
    m_projectFilterCombo->addItem("Supported Extensions Only", true);
    m_projectFilterCombo->setCurrentIndex(ConfigManager::instance().projectExplorerFilterSupported() ? 1 : 0);
    filterRow->addWidget(filterLbl);
    filterRow->addWidget(m_projectFilterCombo, 1);

    pathHeaderLay->addWidget(pathTitleLbl);
    pathHeaderLay->addWidget(m_projectPathLabel);
    pathHeaderLay->addLayout(filterRow);

    m_projectTree = new QTreeView(projExplorerPage);
    m_fsModel = new ProjectTreeModel(m_projectMgr, this);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fsModel->setNameFilterDisables(false);
    m_projectTree->setModel(m_fsModel);
    for (int col = 1; col < 4; ++col) m_projectTree->hideColumn(col);
    m_projectTree->header()->hide();
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);

    applyProjectExplorerFilter();

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
    viewMenu->addAction("Download / Update Syntax && Themes", this, [this]() {
        statusBar()->showMessage("Downloading syntax & theme files...", 3000);
        m_downloader->forceDownloadAll();
    });
    viewMenu->addSeparator();
    viewMenu->addAction("Zoom In", QKeySequence::ZoomIn, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->zoomIn(1); });
    viewMenu->addAction("Zoom Out", QKeySequence::ZoomOut, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->zoomOut(1); });

    // 4. AI
    QMenu* aiMenu = menuBar()->addMenu("&AI");

    // 4.1. Подменю Current File
    QMenu* currentFileMenu = aiMenu->addMenu("Current &File");
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
    for (const auto& [label, act] : aiActs) {
        if (act == AiEditAction::CustomPrompt) {
            currentFileMenu->addSeparator();
        }
        currentFileMenu->addAction(label, this, [this, act]() {
            if (auto* e = m_tabWidget->currentEditor()) e->triggerAiAction(act);
        });
    }
    currentFileMenu->addSeparator();
    currentFileMenu->addAction("Rebuild Summary", this, [this]() {
        if (auto* e = m_tabWidget->currentEditor(); e && !e->filePath().isEmpty())
            rebuildSingleFileSummary(e->filePath(), true);
    });

    // 4.2. Подменю Project
    QMenu* projectAiMenu = aiMenu->addMenu("&Project");
    projectAiMenu->addAction("Custom Prompt...", QKeySequence("Ctrl+Shift+P"), this, [this]() {
        onProjectCustomPrompt();
    });
    projectAiMenu->addSeparator();
    projectAiMenu->addAction("Rebuild Summary", this, &MainWindow::onRebuildProjectSummary);
    projectAiMenu->addAction("Generate Summaries (Recursive)...", this, [this]() {
        onRecursiveProjectSummary();
    });

    aiMenu->addSeparator();
    aiMenu->addAction("Cancel All Background Tasks", m_taskQueue, &AiTaskQueue::cancelAllBackgroundTasks);
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

    connect(m_inspectorDock, &ContextInspectorDock::rebuildFileSummaryClicked, this, [this](const QString& path) {
        rebuildSingleFileSummary(path, true);
    });
    connect(m_inspectorDock, &ContextInspectorDock::clearFileSummaryClicked, this, &MainWindow::clearSingleFileSummary);
    connect(m_inspectorDock, &ContextInspectorDock::rebuildProjectSummaryClicked, this, &MainWindow::onRebuildProjectSummary);

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
    connect(m_taskQueue, &AiTaskQueue::taskStreamChunk, this, &MainWindow::onTaskStreamChunk);
    connect(m_taskQueue, &AiTaskQueue::taskCancelled, this, &MainWindow::onTaskCancelled);

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
    connect(m_promptHistoryDock, &PromptHistoryDock::useProjectPromptRequested, this, [this](const QString& promptText) {
        onProjectCustomPrompt(promptText);
    });

    connect(m_projectFilterCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        bool filterOnly = m_projectFilterCombo->itemData(idx).toBool();
        ConfigManager::instance().setProjectExplorerFilterSupported(filterOnly);
    });
    connect(&ConfigManager::instance(), &ConfigManager::projectExplorerFilterChanged, this, [this](bool filterOnly) {
        int targetIdx = filterOnly ? 1 : 0;
        if (m_projectFilterCombo->currentIndex() != targetIdx) {
            m_projectFilterCombo->setCurrentIndex(targetIdx);
        }
        applyProjectExplorerFilter();
    });
    connect(&ConfigManager::instance(), &ConfigManager::fileTypesChanged, this, &MainWindow::applyProjectExplorerFilter);
    connect(&ConfigManager::instance(), &ConfigManager::aiBehaviorChanged, this, [this](const AiBehaviorConfig&) {
        applyProjectExplorerFilter();
    });
}

void MainWindow::rebuildSingleFileSummary(const QString& filePath,
                                          bool checkLimits,
                                          int maxTokensOverride,
                                          int ctxWindowOverride) {
    if (filePath.isEmpty()) return;

    QByteArray content;
    if (auto* ed = m_tabWidget->findEditorByPath(filePath)) {
        content = ed->toPlainText().toUtf8();
    } else {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) return;
        content = f.readAll();
    }
    if (content.trimmed().isEmpty()) return;

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = maxTokensOverride > 0 ? maxTokensOverride : cfg.maxOutputSummaryTokens;
    int ctxWindow = ctxWindowOverride > 0 ? ctxWindowOverride : cfg.contextWindow;

    if (checkLimits) {
        QString relPath = m_projectMgr->toRelativePath(filePath);
        QString sysPrompt = m_projectMgr->projectSystemPrompt();
        QString userPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                             + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(content));
        if (!verifyAndAdjustContextLimits("Rebuild File Summary (" + relPath + ")",
                                          sysPrompt,
                                          userPrompt,
                                          qMax(512, maxTokens),
                                          true,
                                          maxTokens,
                                          ctxWindow)) {
            return;
        }
    }

    m_summaryWorker->forceRebuildFileSummary(filePath, content, maxTokens, ctxWindow);
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

QMap<QString, QString> MainWindow::collectOpenEditorBuffers() const {
    QMap<QString, QString> buffers;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto* ed = m_tabWidget->editorAt(i); ed && !ed->filePath().isEmpty()) {
            buffers.insert(QFileInfo(ed->filePath()).absoluteFilePath(), ed->toPlainText());
        }
    }
    return buffers;
}

int MainWindow::roundUpTokenStep(int needed, int maxCap) {
    static constexpr int kSteps[] = {
        1024, 2048, 4096, 8192, 12288, 16384, 24576, 32768,
        49152, 65536, 98304, 131072, 196608, 262144, 524288, 1048576
    };
    for (int step : kSteps) {
        if (step >= needed && step <= maxCap) return step;
    }
    int aligned = ((needed + 4095) / 4096) * 4096;
    return qBound(1024, aligned, maxCap);
}

bool MainWindow::verifyAndAdjustContextLimits(const QString& taskTitle,
                                              const QString& systemPrompt,
                                              const QString& userPrompt,
                                              int minRecommendedOutputTokens,
                                              bool isSummaryTask,
                                              int& inOutMaxOutputTokens,
                                              int& inOutContextWindow) {
    const int promptChars = systemPrompt.length() + userPrompt.length();
    const int promptTokens = (promptChars + 3) / 4;
    const int framingOverhead = 128;
    const int effectivePromptTokens = promptTokens + framingOverhead;

    const int maxOutCap = isSummaryTask ? 65536 : 262144;
    const int minOutNeeded = qBound(64, minRecommendedOutputTokens, maxOutCap);

    const bool outputLimitTooSmall = (inOutMaxOutputTokens < minOutNeeded);

    // Оцениваем необходимый размер окна контекста: промпт + ожидаемый объём ответа
    const int activeOutputReserve = qMax(minOutNeeded, qMin(inOutMaxOutputTokens, 4096));
    const int requiredContextTokens = effectivePromptTokens + activeOutputReserve;
    const bool contextTooSmall = (requiredContextTokens > inOutContextWindow);

    if (!contextTooSmall && !outputLimitTooSmall) {
        return true;
    }

    const int suggestedMaxOut = outputLimitTooSmall
                                    ? roundUpTokenStep(minOutNeeded, maxOutCap)
                                    : inOutMaxOutputTokens;
    const int neededCtxWithSuggestedOut = effectivePromptTokens + qMax(minOutNeeded, qMin(suggestedMaxOut, 8192));
    const int suggestedCtxWindow = contextTooSmall
                                       ? qMax(inOutContextWindow, roundUpTokenStep(neededCtxWithSuggestedOut, 1048576))
                                       : inOutContextWindow;

    QDialog dlg(this);
    dlg.setWindowTitle("Context Window & Token Limit Check — " + taskTitle);
    dlg.setMinimumWidth(540);

    auto* mainLay = new QVBoxLayout(&dlg);

    QString problemDesc;
    if (contextTooSmall && outputLimitTooSmall) {
        problemDesc = "The estimated prompt and output size exceed both the configured <b>Context Window</b> "
                      "and <b>Max Output Tokens</b>.";
    } else if (contextTooSmall) {
        problemDesc = "The estimated prompt and response reserve exceed the configured <b>Context Window</b> "
                      "(the LLM may truncate the prompt or fail to generate a full response).";
    } else {
        problemDesc = "The configured <b>Max Output Tokens</b> limit is smaller than the recommended output reserve "
                      "for this task (the response may be cut off mid-generation).";
    }

    auto* infoLbl = new QLabel(
        QString("<div style='line-height:1.45;'>"
                "%1<br><br>"
                "• <b>Estimated Input Prompt:</b> ~%2 tokens (%3 chars)<br>"
                "• <b>Recommended Output Reserve:</b> ~%4 tokens<br>"
                "• <b>Estimated Required Context:</b> ~%5 tokens<br>"
                "• <b>Current Context Window:</b> <span style='color:%6; font-weight:bold;'>%7 tokens</span><br>"
                "• <b>Current Max Output Tokens:</b> <span style='color:%8; font-weight:bold;'>%9 tokens</span>"
                "</div>")
            .arg(problemDesc)
            .arg(effectivePromptTokens)
            .arg(promptChars)
            .arg(minOutNeeded)
            .arg(neededCtxWithSuggestedOut)
            .arg(contextTooSmall ? "#ff5555" : "#50fa7b")
            .arg(inOutContextWindow)
            .arg(outputLimitTooSmall ? "#ff5555" : "#50fa7b")
            .arg(inOutMaxOutputTokens),
        &dlg);
    infoLbl->setWordWrap(true);
    mainLay->addWidget(infoLbl);

    auto* adjGroup = new QGroupBox("Adjust Context Window / Output Token Limits", &dlg);
    auto* form = new QFormLayout(adjGroup);

    auto* ctxSpin = new QSpinBox(adjGroup);
    ctxSpin->setRange(1024, 1048576);
    ctxSpin->setSingleStep(4096);
    ctxSpin->setValue(suggestedCtxWindow);

    auto* outSpin = new QSpinBox(adjGroup);
    outSpin->setRange(64, maxOutCap);
    outSpin->setSingleStep(isSummaryTask ? 256 : 1024);
    outSpin->setValue(suggestedMaxOut);

    form->addRow("New Context Window (tokens):", ctxSpin);
    form->addRow(isSummaryTask ? "New Max Summary Tokens:" : "New Max Output Code Tokens:", outSpin);

    const auto activeProfName = ConfigManager::instance().activeAiProfileName();
    auto* saveProfileCheck = new QCheckBox(
        QString("Save updated limits to active AI profile (%1)").arg(activeProfName),
        adjGroup);
    saveProfileCheck->setChecked(true);
    form->addRow("", saveProfileCheck);

    mainLay->addWidget(adjGroup);

    auto* btnRow = new QHBoxLayout();
    auto* increaseBtn = new QPushButton("Increase && Run", &dlg);
    increaseBtn->setDefault(true);
    increaseBtn->setStyleSheet(
        "QPushButton { background-color: #50fa7b; color: #191a24; font-weight: bold; border-radius: 4px; padding: 6px 12px; }"
        "QPushButton:hover { background-color: #69ff94; }");

    auto* continueAsIsBtn = new QPushButton("Continue Without Changes", &dlg);
    continueAsIsBtn->setToolTip("Run the prompt immediately using the current context window and token limits");

    auto* cancelBtn = new QPushButton("Cancel", &dlg);

    btnRow->addWidget(increaseBtn);
    btnRow->addWidget(continueAsIsBtn);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    mainLay->addLayout(btnRow);

    int userChoice = 0; // 0 = Cancel, 1 = Increase & Run, 2 = Continue Without Changes
    connect(increaseBtn, &QPushButton::clicked, &dlg, [&]() {
        userChoice = 1;
        dlg.accept();
    });
    connect(continueAsIsBtn, &QPushButton::clicked, &dlg, [&]() {
        userChoice = 2;
        dlg.accept();
    });
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted || userChoice == 0) {
        return false;
    }

    if (userChoice == 1) {
        inOutContextWindow = ctxSpin->value();
        inOutMaxOutputTokens = outSpin->value();

        if (saveProfileCheck->isChecked()) {
            AiConnectionConfig updatedCfg = ConfigManager::instance().aiConfig();
            updatedCfg.contextWindow = inOutContextWindow;
            if (isSummaryTask) {
                updatedCfg.maxOutputSummaryTokens = inOutMaxOutputTokens;
            } else {
                updatedCfg.maxOutputCodeTokens = inOutMaxOutputTokens;
            }
            ConfigManager::instance().setAiConfig(updatedCfg);
        }
    }

    return true;
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
    if (files.isEmpty()) return;

    // Находим самый крупный выбранный файл для предварительной оценки контекста перед постановкой пакета в очередь
    QString largestPath;
    QByteArray largestContent;
    const auto openBuffers = collectOpenEditorBuffers();
    for (const QString& path : files) {
        QByteArray raw;
        QString abs = QFileInfo(path).absoluteFilePath();
        if (openBuffers.contains(abs)) {
            raw = openBuffers.value(abs).toUtf8();
        } else {
            QFile f(abs);
            if (f.open(QIODevice::ReadOnly)) raw = f.readAll();
        }
        if (raw.size() > largestContent.size()) {
            largestContent = raw;
            largestPath = abs;
        }
    }

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = cfg.maxOutputSummaryTokens;
    int ctxWindow = cfg.contextWindow;

    if (!largestPath.isEmpty()) {
        QString relPath = m_projectMgr->toRelativePath(largestPath);
        QString sysPrompt = m_projectMgr->projectSystemPrompt();
        QString userPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                             + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(largestContent));
        if (!verifyAndAdjustContextLimits(
                QString("Recursive Summary (Largest File: %1)").arg(relPath),
                sysPrompt,
                userPrompt,
                qMax(512, maxTokens),
                true,
                maxTokens,
                ctxWindow)) {
            return;
        }
    }

    int scheduled = 0;
    for (const QString& path : files) {
        rebuildSingleFileSummary(path, false, maxTokens, ctxWindow);
        ++scheduled;
    }
    if (scheduled > 0) {
        statusBar()->showMessage(QString("Queued %1 file(s) for background AI summary generation.").arg(scheduled), 5000);
    }
}

void MainWindow::onRebuildProjectSummary() {
    QString digest = m_projectMgr->buildAggregatedFilesDigest();
    if (digest.trimmed().isEmpty()) {
        QMessageBox::information(this, "Rebuild Project Summary",
                                 "No indexed file summaries available yet to synthesize a project summary.");
        return;
    }

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = cfg.maxOutputSummaryTokens;
    int ctxWindow = cfg.contextWindow;

    QString sysPrompt = m_projectMgr->projectSystemPrompt();
    QString userPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;

    if (!verifyAndAdjustContextLimits("Rebuild Project Summary",
                                      sysPrompt,
                                      userPrompt,
                                      qMax(1024, maxTokens),
                                      true,
                                      maxTokens,
                                      ctxWindow)) {
        return;
    }

    m_summaryWorker->forceRebuildProjectSummary(maxTokens, ctxWindow);
}

void MainWindow::onProjectCustomPrompt(const QString& initialPromptText) {
    if (!m_projectMgr->hasOpenProject()) {
        QMessageBox::information(this, "Custom Prompt for Project",
                                 "Please open a project folder first (File -> Open Folder / Project...).");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("AI Custom Prompt for Project");
    dlg.resize(840, 680);
    auto* lay = new QVBoxLayout(&dlg);

    bool hasSummary = !m_projectMgr->projectSummary().trimmed().isEmpty() ||
                      !m_projectMgr->buildAggregatedFilesDigest().trimmed().isEmpty();
    int indexedCount = m_projectMgr->indexedFilesCount();

    auto* statusLbl = new QLabel(
        QString("<b>Project:</b> %1<br>"
                "<span style='color:#888899;'>Project Summary Ready: <b>%2</b> | Indexed Project Files: <b>%3</b></span>")
            .arg(QDir::toNativeSeparators(m_projectMgr->projectRootPath()),
                 hasSummary ? "Yes" : "No (Empty)",
                 QString::number(indexedCount)),
        &dlg);
    statusLbl->setWordWrap(true);
    lay->addWidget(statusLbl);

    auto* ctxRow = new QHBoxLayout();
    ctxRow->addWidget(new QLabel("Project Context Option:", &dlg));
    auto* ctxCombo = new QComboBox(&dlg);
    ctxCombo->addItem("1. Project Summary Only",
                      static_cast<int>(ProjectCustomPromptContextMode::ProjectSummaryOnly));
    ctxCombo->addItem("2. Selected Project Files (Concatenated Internal Markdown)",
                      static_cast<int>(ProjectCustomPromptContextMode::IndexedSourcesMarkdownOnly));
    ctxCombo->addItem("3. Both Project Summary and Selected Files Markdown",
                      static_cast<int>(ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown));

    auto beh = ConfigManager::instance().aiBehavior();
    ctxCombo->setCurrentIndex(qBound(0, static_cast<int>(beh.projectCustomPromptContextMode), 2));
    ctxRow->addWidget(ctxCombo, 1);
    lay->addLayout(ctxRow);

    auto* split = new QSplitter(Qt::Vertical, &dlg);
    split->setChildrenCollapsible(false);

    // Верхняя часть сплиттера: дерево выбора файлов проекта (по умолчанию отмечены проиндексированные файлы)
    auto* filesBox = new QGroupBox("Project Files to Include in Prompt Context (Indexed files are pre-selected):", split);
    auto* filesLay = new QVBoxLayout(filesBox);
    auto* fileSelector = new ProjectFileSelectorWidget(
        m_projectMgr,
        m_projectMgr->projectRootPath(),
        FileSelectorPreset::IndexedFilesDefault,
        filesBox);
    auto* selectedCountLbl = new QLabel(filesBox);
    selectedCountLbl->setStyleSheet("font-weight: bold; color: #bd93f9;");
    filesLay->addWidget(fileSelector, 1);
    filesLay->addWidget(selectedCountLbl);

    // Нижняя часть сплиттера: ввод текста кастомного промпта
    auto* promptBox = new QWidget(split);
    auto* promptLay = new QVBoxLayout(promptBox);
    promptLay->setContentsMargins(0, 4, 0, 0);
    promptLay->addWidget(new QLabel(
        "Enter instructions for the LLM (response will be generated as a Markdown document in a separate window):",
        promptBox));

    auto* promptEdit = new QPlainTextEdit(promptBox);
    promptEdit->setPlaceholderText(
        "e.g. Analyze the project architecture, propose new classes/files, or generate implementation files...");
    QString seedText = initialPromptText.isEmpty() ? ConfigManager::instance().lastCustomPrompt() : initialPromptText;
    promptEdit->setPlainText(seedText);
    promptLay->addWidget(promptEdit, 1);

    split->addWidget(filesBox);
    split->addWidget(promptBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({340, 220});
    lay->addWidget(split, 1);

    // Панель предварительной оценки размера контекста в реальном времени
    auto* estimateLbl = new QLabel(&dlg);
    estimateLbl->setWordWrap(true);
    lay->addWidget(estimateLbl);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto* okBtn = btns->button(QDialogButtonBox::Ok)) {
        okBtn->setText("Run Project Prompt");
    }
    lay->addWidget(btns);

    const auto openBuffers = collectOpenEditorBuffers();
    const QString sysPrompt = m_projectMgr->projectSystemPrompt();
    const QString mdSuffix = ConfigManager::instance().promptTemplate("ProjectCustomPrompt");

    auto updateLiveContextEstimate = [&]() {
        auto mode = static_cast<ProjectCustomPromptContextMode>(ctxCombo->currentData().toInt());
        bool usesFiles = (mode != ProjectCustomPromptContextMode::ProjectSummaryOnly);
        filesBox->setEnabled(usesFiles);

        QStringList selFiles = fileSelector->selectedFilePaths();
        if (usesFiles) {
            selectedCountLbl->setText(QString("Selected for prompt context: %1 file(s)").arg(selFiles.size()));
        } else {
            selectedCountLbl->setText("File selection is inactive in '1. Project Summary Only' mode.");
        }

        QString projCtx = m_projectMgr->buildProjectCustomPromptContext(mode, selFiles, openBuffers);
        QString customInst = promptEdit->toPlainText().trimmed();
        QString fullUser = customInst;
        if (!mdSuffix.trimmed().isEmpty()) fullUser += "\n\n" + mdSuffix.trimmed();
        if (!projCtx.trimmed().isEmpty()) fullUser += "\n\n" + projCtx;

        int totalChars = sysPrompt.length() + fullUser.length();
        int promptTok = (totalChars + 3) / 4 + 128;
        int recOutTok = qMax(2048, qMin(16384, promptTok / 3));
        int reqCtxTok = promptTok + recOutTok;

        const auto& aiCfg = ConfigManager::instance().aiConfig();
        bool exceedsCtx = (reqCtxTok > aiCfg.contextWindow);
        bool exceedsOut = (recOutTok > aiCfg.maxOutputCodeTokens);

        if (exceedsCtx || exceedsOut) {
            estimateLbl->setStyleSheet("color: #ffb86c; font-weight: bold;");
            estimateLbl->setText(
                QString("Estimated Context: ~%1 tok (Prompt: ~%2 tok + Output reserve: ~%3 tok) | "
                        "Configured Context Window: %4 tok, Max Output: %5 tok — Adjust limits on run if needed.")
                    .arg(reqCtxTok)
                    .arg(promptTok)
                    .arg(recOutTok)
                    .arg(aiCfg.contextWindow)
                    .arg(aiCfg.maxOutputCodeTokens));
        } else {
            estimateLbl->setStyleSheet("color: #50fa7b;");
            estimateLbl->setText(
                QString("Estimated Context: ~%1 tok (Prompt: ~%2 tok + Output reserve: ~%3 tok) | "
                        "Fits within Context Window (%4 tok)")
                    .arg(reqCtxTok)
                    .arg(promptTok)
                    .arg(recOutTok)
                    .arg(aiCfg.contextWindow));
        }
    };

    connect(ctxCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) { updateLiveContextEstimate(); });
    connect(fileSelector, &ProjectFileSelectorWidget::selectionChanged, &dlg, [&](int) { updateLiveContextEstimate(); });
    connect(promptEdit, &QPlainTextEdit::textChanged, &dlg, updateLiveContextEstimate);

    updateLiveContextEstimate();

    QString finalCustomInst;
    QString finalFullUserPrompt;
    int finalMaxOutputTokens = ConfigManager::instance().aiConfig().maxOutputCodeTokens;
    int finalContextWindow = ConfigManager::instance().aiConfig().contextWindow;

    connect(btns, &QDialogButtonBox::accepted, &dlg, [&]() {
        QString customInst = promptEdit->toPlainText().trimmed();
        if (customInst.isEmpty()) {
            QMessageBox::warning(&dlg, "Empty Prompt", "Please enter instructions for the LLM.");
            return;
        }

        auto selectedMode = static_cast<ProjectCustomPromptContextMode>(ctxCombo->currentData().toInt());
        QStringList selectedFiles = fileSelector->selectedFilePaths();

        if (selectedMode != ProjectCustomPromptContextMode::ProjectSummaryOnly && selectedFiles.isEmpty()) {
            auto ans = QMessageBox::question(
                &dlg,
                "No Project Files Selected",
                "No files are checked in the project tree.\n"
                "Do you want to continue without source files in the context?",
                QMessageBox::Yes | QMessageBox::No);
            if (ans == QMessageBox::No) return;
        }

        int includedFiles = 0;
        QString projectContext = m_projectMgr->buildProjectCustomPromptContext(
            selectedMode, selectedFiles, openBuffers, &includedFiles);

        if (projectContext.trimmed().isEmpty()) {
            auto ans = QMessageBox::question(
                &dlg,
                "Empty Project Context",
                "The selected project context is currently empty (no selected files or project summary found).\n"
                "Do you want to send the prompt anyway?",
                QMessageBox::Yes | QMessageBox::No);
            if (ans == QMessageBox::No) return;
        }

        QString fullUserPrompt = customInst;
        if (!mdSuffix.trimmed().isEmpty()) {
            fullUserPrompt += "\n\n" + mdSuffix.trimmed();
        }
        if (!projectContext.trimmed().isEmpty()) {
            fullUserPrompt += "\n\n" + projectContext;
        }

        const auto& cfg = ConfigManager::instance().aiConfig();
        int maxTokens = cfg.maxOutputCodeTokens;
        int ctxWindow = cfg.contextWindow;
        int promptTokens = (sysPrompt.length() + fullUserPrompt.length() + 3) / 4;
        int minRecOutput = qMax(2048, qMin(16384, promptTokens / 3));

        if (!verifyAndAdjustContextLimits("Custom Prompt for Project",
                                          sysPrompt,
                                          fullUserPrompt,
                                          minRecOutput,
                                          false,
                                          maxTokens,
                                          ctxWindow)) {
            updateLiveContextEstimate();
            return;
        }

        if (beh.projectCustomPromptContextMode != selectedMode) {
            beh.projectCustomPromptContextMode = selectedMode;
            ConfigManager::instance().setAiBehavior(beh);
        }

        finalCustomInst = customInst;
        finalFullUserPrompt = fullUserPrompt;
        finalMaxOutputTokens = maxTokens;
        finalContextWindow = ctxWindow;
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    ConfigManager::instance().setLastCustomPrompt(finalCustomInst);

    const auto& cfg = ConfigManager::instance().aiConfig();

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = TaskTargetType::ProjectCustomPrompt;
    task.filePath = "[Project Custom Prompt]";
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = finalMaxOutputTokens;
    task.contextWindow = finalContextWindow;
    task.systemPrompt = sysPrompt;
    task.userPrompt = finalFullUserPrompt;
    task.stream = true;

    QString taskId = m_taskQueue->enqueueTask(task);

    auto* resWin = new ProjectPromptResultDialog(finalCustomInst, m_projectMgr->projectRootPath(), taskId, this);
    m_projectPromptWindows.insert(taskId, resWin);

    connect(resWin, &ProjectPromptResultDialog::cancelTaskRequested, m_taskQueue, &AiTaskQueue::cancelTaskById);
    connect(resWin, &ProjectPromptResultDialog::openContentInEditorRequested, this,
            [this](const QString& suggestedPath, const QString& code, const QString& langId) {
                CodeEditor* ed = m_tabWidget->newDocument();
                if (ed) {
                    ed->setPlainText(code);
                    if (!langId.isEmpty()) {
                        ed->setSyntaxLanguage(langId);
                    } else if (!suggestedPath.isEmpty()) {
                        ed->setSyntaxLanguage(ConfigManager::instance().languageIdForFile(suggestedPath));
                    }
                    updateCentralViewState();
                    raise();
                    activateWindow();
                    ed->setFocus();
                }
            });
    connect(resWin, &ProjectPromptResultDialog::replaceFileInEditorRequested, this,
            [this](const QString& existingFilePath, const QString& newCode, const QString& langId) {
                if (existingFilePath.isEmpty()) return;
                if (auto* ed = m_tabWidget->openFile(existingFilePath)) {
                    ed->replaceContentUnsaved(newCode);
                    if (!langId.isEmpty()) {
                        ed->setSyntaxLanguage(langId);
                    }
                    updateCentralViewState();
                    raise();
                    activateWindow();
                    ed->setFocus();
                    statusBar()->showMessage(
                        "Replaced content in editor (unsaved): " + QDir::toNativeSeparators(existingFilePath) +
                            " — press Ctrl+S to save or Ctrl+Z to undo.",
                        5000);
                }
            });
    connect(resWin, &ProjectPromptResultDialog::fileSavedToDisk, this, [this](const QString& savedPath) {
        if (auto* ed = m_tabWidget->findEditorByPath(savedPath)) {
            ed->loadFile(savedPath);
        }
        statusBar()->showMessage("Saved: " + QDir::toNativeSeparators(savedPath), 4000);
    });

    resWin->show();
    resWin->raise();
    resWin->activateWindow();
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
    menu.addAction("Custom Prompt for Project...", this, [this]() {
        onProjectCustomPrompt();
    });
    menu.addAction("Generate Summaries for Entire Project (Recursive)...", this, [this]() {
        onRecursiveProjectSummary(m_projectMgr->projectRootPath());
    });
    menu.addSeparator();
    QAction* filterAct = menu.addAction("Show Only Supported File Extensions");
    filterAct->setCheckable(true);
    filterAct->setChecked(ConfigManager::instance().projectExplorerFilterSupported());
    connect(filterAct, &QAction::toggled, this, [](bool checked) {
        ConfigManager::instance().setProjectExplorerFilterSupported(checked);
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

void MainWindow::applyProjectExplorerFilter() {
    if (!m_fsModel) return;
    if (ConfigManager::instance().projectExplorerFilterSupported()) {
        QStringList filters = ConfigManager::instance().supportedProjectFileNameFilters();
        m_fsModel->setNameFilterDisables(false);
        m_fsModel->setNameFilters(filters.isEmpty() ? QStringList{"*"} : filters);
    } else {
        m_fsModel->setNameFilters({});
    }
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

void MainWindow::onEditorAiAction(CodeEditor* editor,
                                  AiEditAction action,
                                  const QString& selectedText,
                                  const QString& customInstruction) {
    if (!editor) return;
    QString sysCtx = m_projectMgr->buildEffectiveSystemContext(editor->filePath());
    QString customSuffix = ConfigManager::instance().promptTemplate("CustomPrompt");
    QString instruction = (action == AiEditAction::CustomPrompt)
                              ? (customSuffix.isEmpty() ? customInstruction : (customInstruction + "\n" + customSuffix))
                              : ConfigManager::instance().promptTemplate(actionKeyForEnum(action));
    QString fullUserPrompt = instruction + "\n\n" + selectedText;

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = cfg.maxOutputCodeTokens;
    int ctxWindow = cfg.contextWindow;

    int targetCodeTokens = (selectedText.length() + 3) / 4;
    int minRecOutput = (action == AiEditAction::ExplainCode)
                           ? 2048
                           : qMax(1024, targetCodeTokens + 512);

    if (!verifyAndAdjustContextLimits("Editor AI Action (" + actionKeyForEnum(action) + ")",
                                      sysCtx,
                                      fullUserPrompt,
                                      minRecOutput,
                                      false,
                                      maxTokens,
                                      ctxWindow)) {
        return;
    }

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = (action == AiEditAction::ExplainCode)
                          ? TaskTargetType::ExplainToDialog
                          : TaskTargetType::ModifyEditorCode;
    task.targetEditor = editor;
    task.filePath = editor->filePath();
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = maxTokens;
    task.contextWindow = ctxWindow;
    task.systemPrompt = sysCtx;
    task.userPrompt = fullUserPrompt;
    task.stream = true;

    m_taskQueue->enqueueTask(task);
}

void MainWindow::onTaskStreamChunk(const AiTask& task, const QString& chunk) {
    if (task.targetType == TaskTargetType::ProjectCustomPrompt) {
        if (auto win = m_projectPromptWindows.value(task.taskId)) {
            win->appendStreamChunk(chunk);
        }
    }
}

void MainWindow::onTaskCompleted(const AiTask& task, const QString& resultText) {
    if (task.targetType == TaskTargetType::ProjectCustomPrompt) {
        if (auto win = m_projectPromptWindows.take(task.taskId)) {
            win->finalizeResponse(resultText);
        }
        return;
    }
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
    if (task.targetType == TaskTargetType::ProjectCustomPrompt) {
        if (auto win = m_projectPromptWindows.take(task.taskId)) {
            win->markFailedOrCancelled("AI Request Error: " + errorText);
        }
    }
    statusBar()->showMessage("AI Request Error: " + errorText, 6000);
    if (task.priority == TaskPriority::High_UserAction)
        QMessageBox::warning(this, "AI Request Failed", "Error communicating with LLM endpoint:\n" + errorText);
}

void MainWindow::onTaskCancelled(const AiTask& task) {
    if (task.targetType == TaskTargetType::ProjectCustomPrompt) {
        if (auto win = m_projectPromptWindows.take(task.taskId)) {
            win->markFailedOrCancelled("Generation cancelled by user.");
        }
    }
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
