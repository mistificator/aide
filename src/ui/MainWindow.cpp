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
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
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
#include <QToolBar>
#include <QTimer>
#include <QPainter>
#include <QRadialGradient>
#include <algorithm>

class ProjectTreeModel : public QFileSystemModel {
public:
    ProjectTreeModel(ProjectManager* pm, QObject* parent = nullptr) : QFileSystemModel(parent), m_pm(pm) {}

    void setHighlightQuery(const QString& query) {
        m_highlightQuery = query.trimmed();
    }

    QString highlightQuery() const {
        return m_highlightQuery;
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (index.column() == 0 && m_pm && m_pm->hasOpenProject() &&
            (role == Qt::DisplayRole || role == Qt::ForegroundRole ||
             role == Qt::BackgroundRole || role == Qt::FontRole || role == Qt::ToolTipRole)) {

            const bool hasSearchMatch = !m_highlightQuery.isEmpty() &&
                                        fileName(index).contains(m_highlightQuery, Qt::CaseInsensitive);

            if (role == Qt::BackgroundRole && hasSearchMatch) {
                return QColor(241, 250, 140, 65);
            }

            if (isDir(index)) {
                const QString path = filePath(index);
                const QString subRel = m_pm->toRelativePath(path);
                if (!subRel.isEmpty() && !subRel.startsWith("..")) {
                    const bool hasStandalone = m_pm->hasStandaloneAideFile(subRel);
                    const QString sum = m_pm->projectSummary(subRel);
                    const bool hasSum = !sum.trimmed().isEmpty();

                    if (role == Qt::DisplayRole) {
                        if (hasStandalone) {
                            return QFileSystemModel::data(index, Qt::DisplayRole).toString() + " [.aide]";
                        }
                    } else if (role == Qt::ForegroundRole) {
                        if (hasSearchMatch) return QColor("#f1fa8c");
                        if (hasStandalone) return QColor("#bd93f9");
                        if (hasSum) return QColor("#8be9fd");
                    } else if (role == Qt::FontRole) {
                        if (hasSearchMatch || hasStandalone || hasSum) {
                            QFont f = QFileSystemModel::data(index, Qt::FontRole).value<QFont>();
                            f.setBold(true);
                            return f;
                        }
                    } else if (role == Qt::ToolTipRole) {
                        const int indexed = m_pm->indexedFilesCount(subRel, true);
                        const bool dirty = m_pm->isProjectSummaryDirty(subRel);
                        const QString modeStr = hasStandalone
                                                    ? QStringLiteral("Standalone (.aide file in folder)")
                                                    : QStringLiteral("Virtual (stored in parent .aide)");
                        const QString status = !hasSum
                                                   ? QStringLiteral("No subproject summary yet")
                                                   : (dirty ? QStringLiteral("Summary Ready (Outdated / Dirty)")
                                                            : QStringLiteral("Summary Ready"));
                        QString tip = QString("Subproject: %1\nMode: %2\nStatus: %3\nIndexed files: %4")
                                          .arg(subRel, modeStr, status)
                                          .arg(indexed);
                        if (hasSum) {
                            tip += "\n\n" + sum.left(280);
                        }
                        return tip;
                    }
                }
            } else {
                if (role == Qt::FontRole && hasSearchMatch) {
                    QFont f = QFileSystemModel::data(index, Qt::FontRole).value<QFont>();
                    f.setBold(true);
                    return f;
                }
                if (role == Qt::ForegroundRole || role == Qt::ToolTipRole) {
                    const QString path = filePath(index);
                    auto fc = m_pm->fileContext(path);
                    bool hasCreatedSummary = fc.summaryCreated || !fc.aiSummary.isEmpty();
                    if (role == Qt::ForegroundRole) {
                        if (hasSearchMatch) return QColor("#f1fa8c");
                        if (fc.isSummaryPending) return QColor("#ffb86c");
                        if (hasCreatedSummary) return QColor("#50fa7b");
                    } else if (role == Qt::ToolTipRole) {
                        QString ownerNote = fc.subprojectRelPath.isEmpty()
                        ? QString()
                        : QString("\nSubproject: %1").arg(fc.subprojectRelPath);
                        if (fc.isSummaryPending) return "AI Summary: Indexing in background..." + ownerNote;
                        if (!fc.aiSummary.isEmpty())
                            return QString("AI Summary Ready (%1)%2:\n%3")
                                .arg(fc.llmModel, ownerNote, fc.aiSummary.left(300));
                        if (fc.summaryCreated)
                            return "AI Summary: Created (intentionally empty / cleared)" + ownerNote;
                        if (!ConfigManager::instance().shouldGenerateSummaryForFile(path))
                            return "AI Summary: Not required according to AI Behavior settings" + ownerNote;
                        return "AI Summary: Not indexed yet (open file or run recursive summary)" + ownerNote;
                    }
                }
            }
        }
        return QFileSystemModel::data(index, role);
    }
private:
    ProjectManager* m_pm;
    QString m_highlightQuery;
};

class QMenuCompat : public QMenu {
public:
    using QMenu::addAction;
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    template<typename Functor>
    QAction *addAction(const QString &text, const QKeySequence &shortcut, Functor functor)
    {
        return QMenu::addAction(text, functor, shortcut);
    }
    template<typename Class, typename Functor>
    QAction *addAction(const QString &text, const QKeySequence &shortcut, const Class *context, Functor functor)
    {
        return QMenu::addAction(text, context, functor, shortcut);
    }
    template<typename Functor>
    QAction *addAction(const QIcon &icon, const QString &text, const QKeySequence &shortcut, Functor functor)
    {
        return QMenu::addAction(icon, text, functor, shortcut);
    }
    template<typename Class, typename Functor>
    QAction *addAction(const QIcon &icon, const QString &text, const QKeySequence &shortcut, const Class *context, Functor functor)
    {
        return QMenu::addAction(icon, text, context, functor, shortcut);
    }
#endif
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

    const QByteArray savedGeom = ConfigManager::instance().windowGeometry();
    if (!savedGeom.isEmpty()) {
        restoreGeometry(savedGeom);
    }
    const QByteArray savedState = ConfigManager::instance().windowState();
    if (!savedState.isEmpty()) {
        restoreState(savedState);
    }

    int maxRightDockW = qMax(340, width() / 3);
    if (m_inspectorDock->width() > maxRightDockW || savedState.isEmpty()) {
        resizeDocks({m_inspectorDock}, {360}, Qt::Horizontal);
    }

    m_downloader->ensureResourcesPresent();
    updateCentralViewState();

    m_llmConnectRetryTimer.setSingleShot(true);
    connect(&m_llmConnectRetryTimer, &QTimer::timeout, this, &MainWindow::probeLlmConnectionNow);

    refreshProfileSelectors();
    setLlmConnectionState(LlmConnectionState::Disconnected, "LLM: Disconnected");

    QString lastProj = ConfigManager::instance().lastProjectPath();
    if (!lastProj.isEmpty() && QDir(lastProj).exists()) {
        QTimer::singleShot(0, this, [this, lastProj]() {
            openProjectAtPath(lastProj);
        });
    }

    // При старте AIDE: запускаем локальную LLM (если включён автозапуск) и выполняем автоматический Test/Fetch
    QTimer::singleShot(0, this, [this]() {
        const auto& cfgMgr = ConfigManager::instance();
        if (cfgMgr.aiBehavior().autoStartLocalExecutable) {
            LocalLlmProcessManager::instance().syncWithProfile(cfgMgr.aiConfig(), true);
        }
        startLlmConnectionCheck(true);
    });
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
    m_projectTree->setUniformRowHeights(true);
    m_fsModel = new ProjectTreeModel(m_projectMgr, this);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fsModel->setNameFilterDisables(false);
    m_projectTree->setModel(m_fsModel);
    for (int col = 1; col < 4; ++col) m_projectTree->hideColumn(col);
    m_projectTree->header()->hide();
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);

    applyProjectExplorerFilter();

    // Нижняя панель быстрого поиска файлов в Project Explorer
    auto* searchFooterBox = new QWidget(projExplorerPage);
    searchFooterBox->setStyleSheet(
        "QWidget { background-color: rgba(128, 128, 150, 0.12); border-top: 1px solid rgba(128, 128, 150, 0.25); }");
    auto* searchFooterLay = new QHBoxLayout(searchFooterBox);
    searchFooterLay->setContentsMargins(6, 4, 6, 4);
    searchFooterLay->setSpacing(4);

    m_projectSearchEdit = new QLineEdit(searchFooterBox);
    m_projectSearchEdit->setPlaceholderText("Find file in project...");
    m_projectSearchEdit->setClearButtonEnabled(true);
    m_projectSearchEdit->setToolTip("Case-insensitive file name search. Press Enter for next match.");

    m_projectSearchCountLabel = new QLabel("0/0", searchFooterBox);
    m_projectSearchCountLabel->setStyleSheet("color: #888899; font-size: 10px; font-weight: bold; border: none; background: transparent;");
    m_projectSearchCountLabel->hide();

    m_projectSearchPrevBtn = new QPushButton(QString::fromUtf8("▲"), searchFooterBox);
    m_projectSearchPrevBtn->setFixedWidth(24);
    m_projectSearchPrevBtn->setToolTip("Previous matching file");
    m_projectSearchPrevBtn->setEnabled(false);

    m_projectSearchNextBtn = new QPushButton(QString::fromUtf8("▼"), searchFooterBox);
    m_projectSearchNextBtn->setFixedWidth(24);
    m_projectSearchNextBtn->setToolTip("Next matching file (Enter)");
    m_projectSearchNextBtn->setEnabled(false);

    searchFooterLay->addWidget(m_projectSearchEdit, 1);
    searchFooterLay->addWidget(m_projectSearchCountLabel);
    searchFooterLay->addWidget(m_projectSearchPrevBtn);
    searchFooterLay->addWidget(m_projectSearchNextBtn);

    projExplorerLay->addWidget(pathHeaderBox);
    projExplorerLay->addWidget(m_projectTree, 1);
    projExplorerLay->addWidget(searchFooterBox);

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

    // Комбобокс выбора профиля ИИ в статусбаре
    auto* statusProfLbl = new QLabel("Profile:", this);
    statusProfLbl->setStyleSheet("color: #888899; font-size: 11px; margin-left: 4px;");
    m_statusProfileCombo = new QComboBox(this);
    m_statusProfileCombo->setMinimumWidth(150);
    m_statusProfileCombo->setToolTip("Switch active AI Connection Profile");

    // Кнопка-индикатор статуса подключения к LLM (серая / жёлтая / зелёная лампа)
    m_llmConnStatusBtn = new QPushButton("LLM: Disconnected", this);
    m_llmConnStatusBtn->setFlat(true);
    m_llmConnStatusBtn->setIconSize(QSize(14, 14));
    m_llmConnStatusBtn->setToolTip("Click to test / reconnect to current LLM endpoint");
    connect(m_llmConnStatusBtn, &QPushButton::clicked, this, [this]() {
        startLlmConnectionCheck(true);
    });

    m_queueStatusBtn = new QPushButton("AI: Idle", this);
    m_queueStatusBtn->setFlat(true);
    m_queueStatusBtn->setToolTip("Click to open AI Task Queue");
    connect(m_queueStatusBtn, &QPushButton::clicked, this, [this]() {
        m_taskQueueDock->show();
        m_taskQueueDock->raise();
    });

    m_langStatusLabel = new QLabel("Lang: -", this);
    statusBar()->addPermanentWidget(m_langStatusLabel);
    statusBar()->addPermanentWidget(statusProfLbl);
    statusBar()->addPermanentWidget(m_statusProfileCombo);
    statusBar()->addPermanentWidget(m_llmConnStatusBtn);
    statusBar()->addPermanentWidget(m_queueStatusBtn);
}

void MainWindow::setupMenus() {
    // 1. FILE
    QMenuCompat* fileMenu = static_cast<QMenuCompat *>(menuBar()->addMenu("&File"));
    fileMenu->addAction("New File", QKeySequence::New, this, [this]() {
        m_tabWidget->newDocument();
        updateCentralViewState();
    });
    fileMenu->addAction("Open File...", QKeySequence::Open, this, &MainWindow::onOpenFile);
    fileMenu->addAction("Open Folder / Project...", QKeySequence("Ctrl+Shift+O"), this, &MainWindow::onOpenFolder);

    m_recentProjectsMenu = static_cast<QMenuCompat *>(fileMenu->addMenu("Recent Projects"));
    connect(m_recentProjectsMenu, &QMenu::aboutToShow, this, &MainWindow::populateRecentProjectsMenu);

    fileMenu->addAction("Close All Projects", QKeySequence("Ctrl+Shift+W"), this, &MainWindow::onCloseAllProjects);
    fileMenu->addSeparator();
    fileMenu->addAction("Save", QKeySequence::Save, m_tabWidget, &EditorTabWidget::saveCurrentDocument);
    fileMenu->addAction("Save As...", QKeySequence::SaveAs, m_tabWidget, &EditorTabWidget::saveCurrentDocumentAs);
    fileMenu->addAction("Save All", QKeySequence("Ctrl+Shift+S"), m_tabWidget, &EditorTabWidget::saveAllDocuments);
    fileMenu->addAction("Save Project (.aide)", this, [this]() {
        if (m_projectMgr->saveProjectFile()) statusBar()->showMessage("Project and subprojects (.aide) saved.", 3000);
    });
    fileMenu->addAction("Export Project Summaries as Wiki (Markdown)...", this, &MainWindow::onExportProjectWiki);
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
    QMenuCompat* editMenu = static_cast<QMenuCompat *>(menuBar()->addMenu("&Edit"));
    editMenu->addAction("Undo", QKeySequence::Undo, this, [this]() {
        if (m_tabWidget->isCurrentTabInRawEditMode())
            if (auto* e = m_tabWidget->currentEditor()) e->undo();
    });
    editMenu->addAction("Redo", QKeySequence::Redo, this, [this]() {
        if (m_tabWidget->isCurrentTabInRawEditMode())
            if (auto* e = m_tabWidget->currentEditor()) e->redo();
    });
    editMenu->addSeparator();
    editMenu->addAction("Cut", QKeySequence::Cut, this, [this]() {
        if (m_tabWidget->isCurrentTabInRawEditMode())
            if (auto* e = m_tabWidget->currentEditor()) e->cut();
    });
    editMenu->addAction("Copy", QKeySequence::Copy, this, [this]() {
        if (auto* fw = QApplication::focusWidget()) {
            if (auto* te = qobject_cast<QTextEdit*>(fw)) { te->copy(); return; }
            if (auto* pte = qobject_cast<QPlainTextEdit*>(fw)) { pte->copy(); return; }
        }
        if (auto* e = m_tabWidget->currentEditor()) e->copy();
    });
    editMenu->addAction("Paste", QKeySequence::Paste, this, [this]() {
        if (m_tabWidget->isCurrentTabInRawEditMode())
            if (auto* e = m_tabWidget->currentEditor()) e->paste();
    });
    editMenu->addAction("Select All", QKeySequence::SelectAll, this, [this]() {
        if (auto* fw = QApplication::focusWidget()) {
            if (auto* te = qobject_cast<QTextEdit*>(fw)) { te->selectAll(); return; }
            if (auto* pte = qobject_cast<QPlainTextEdit*>(fw)) { pte->selectAll(); return; }
        }
        if (auto* e = m_tabWidget->currentEditor()) e->selectAll();
    });
    editMenu->addSeparator();
    editMenu->addAction("Find / Replace...", QKeySequence::Find, this, &MainWindow::onFindReplaceDialog);
    editMenu->addSeparator();
    editMenu->addAction("File Types && Syntax Preferences...", this, &MainWindow::onEditorPreferences);

    m_aiToolBar = addToolBar("AI Toolbar");
    m_aiToolBar->setObjectName("AiMainToolBar");
    m_aiToolBar->setIconSize(QSize(22, 22));
    m_aiToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_aiToolBar->setMovable(true);

    // 3. VIEW
    QMenuCompat* viewMenu = static_cast<QMenuCompat *>(menuBar()->addMenu("&View"));
    viewMenu->addAction(m_aiToolBar->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(m_projectDock->toggleViewAction());
    viewMenu->addAction(m_inspectorDock->toggleViewAction());
    viewMenu->addAction(m_promptHistoryDock->toggleViewAction());
    viewMenu->addAction(m_taskQueueDock->toggleViewAction());
    viewMenu->addAction(m_debugConsoleDock->toggleViewAction());
    viewMenu->addSeparator();
    m_themeMenu = static_cast<QMenuCompat *>(viewMenu->addMenu("Color Scheme"));
    connect(m_themeMenu, &QMenu::aboutToShow, this, &MainWindow::populateThemeMenu);

    m_langMenu = static_cast<QMenuCompat *>(viewMenu->addMenu("Syntax Highlighting"));
    connect(m_langMenu, &QMenu::aboutToShow, this, &MainWindow::populateSyntaxMenu);

    viewMenu->addAction("Download / Update Syntax && Themes", this, [this]() {
        statusBar()->showMessage("Downloading syntax & theme files...", 3000);
        m_downloader->forceDownloadAll();
    });
    viewMenu->addSeparator();
    viewMenu->addAction("Toggle Markdown View Mode", QKeySequence("Ctrl+Shift+M"), this, [this]() {
        m_tabWidget->toggleCurrentMarkdownViewMode();
    });
    viewMenu->addSeparator();
    viewMenu->addAction("Zoom In", QKeySequence::ZoomIn, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->zoomIn(1); });
    viewMenu->addAction("Zoom Out", QKeySequence::ZoomOut, this, [this]() { if (auto* e = m_tabWidget->currentEditor()) e->zoomOut(1); });

    // 4. AI
    QMenuCompat* aiMenu = static_cast<QMenuCompat *>(menuBar()->addMenu("&AI"));

    // 4.1. Current File
    QMenuCompat* currentFileMenu = static_cast<QMenuCompat *>(aiMenu->addMenu("Current &File"));
    m_otherFileAiActions.clear();

    m_actCodeExplain = currentFileMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::CodeExplain),
        "Explain Code",
        this,
        [this]() { if (auto* e = m_tabWidget->currentEditor()) e->triggerAiAction(AiEditAction::ExplainCode); });
    m_actCodeExplain->setToolTip("Explain Code (Current File / Selection)");

    auto addFileAiAct = [&](const QString& label, AiEditAction act) -> QAction* {
        QAction* a = currentFileMenu->addAction(label, this, [this, act]() {
            if (auto* e = m_tabWidget->currentEditor()) e->triggerAiAction(act);
        });
        m_otherFileAiActions.append(a);
        return a;
    };

    addFileAiAct("Refactor && Clean Up", AiEditAction::RefactorCode);
    addFileAiAct("Find Bugs && Edge Cases", AiEditAction::FindBugs);
    addFileAiAct("Optimize Performance", AiEditAction::OptimizePerformance);

    m_actAddDocs = currentFileMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::AddDocs),
        "Add Documentation / Comments",
        this,
        [this]() { if (auto* e = m_tabWidget->currentEditor()) e->triggerAiAction(AiEditAction::AddDocumentation); });
    m_actAddDocs->setToolTip("Add Documentation / Comments (Current File / Selection)");

    addFileAiAct("Generate Unit Tests", AiEditAction::GenerateUnitTests);
    addFileAiAct("Complete / Continue Code", AiEditAction::CompleteCode);

    currentFileMenu->addSeparator();
    m_actFileCustomPrompt = currentFileMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::FileCustomPrompt),
        "Custom Prompt...",
        this,
        [this]() { if (auto* e = m_tabWidget->currentEditor()) e->triggerAiAction(AiEditAction::CustomPrompt); });
    m_actFileCustomPrompt->setToolTip("File Custom Prompt...");

    m_actFileAiContinueGen = currentFileMenu->addAction(
        "Continue Last AI Generation",
        this,
        [this]() { if (auto* e = m_tabWidget->currentEditor()) e->triggerContinueAiGeneration(); });
    m_actFileAiFollowUp = currentFileMenu->addAction(
        "Continue Last AI Request (Follow-up)...",
        this,
        [this]() { if (auto* e = m_tabWidget->currentEditor()) e->triggerFollowUpAiPrompt(); });

    currentFileMenu->addSeparator();
    m_actFileSummaryRebuild = currentFileMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::FileSummaryRebuild),
        "Rebuild Summary",
        this,
        [this]() {
            if (auto* e = m_tabWidget->currentEditor(); e && !e->filePath().isEmpty())
                rebuildSingleFileSummary(e->filePath(), true);
        });
    m_actFileSummaryRebuild->setToolTip("Rebuild File Summary");

    m_actFileSummaryRemove = currentFileMenu->addAction(
        "Remove Summary",
        this,
        [this]() {
            if (auto* e = m_tabWidget->currentEditor(); e && !e->filePath().isEmpty())
                removeSingleFileSummary(e->filePath());
        });
    m_actFileSummaryRemove->setToolTip("Completely remove summary for the current file");

    // 4.2. Project
    QMenuCompat* projectAiMenu = static_cast<QMenuCompat *>(aiMenu->addMenu("&Project"));
    m_actProjCustomPrompt = projectAiMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::ProjectCustomPrompt),
        "Custom Prompt...",
        QKeySequence("Ctrl+Shift+P"),
        this,
        [this]() { onProjectCustomPrompt(); });
    m_actProjCustomPrompt->setToolTip("Project Custom Prompt... (Ctrl+Shift+P)");

    projectAiMenu->addSeparator();
    m_actProjSummaryRebuild = projectAiMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::ProjectSummaryRebuild),
        "Rebuild Summary",
        this,
        [this]() { onRebuildProjectSummary(m_inspectorDock ? m_inspectorDock->selectedProjectScope() : QString()); });
    m_actProjSummaryRebuild->setToolTip("Rebuild Project / Subproject Summary");

    m_actProjRecursiveSummary = projectAiMenu->addAction(
        AppIcon::actionIcon(AppIcon::ActionIcon::ProjectRecursiveSummary),
        "Generate Summaries (Recursive)...",
        this,
        [this]() { onRecursiveProjectSummary(); });
    m_actProjRecursiveSummary->setToolTip("Generate Summaries for Project Files (Recursive)...");

    projectAiMenu->addSeparator();
    m_actProjExportWiki = projectAiMenu->addAction(
        "Export Project Summaries as Wiki (Markdown)...",
        this,
        &MainWindow::onExportProjectWiki);
    m_actProjExportWiki->setToolTip("Export project, subproject, and file AI summaries as a structured Markdown Wiki");

    auto* tbProfLabel = new QLabel(" AI Profile: ", m_aiToolBar);
    tbProfLabel->setStyleSheet("color: #888899; font-size: 11px; font-weight: bold;");
    m_aiToolBar->addWidget(tbProfLabel);

    m_toolbarProfileCombo = new QComboBox(m_aiToolBar);
    m_toolbarProfileCombo->setMinimumWidth(165);
    m_toolbarProfileCombo->setToolTip("Switch active AI Connection Profile");
    m_aiToolBar->addWidget(m_toolbarProfileCombo);
    m_aiToolBar->addSeparator();

    m_aiToolBar->addAction(m_actProjCustomPrompt);
    m_aiToolBar->addAction(m_actProjSummaryRebuild);
    m_aiToolBar->addAction(m_actProjRecursiveSummary);
    m_aiToolBar->addSeparator();
    m_aiToolBar->addAction(m_actFileCustomPrompt);
    m_aiToolBar->addAction(m_actFileSummaryRebuild);
    m_aiToolBar->addAction(m_actCodeExplain);
    m_aiToolBar->addAction(m_actAddDocs);

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
    m_windowMenu = static_cast<QMenuCompat *>(menuBar()->addMenu("&Window"));
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::populateWindowMenu);

    // 6. HELP
    QMenuCompat* helpMenu = static_cast<QMenuCompat *>(menuBar()->addMenu("&Help"));
    helpMenu->addAction("About AIDE", this, [this]() {
        QMessageBox aboutBox(this);
        aboutBox.setWindowTitle("About AIDE");
        aboutBox.setIconPixmap(AppIcon::renderIconPixmap(64));
        aboutBox.setText(
            QString("<b>AIDE (AI Development Environment)</b><br>"
                    "<b>Version:</b> %1<br><br>"
                    "Multi-Document Code Editor with Streaming LLM Integration")
                .arg(QApplication::applicationVersion()));
        aboutBox.exec();
    });
    helpMenu->addAction("About Qt", qApp, &QApplication::aboutQt);

    updateAiActionsState();
}

void MainWindow::setupConnections() {
    connect(m_projectTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!m_fsModel->isDir(idx)) {
            m_tabWidget->openFile(m_fsModel->filePath(idx));
            updateCentralViewState();
        }
    });
    connect(m_projectTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onProjectTreeContextMenu);

    connect(m_projectSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onProjectSearchTextChanged);
    connect(m_projectSearchEdit, &QLineEdit::returnPressed, this, &MainWindow::onProjectSearchNext);
    connect(m_projectSearchNextBtn, &QPushButton::clicked, this, &MainWindow::onProjectSearchNext);
    connect(m_projectSearchPrevBtn, &QPushButton::clicked, this, &MainWindow::onProjectSearchPrev);

    connect(m_tabWidget, &EditorTabWidget::activeEditorChanged, this, [this](CodeEditor* ed) {
        onActiveEditorChanged(ed);
        updateCentralViewState();
    });
    connect(m_tabWidget, &EditorTabWidget::documentOpened,
            m_summaryWorker, &SummaryWorker::checkAndScheduleFileSummary);
    connect(m_tabWidget, &EditorTabWidget::documentSaved,
            m_summaryWorker, &SummaryWorker::checkAndScheduleFileSummary);
    connect(m_tabWidget, &EditorTabWidget::aiActionTriggered, this, &MainWindow::onEditorAiAction);
    connect(m_tabWidget, &EditorTabWidget::aiContinueTriggered, this, &MainWindow::onEditorAiContinue);
    connect(m_tabWidget, &EditorTabWidget::cancelAiTriggered, m_taskQueue, &AiTaskQueue::cancelTaskForEditor);
    connect(m_tabWidget, &EditorTabWidget::diffRequested, this, &MainWindow::onCompareUnsavedDiff);

    connect(m_aiProvider, &AiProvider::debugLog, m_debugConsoleDock, &AiDebugConsoleDock::appendLog);

    connect(m_inspectorDock, &ContextInspectorDock::rebuildFileSummaryClicked, this, [this](const QString& path) {
        rebuildSingleFileSummary(path, true);
    });
    connect(m_inspectorDock, &ContextInspectorDock::continueFileSummaryClicked, this, &MainWindow::continueSingleFileSummary);
    connect(m_inspectorDock, &ContextInspectorDock::clearFileSummaryClicked, this, &MainWindow::clearSingleFileSummary);
    connect(m_inspectorDock, &ContextInspectorDock::removeFileSummaryClicked, this, &MainWindow::removeSingleFileSummary);
    connect(m_inspectorDock, &ContextInspectorDock::rebuildProjectSummaryClicked, this, &MainWindow::onRebuildProjectSummary);
    connect(m_inspectorDock, &ContextInspectorDock::continueProjectSummaryClicked, this, &MainWindow::onContinueProjectSummary);

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString&) {
        m_projectTree->viewport()->update();
    });
    connect(m_projectMgr, &ProjectManager::subprojectsChanged, this, [this]() {
        m_projectTree->viewport()->update();
    });
    connect(m_projectMgr, &ProjectManager::fileRemovedFromProject, this,
            [this](const QString& relPath, const QString& absPath) {
                m_taskQueue->cancelFileSummaryTask(relPath);
                m_inspectorDock->resetLiveSummary(relPath, false);
                if (auto* ed = m_tabWidget->findEditorByPath(absPath)) {
                    int idx = m_tabWidget->indexOf(ed);
                    if (idx >= 0) {
                        m_tabWidget->closeDocument(idx);
                        updateCentralViewState();
                    }
                }
                if (!m_projectSearchEdit->text().trimmed().isEmpty()) {
                    onProjectSearchTextChanged(m_projectSearchEdit->text());
                } else {
                    m_projectTree->viewport()->update();
                }
            });
    connect(m_projectMgr, &ProjectManager::fileRenamedInProject, this,
            [this](const QString& oldRelPath, const QString&, const QString& newAbsPath) {
                m_taskQueue->cancelFileSummaryTask(oldRelPath);
                m_inspectorDock->resetLiveSummary(oldRelPath, false);
                const QString oldAbsPath = m_projectMgr->toAbsolutePath(oldRelPath);
                if (auto* ed = m_tabWidget->findEditorByPath(oldAbsPath)) {
                    ed->loadFile(newAbsPath);
                    m_tabWidget->refreshEditorModeBar(ed);
                    if (m_tabWidget->currentEditor() == ed) {
                        m_inspectorDock->setActiveFile(newAbsPath);
                    }
                }
                if (!m_projectSearchEdit->text().trimmed().isEmpty()) {
                    onProjectSearchTextChanged(m_projectSearchEdit->text());
                } else {
                    m_projectTree->viewport()->update();
                }
            });
    connect(m_projectMgr, &ProjectManager::projectLoaded, this, [this](const QString&) {
        updateAiActionsState();
    });
    connect(m_projectMgr, &ProjectManager::projectClosed, this, [this]() {
        updateAiActionsState();
    });
    connect(m_tabWidget, &EditorTabWidget::documentSaved, this, [this](const QString&, const QByteArray&) {
        updateAiActionsState();
    });

    connect(m_taskQueue, &AiTaskQueue::queueStatusChanged, this, [this](int high, int low, bool busy) {
        if (!busy && high == 0 && low == 0) {
            m_queueStatusBtn->setText("AI: Idle");
        } else if (!m_taskQueue->isLlmConnected() && !busy) {
            m_queueStatusBtn->setText(QString("AI: Waiting for LLM (User: %1 | Bg: %2)").arg(high).arg(low));
        } else {
            m_queueStatusBtn->setText(QString("AI: Busy (User: %1 | Background: %2)").arg(high).arg(low));
        }
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

    if (m_toolbarProfileCombo) {
        connect(m_toolbarProfileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onQuickProfileSelected);
    }
    if (m_statusProfileCombo) {
        connect(m_statusProfileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onQuickProfileSelected);
    }

    connect(&ConfigManager::instance(), &ConfigManager::aiConfigChanged, this, [this](const AiConnectionConfig&) {
        refreshProfileSelectors();
    });

    connect(m_aiProvider, &AiProvider::modelsFetched, this, &MainWindow::onModelsFetchedForStatus);

    connect(&LocalLlmProcessManager::instance(), &LocalLlmProcessManager::debugLog,
            m_debugConsoleDock, &AiDebugConsoleDock::appendLog);
    connect(&LocalLlmProcessManager::instance(), &LocalLlmProcessManager::processStateChanged,
            this, &MainWindow::onLocalLlmProcessStateChanged);
}

void MainWindow::revealProjectFileInTree(const QString& relFilePath) {
    if (relFilePath.isEmpty() || !m_projectMgr->hasOpenProject()) return;
    const QString absPath = m_projectMgr->toAbsolutePath(relFilePath);
    const QModelIndex rootIdx = m_projectTree->rootIndex();
    const QModelIndex targetIdx = m_fsModel->index(absPath);
    if (!targetIdx.isValid()) return;

    // Раскрываем все родительские папки от корня проекта до найденного файла
    QList<QModelIndex> parentsToExpand;
    for (QModelIndex p = targetIdx.parent(); p.isValid() && p != rootIdx; p = p.parent()) {
        parentsToExpand.prepend(p);
    }
    for (const QModelIndex& p : parentsToExpand) {
        m_projectTree->expand(p);
    }

    m_projectTree->setCurrentIndex(targetIdx);
    m_projectTree->scrollTo(targetIdx, QAbstractItemView::PositionAtCenter);

    // Повторная докрутка через 60 мс на случай асинхронной подгрузки вложенной папки QFileSystemModel
    QTimer::singleShot(60, this, [this, absPath]() {
        if (!m_projectMgr->hasOpenProject()) return;
        const QModelIndex idx = m_fsModel->index(absPath);
        if (idx.isValid()) {
            m_projectTree->setCurrentIndex(idx);
            m_projectTree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        }
    });
}

QIcon MainWindow::createStatusLampIcon(const QColor& fillColor, const QColor& glowColor) {
    QIcon icon;
    for (int sz : {14, 16, 20}) {
        QPixmap px(sz, sz);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QPointF center(sz * 0.5, sz * 0.5);
        const double radius = sz * 0.34;

        // Внешнее свечение лампы
        QRadialGradient glow(center, sz * 0.48);
        QColor g0 = glowColor;
        g0.setAlpha(150);
        QColor g1 = glowColor;
        g1.setAlpha(0);
        glow.setColorAt(0.0, g0);
        glow.setColorAt(1.0, g1);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(center, sz * 0.48, sz * 0.48);

        // Основной корпус лампы с бликом
        QRadialGradient lampGrad(QPointF(center.x() - radius * 0.3, center.y() - radius * 0.3), radius * 1.3);
        lampGrad.setColorAt(0.0, fillColor.lighter(135));
        lampGrad.setColorAt(0.7, fillColor);
        lampGrad.setColorAt(1.0, fillColor.darker(130));

        p.setPen(QPen(QColor(25, 26, 36, 200), 1.0));
        p.setBrush(lampGrad);
        p.drawEllipse(center, radius, radius);

        icon.addPixmap(px);
    }
    return icon;
}

void MainWindow::setLlmConnectionState(LlmConnectionState state,
                                       const QString& statusText,
                                       const QString& toolTipText) {
    m_llmConnState = state;
    if (!m_llmConnStatusBtn) return;

    static const QIcon grayLamp   = createStatusLampIcon(QColor("#7c7f93"), QColor("#6272a4"));
    static const QIcon yellowLamp = createStatusLampIcon(QColor("#f1fa8c"), QColor("#ffb86c"));
    static const QIcon greenLamp  = createStatusLampIcon(QColor("#50fa7b"), QColor("#50fa7b"));

    const auto& cfg = ConfigManager::instance().aiConfig();
    if (state == LlmConnectionState::Connected) {
        m_llmConnStatusBtn->setIcon(greenLamp);
        m_llmConnStatusBtn->setStyleSheet("QPushButton { color: #50fa7b; font-weight: bold; }");
        m_llmConnStatusBtn->setText(statusText.isEmpty() ? "LLM: Connected" : statusText);
        m_llmConnStatusBtn->setToolTip(
            toolTipText.isEmpty()
                ? QString("Connected to LLM (%1 — %2)\nClick to re-test connection.")
                      .arg(cfg.profileName, cfg.endpointUrl)
                : toolTipText);
    } else if (state == LlmConnectionState::Connecting) {
        m_llmConnStatusBtn->setIcon(yellowLamp);
        m_llmConnStatusBtn->setStyleSheet("QPushButton { color: #f1fa8c; font-weight: bold; }");
        m_llmConnStatusBtn->setText(statusText.isEmpty() ? "LLM: Connecting..." : statusText);
        m_llmConnStatusBtn->setToolTip(
            toolTipText.isEmpty()
                ? QString("Testing connection / waiting for LLM port (%1 — %2)...\n"
                          "Task queue is paused until connection turns green.")
                      .arg(cfg.profileName, cfg.endpointUrl)
                : toolTipText);
    } else {
        m_llmConnStatusBtn->setIcon(grayLamp);
        m_llmConnStatusBtn->setStyleSheet("QPushButton { color: #888899; }");
        m_llmConnStatusBtn->setText(statusText.isEmpty() ? "LLM: Disconnected" : statusText);
        m_llmConnStatusBtn->setToolTip(
            toolTipText.isEmpty()
                ? QString("No connection to LLM (%1 — %2).\n"
                          "Task queue is paused. Click to retry Test / Fetch.")
                      .arg(cfg.profileName, cfg.endpointUrl)
                : toolTipText);
    }
}

void MainWindow::refreshProfileSelectors() {
    m_updatingProfileCombos = true;
    const auto profiles = ConfigManager::instance().aiProfiles();
    const QString activeName = ConfigManager::instance().activeAiProfileName();

    auto populateCombo = [&](QComboBox* combo) {
        if (!combo) return;
        combo->blockSignals(true);
        combo->clear();
        int activeIdx = 0;
        for (int i = 0; i < profiles.size(); ++i) {
            combo->addItem(profiles[i].profileName, profiles[i].profileName);
            if (profiles[i].profileName == activeName) {
                activeIdx = i;
            }
        }
        combo->setCurrentIndex(activeIdx);
        combo->blockSignals(false);
    };

    populateCombo(m_toolbarProfileCombo);
    populateCombo(m_statusProfileCombo);
    m_updatingProfileCombos = false;
}

void MainWindow::onQuickProfileSelected(int index) {
    if (m_updatingProfileCombos || index < 0) return;

    auto* senderCombo = qobject_cast<QComboBox*>(sender());
    if (!senderCombo) return;

    const QString selectedProfileName = senderCombo->itemData(index).toString().trimmed();
    if (selectedProfileName.isEmpty()) return;

    auto& cfgMgr = ConfigManager::instance();
    if (cfgMgr.activeAiProfileName() == selectedProfileName) return;

    cfgMgr.setActiveAiProfile(selectedProfileName);
    refreshProfileSelectors();

    // Если включён автозапуск локальной LLM — останавливаем процесс старого профиля и запускаем новый
    if (cfgMgr.aiBehavior().autoStartLocalExecutable) {
        LocalLlmProcessManager::instance().syncWithProfile(cfgMgr.aiConfig(), true);
    }

    // Автоматически запускаем Test / Fetch для нового профиля (очередь ждёт зелёной лампы)
    startLlmConnectionCheck(true);

    m_inspectorDock->refreshDisplay();
    statusBar()->showMessage(QString("Switched AI Profile to '%1' — testing connection...").arg(selectedProfileName), 4000);
}

void MainWindow::startLlmConnectionCheck(bool allowLocalStartupRetries) {
    m_llmConnectRetryTimer.stop();

    // Пока соединение не подтверждено, очередь не должна молотить вхолостую
    m_taskQueue->setLlmConnected(false);

    const auto cfg = ConfigManager::instance().aiConfig();
    auto& procMgr = LocalLlmProcessManager::instance();

    const bool hasLocalExec = cfg.useLocalExecutable && !cfg.localExecutablePath.trimmed().isEmpty();
    const bool localStartingOrRunning = hasLocalExec &&
                                        (allowLocalStartupRetries ||
                                         procMgr.isTransitioning() ||
                                         procMgr.isExecutableRunning(cfg.localExecutablePath));

    // Локальная LLM может долго загружать веса в память (до ~90-120 секунд) до открытия HTTP-порта
    m_llmConnectRetriesLeft = localStartingOrRunning ? 60 : 0;

    if (procMgr.transitionState() == LocalLlmProcessManager::TransitionState::Stopping) {
        setLlmConnectionState(LlmConnectionState::Disconnected, "LLM: Stopping...");
        return;
    }

    if (procMgr.transitionState() == LocalLlmProcessManager::TransitionState::Starting ||
        procMgr.transitionState() == LocalLlmProcessManager::TransitionState::Restarting) {
        setLlmConnectionState(LlmConnectionState::Connecting, "LLM: Starting Local Process...");
        m_llmConnectRetryTimer.start(1500);
        return;
    }

    setLlmConnectionState(LlmConnectionState::Connecting, "LLM: Connecting...");
    probeLlmConnectionNow();
}

void MainWindow::probeLlmConnectionNow() {
    m_llmConnectRetryTimer.stop();
    const auto cfg = ConfigManager::instance().aiConfig();

    if (cfg.endpointUrl.trimmed().isEmpty()) {
        setLlmConnectionState(LlmConnectionState::Disconnected, "LLM: Disconnected");
        m_taskQueue->setLlmConnected(false);
        return;
    }

    setLlmConnectionState(LlmConnectionState::Connecting, "LLM: Connecting...");
    m_aiProvider->fetchAvailableModels(cfg);
}

void MainWindow::onModelsFetchedForStatus(const QStringList& models) {
    if (!models.isEmpty()) {
        m_llmConnectRetryTimer.stop();
        m_llmConnectRetriesLeft = 0;

        const auto cfg = ConfigManager::instance().aiConfig();
        QString labelModel = cfg.modelName.trimmed();
        if (labelModel.isEmpty()) {
            labelModel = models.first();
        }
        if (labelModel.length() > 24) {
            labelModel = labelModel.left(22) + "..";
        }

        setLlmConnectionState(
            LlmConnectionState::Connected,
            QString("LLM: Connected (%1)").arg(labelModel),
            QString("Connected to %1 (%2)\nAvailable models: %3\nActive model: %4\nClick to re-test connection.")
                .arg(cfg.profileName, cfg.endpointUrl)
                .arg(models.size())
                .arg(cfg.modelName.isEmpty() ? models.first() : cfg.modelName));

        // Зелёная лампа — разрешаем очереди обрабатывать задачи
        m_taskQueue->setLlmConnected(true);
        return;
    }

    // Если список моделей пуст (порт ещё не отвечает или ошибка соединения):
    // проверяем, не загружается ли сейчас локальный исполняемый файл LLM
    const auto cfg = ConfigManager::instance().aiConfig();
    auto& procMgr = LocalLlmProcessManager::instance();
    const bool localAlive = cfg.useLocalExecutable &&
                            !cfg.localExecutablePath.trimmed().isEmpty() &&
                            (procMgr.isTransitioning() || procMgr.isExecutableRunning(cfg.localExecutablePath));

    if (m_llmConnectRetriesLeft > 0 && localAlive) {
        --m_llmConnectRetriesLeft;
        setLlmConnectionState(
            LlmConnectionState::Connecting,
            "LLM: Waiting for local port...",
            QString("Local LLM process is running, waiting for HTTP server (%1) to become ready...\n"
                    "Remaining automatic retries: %2")
                .arg(cfg.endpointUrl)
                .arg(m_llmConnectRetriesLeft));
        m_llmConnectRetryTimer.start(2000);
        return;
    }

    m_llmConnectRetryTimer.stop();
    m_llmConnectRetriesLeft = 0;
    m_taskQueue->setLlmConnected(false);
    setLlmConnectionState(LlmConnectionState::Disconnected, "LLM: Disconnected");
}

void MainWindow::onLocalLlmProcessStateChanged() {
    auto& procMgr = LocalLlmProcessManager::instance();
    const auto tState = procMgr.transitionState();
    const auto cfg = ConfigManager::instance().aiConfig();

    if (tState == LocalLlmProcessManager::TransitionState::Starting ||
        tState == LocalLlmProcessManager::TransitionState::Restarting) {
        m_llmConnectRetryTimer.stop();
        m_taskQueue->setLlmConnected(false);
        m_llmConnectRetriesLeft = 60;
        setLlmConnectionState(LlmConnectionState::Connecting, "LLM: Starting Local Process...");
        return;
    }

    if (tState == LocalLlmProcessManager::TransitionState::Stopping) {
        m_llmConnectRetryTimer.stop();
        m_llmConnectRetriesLeft = 0;
        m_taskQueue->setLlmConnected(false);
        setLlmConnectionState(LlmConnectionState::Disconnected, "LLM: Stopping...");
        return;
    }

    // Переход завершён (Idle): проверяем, запущен ли процесс
    if (cfg.useLocalExecutable && !cfg.localExecutablePath.trimmed().isEmpty()) {
        if (procMgr.isExecutableRunning(cfg.localExecutablePath)) {
            if (m_llmConnState != LlmConnectionState::Connected) {
                startLlmConnectionCheck(true);
            }
        } else {
            m_llmConnectRetryTimer.stop();
            m_llmConnectRetriesLeft = 0;
            m_taskQueue->setLlmConnected(false);
            setLlmConnectionState(LlmConnectionState::Disconnected, "LLM: Disconnected");
        }
    }
}

void MainWindow::onProjectSearchTextChanged(const QString& text) {
    const QString query = text.trimmed();
    m_fsModel->setHighlightQuery(query);
    m_projectTree->viewport()->update();

    m_projectSearchMatches.clear();
    m_projectSearchIndex = -1;

    if (query.isEmpty() || !m_projectMgr->hasOpenProject()) {
        m_projectSearchCountLabel->hide();
        m_projectSearchPrevBtn->setEnabled(false);
        m_projectSearchNextBtn->setEnabled(false);
        return;
    }

    const QStringList& allFiles = m_projectMgr->allProjectFileRelPaths();
    for (const QString& rel : allFiles) {
        const QString fileName = QFileInfo(rel).fileName();
        if (fileName.contains(query, Qt::CaseInsensitive)) {
            m_projectSearchMatches.append(rel);
        }
    }

    m_projectSearchCountLabel->show();
    if (m_projectSearchMatches.isEmpty()) {
        m_projectSearchCountLabel->setStyleSheet("color: #ff5555; font-size: 10px; font-weight: bold; border: none; background: transparent;");
        m_projectSearchCountLabel->setText("0/0");
        m_projectSearchPrevBtn->setEnabled(false);
        m_projectSearchNextBtn->setEnabled(false);
        return;
    }

    m_projectSearchIndex = 0;
    m_projectSearchCountLabel->setStyleSheet("color: #f1fa8c; font-size: 10px; font-weight: bold; border: none; background: transparent;");
    m_projectSearchCountLabel->setText(QString("1/%1").arg(m_projectSearchMatches.size()));
    m_projectSearchPrevBtn->setEnabled(m_projectSearchMatches.size() > 1);
    m_projectSearchNextBtn->setEnabled(m_projectSearchMatches.size() > 1);

    revealProjectFileInTree(m_projectSearchMatches.first());
}

void MainWindow::onProjectSearchNext() {
    if (m_projectSearchMatches.isEmpty()) return;
    m_projectSearchIndex = (m_projectSearchIndex + 1) % m_projectSearchMatches.size();
    m_projectSearchCountLabel->setText(
        QString("%1/%2").arg(m_projectSearchIndex + 1).arg(m_projectSearchMatches.size()));
    revealProjectFileInTree(m_projectSearchMatches[m_projectSearchIndex]);
}

void MainWindow::onProjectSearchPrev() {
    if (m_projectSearchMatches.isEmpty()) return;
    m_projectSearchIndex = (m_projectSearchIndex - 1 + m_projectSearchMatches.size()) % m_projectSearchMatches.size();
    m_projectSearchCountLabel->setText(
        QString("%1/%2").arg(m_projectSearchIndex + 1).arg(m_projectSearchMatches.size()));
    revealProjectFileInTree(m_projectSearchMatches[m_projectSearchIndex]);
}

void MainWindow::onExportProjectWiki() {
    if (!m_projectMgr->hasOpenProject()) {
        QMessageBox::information(this, "Export Project Wiki", "Please open a project folder first.");
        return;
    }

    const int indexedCount = m_projectMgr->indexedFilesCount(QString(), true);
    const bool hasRootSum = !m_projectMgr->projectSummary().trimmed().isEmpty();
    if (indexedCount == 0 && !hasRootSum) {
        auto ans = QMessageBox::question(
            this,
            "Export Project Wiki",
            "This project does not have any generated file or project summaries yet.\n"
            "Do you still want to export an empty Wiki skeleton?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (ans != QMessageBox::Yes) return;
    }

    const QString startDir = ConfigManager::instance().resolveInitialDialogDir(m_projectMgr->projectRootPath());
    const QString targetDir = QFileDialog::getExistingDirectory(
        this,
        "Select Destination Folder for Markdown Wiki Export",
        startDir);
    if (targetDir.isEmpty()) return;

    ConfigManager::instance().setLastDialogDir(targetDir);

    QString err;
    const int exportedCount = m_projectMgr->exportProjectWiki(targetDir, &err);
    if (exportedCount < 0) {
        QMessageBox::warning(this, "Export Wiki Failed", "Could not export Project Wiki:\n" + err);
        return;
    }

    statusBar()->showMessage(
        QString("Exported Project Wiki (%1 Markdown file(s)) to %2")
            .arg(exportedCount)
            .arg(QDir::toNativeSeparators(targetDir)),
        5000);

    const QString rootReadmePath = QDir(targetDir).filePath("README.md");
    auto openAns = QMessageBox::question(
        this,
        "Project Wiki Exported",
        QString("Successfully exported <b>%1 Markdown (.md) file(s)</b> mirroring the project structure into:<br>"
                "<code>%2</code><br><br>"
                "Would you like to open the exported root <b>README.md</b> in the editor?")
            .arg(exportedCount)
            .arg(QDir::toNativeSeparators(targetDir).toHtmlEscaped()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (openAns == QMessageBox::Yes && QFileInfo::exists(rootReadmePath)) {
        m_tabWidget->openFile(rootReadmePath);
        updateCentralViewState();
    }
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
        QString ownerSub = m_projectMgr->owningProjectRelPathForFile(filePath);
        QString sysPrompt = m_projectMgr->projectSystemPrompt(ownerSub);
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

int MainWindow::enqueueBatchFilesWithProgress(const QStringList& files,
                                              const QString& titlePrefix,
                                              int* outMaxTokens,
                                              int* outCtxWindow) {
    if (files.isEmpty()) return 0;

    QProgressDialog progress(
        QString("Reading %1 selected file(s)...").arg(files.size()),
        "Cancel",
        0,
        files.size(),
        this);
    progress.setWindowTitle(titlePrefix);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(200);
    progress.setMinimumWidth(420);

    const auto openBuffers = collectOpenEditorBuffers();
    QVector<BatchFileSummaryItem> batchItems;
    batchItems.reserve(files.size());

    QString largestPath;
    QByteArray largestContent;

    for (int i = 0; i < files.size(); ++i) {
        if ((i % 15) == 0 || i + 1 == files.size()) {
            progress.setValue(i);
            progress.setLabelText(
                QString("Reading project files (%1 / %2)...\n%3")
                    .arg(i + 1)
                    .arg(files.size())
                    .arg(m_projectMgr->toRelativePath(files[i])));
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                statusBar()->showMessage("Summary scheduling cancelled.", 3000);
                return 0;
            }
        }

        const QString abs = QFileInfo(files[i]).absoluteFilePath();
        QByteArray raw;
        if (openBuffers.contains(abs)) {
            raw = openBuffers.value(abs).toUtf8();
        } else {
            QFile f(abs);
            if (f.open(QIODevice::ReadOnly)) {
                raw = f.readAll();
            }
        }
        if (raw.trimmed().isEmpty()) continue;

        if (raw.size() > largestContent.size()) {
            largestContent = raw;
            largestPath = abs;
        }
        batchItems.append({abs, std::move(raw)});
    }
    progress.setValue(files.size());

    if (batchItems.isEmpty()) return 0;

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = cfg.maxOutputSummaryTokens;
    int ctxWindow = cfg.contextWindow;

    if (!largestPath.isEmpty()) {
        const QString relPath = m_projectMgr->toRelativePath(largestPath);
        const QString ownerSub = m_projectMgr->owningProjectRelPathForFile(relPath);
        const QString sysPrompt = m_projectMgr->projectSystemPrompt(ownerSub);
        const QString userPrompt = ConfigManager::instance().promptTemplate("FileSummary")
                                   + QString("\n\nFile: %1\n\n%2").arg(relPath, QString::fromUtf8(largestContent));
        if (!verifyAndAdjustContextLimits(
                QString("%1 (Largest File: %2)").arg(titlePrefix, relPath),
                sysPrompt,
                userPrompt,
                qMax(512, maxTokens),
                true,
                maxTokens,
                ctxWindow)) {
            return 0;
        }
    }

    if (outMaxTokens) *outMaxTokens = maxTokens;
    if (outCtxWindow) *outCtxWindow = ctxWindow;

    return m_summaryWorker->scheduleFileSummariesBatch(batchItems, maxTokens, ctxWindow);
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

    int userChoice = 0;
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

    const int scheduled = enqueueBatchFilesWithProgress(files, "Recursive Summary");
    if (scheduled > 0) {
        statusBar()->showMessage(
            QString("Queued %1 file(s) for background AI summary generation.").arg(scheduled),
            5000);
    }
}

void MainWindow::onRebuildProjectSummary(const QString& subprojectRel) {
    if (!m_projectMgr->hasOpenProject()) return;

    const QString cleanSubRel = m_projectMgr->toRelativePath(subprojectRel);
    if (!cleanSubRel.isEmpty()) {
        m_projectMgr->ensureSubprojectNode(cleanSubRel);
    }

    const QString targetName = cleanSubRel.isEmpty()
                                   ? QStringLiteral("Root Project")
                                   : QString("Subproject '%1'").arg(cleanSubRel);

    const QStringList unindexedFiles = m_projectMgr->unindexedAllowedFilePaths(cleanSubRel, true);
    const int indexedCount = m_projectMgr->indexedFilesCount(cleanSubRel, true);

    if (!unindexedFiles.isEmpty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Rebuild Summary — " + targetName);
        msgBox.setIcon(QMessageBox::Question);

        if (indexedCount == 0) {
            msgBox.setText(
                QString("<b>%1</b> currently has <b>0 indexed files</b>, and <b>%2 allowed source file(s)</b> without a summary.<br><br>"
                        "Would you like to generate summaries for those %2 file(s) now and then automatically synthesize the %1 summary (bottom-up across all subfolders)?")
                    .arg(targetName)
                    .arg(unindexedFiles.size()));
        } else {
            msgBox.setText(
                QString("<b>%1</b> currently has only <b>%2 indexed file(s)</b>, while <b>%3 allowed source file(s)</b> do not have summaries yet.<br><br>"
                        "If you rebuild now, only the %2 already-indexed file(s) will be included. "
                        "Would you like to index all %3 missing file(s) first and then automatically synthesize the complete summary?")
                    .arg(targetName)
                    .arg(indexedCount)
                    .arg(unindexedFiles.size()));
        }

        QPushButton* indexAndBuildBtn = msgBox.addButton(
            QString("Index Missing (%1) && Rebuild Summary").arg(unindexedFiles.size()),
            QMessageBox::AcceptRole);
        QPushButton* buildExistingOnlyBtn = nullptr;
        if (indexedCount > 0) {
            buildExistingOnlyBtn = msgBox.addButton(
                QString("Use Existing Summaries Only (%1)").arg(indexedCount),
                QMessageBox::ActionRole);
        }
        QPushButton* cancelBtn = msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(indexAndBuildBtn);
        msgBox.exec();

        if (msgBox.clickedButton() == cancelBtn) {
            return;
        }
        if (msgBox.clickedButton() == indexAndBuildBtn) {
            int maxTokens = 0;
            int ctxWindow = 0;
            int scheduled = enqueueBatchFilesWithProgress(
                unindexedFiles,
                "Indexing " + targetName,
                &maxTokens,
                &ctxWindow);
            if (scheduled > 0) {
                m_summaryWorker->forceRebuildProjectSummary(maxTokens, ctxWindow, cleanSubRel);
                statusBar()->showMessage(
                    QString("Queued %1 file(s); %2 summary (and nested virtual subprojects) will be synthesized automatically.")
                        .arg(scheduled)
                        .arg(targetName),
                    5000);
            }
            return;
        }
        (void)buildExistingOnlyBtn;
    }

    QString digest = m_projectMgr->buildAggregatedFilesDigest(cleanSubRel);
    if (digest.trimmed().isEmpty()) {
        m_summaryWorker->forceRebuildProjectSummary(0, 0, cleanSubRel);
        statusBar()->showMessage(
            QString("Scheduled %1 summary (will run as soon as pending file summaries finish).").arg(targetName),
            4000);
        return;
    }

    const auto& cfg = ConfigManager::instance().aiConfig();
    int maxTokens = cfg.maxOutputSummaryTokens;
    int ctxWindow = cfg.contextWindow;

    QString sysPrompt = m_projectMgr->projectSystemPrompt(cleanSubRel);
    QString userPrompt = ConfigManager::instance().promptTemplate("ProjectSummary") + "\n\n" + digest;
    QString title = cleanSubRel.isEmpty()
                        ? QStringLiteral("Rebuild Project Summary")
                        : QString("Rebuild Subproject Summary (%1)").arg(cleanSubRel);

    if (!verifyAndAdjustContextLimits(title,
                                      sysPrompt,
                                      userPrompt,
                                      qMax(1024, maxTokens),
                                      true,
                                      maxTokens,
                                      ctxWindow)) {
        return;
    }

    m_summaryWorker->forceRebuildProjectSummary(maxTokens, ctxWindow, cleanSubRel);
    statusBar()->showMessage(QString("Queued %1 summary generation.").arg(targetName), 4000);
}

void MainWindow::onContinueProjectSummary(const QString& subprojectRel) {
    m_summaryWorker->continueProjectSummary({}, 0, 0, subprojectRel);
    statusBar()->showMessage("Continuing project / subproject summary generation...", 3000);
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

    int indexedCount = m_projectMgr->indexedFilesCount();
    int subprojectsCount = m_projectMgr->subprojectRelPaths().size();
    bool hasSummary = !m_projectMgr->projectSummary().trimmed().isEmpty() || (indexedCount > 0);
    auto* statusLbl = new QLabel(
        QString("<b>Project:</b> %1<br>"
                "<span style='color:#888899;'>Project Summary Ready: <b>%2</b> | Indexed Project Files: <b>%3</b> | Indexed Subprojects: <b>%4</b></span>")
            .arg(QDir::toNativeSeparators(m_projectMgr->projectRootPath()),
                 hasSummary ? "Yes" : "No (Empty)",
                 QString::number(indexedCount),
                 QString::number(subprojectsCount)),
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
    const int projSummaryChars = m_projectMgr->projectSummary().length();

    auto updateLiveContextEstimate = [&]() {
        auto mode = static_cast<ProjectCustomPromptContextMode>(ctxCombo->currentData().toInt());
        bool usesFiles = (mode != ProjectCustomPromptContextMode::ProjectSummaryOnly);
        bool usesProjSum = (mode != ProjectCustomPromptContextMode::IndexedSourcesMarkdownOnly);

        filesBox->setEnabled(usesFiles);

        int selCount = fileSelector->selectedFilesCount();
        qint64 selBytes = fileSelector->selectedFilesTotalBytes();
        if (usesFiles) {
            selectedCountLbl->setText(
                fileSelector->isLoading()
                    ? QStringLiteral("Loading project tree...")
                    : QString("Selected for prompt context: %1 file(s) (~%2 KB)")
                          .arg(selCount)
                          .arg(selBytes / 1024));
        } else {
            selectedCountLbl->setText("File selection is inactive in '1. Project Summary Only' mode.");
        }

        qint64 estProjCtxChars = 0;
        if (usesProjSum) estProjCtxChars += projSummaryChars;
        if (usesFiles) estProjCtxChars += selBytes + (selCount * 96);

        QString customInst = promptEdit->toPlainText().trimmed();
        qint64 totalChars = sysPrompt.length() + customInst.length() + mdSuffix.length() + estProjCtxChars;
        int promptTok = static_cast<int>((totalChars + 3) / 4) + 128;
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

        const bool isEmptyProject = (fileSelector->totalFilesCount() == 0) &&
                                    (m_projectMgr->indexedFilesCount() == 0) &&
                                    m_projectMgr->projectSummary().trimmed().isEmpty();

        bool userConfirmedEmptySelection = false;
        if (!isEmptyProject &&
            selectedMode != ProjectCustomPromptContextMode::ProjectSummaryOnly &&
            selectedFiles.isEmpty()) {
            auto ans = QMessageBox::question(
                &dlg,
                "No Project Files Selected",
                "No files are checked in the project tree.\n"
                "Do you want to continue without source files in the context?",
                QMessageBox::Yes | QMessageBox::No);
            if (ans == QMessageBox::No) return;
            userConfirmedEmptySelection = true;
        }

        int includedFiles = 0;
        QString projectContext = m_projectMgr->buildProjectCustomPromptContext(
            selectedMode, selectedFiles, openBuffers, &includedFiles);

        if (!isEmptyProject && !userConfirmedEmptySelection && projectContext.trimmed().isEmpty()) {
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

    auto* resWin = new ProjectPromptResultDialog(
        finalCustomInst, m_projectMgr->projectRootPath(), taskId, sysPrompt, finalFullUserPrompt, this);
    m_projectPromptWindows.insert(taskId, resWin);

    connect(resWin, &ProjectPromptResultDialog::cancelTaskRequested, m_taskQueue, &AiTaskQueue::cancelTaskById);
    connect(resWin, &ProjectPromptResultDialog::continueRequestTriggered, this,
            [this](ProjectPromptResultDialog* dlgPtr,
                   const QString& sysP,
                   const QVector<AiChatMessage>& history,
                   const QString& followUpUserPrompt,
                   bool seamlessAppend) {
                if (!dlgPtr) return;
                const auto& aiCfg = ConfigManager::instance().aiConfig();
                QString mdSuffix = ConfigManager::instance().promptTemplate("ProjectCustomPrompt");
                QString effectiveUser = (!seamlessAppend && !mdSuffix.trimmed().isEmpty())
                                            ? (followUpUserPrompt + "\n\n" + mdSuffix.trimmed())
                                            : followUpUserPrompt;

                AiTask contTask;
                contTask.priority = TaskPriority::High_UserAction;
                contTask.targetType = TaskTargetType::ProjectCustomPrompt;
                contTask.filePath = "[Project Custom Prompt — Continue]";
                contTask.llmModel = aiCfg.llmIdentifier();
                contTask.maxOutputTokens = aiCfg.maxOutputCodeTokens;
                contTask.contextWindow = aiCfg.contextWindow;
                contTask.systemPrompt = sysP;
                contTask.conversationHistory = history;
                contTask.userPrompt = effectiveUser;
                contTask.isContinuation = true;
                contTask.appendToPreviousOutput = seamlessAppend;
                contTask.stream = true;

                QString newId = m_taskQueue->enqueueTask(contTask);
                m_projectPromptWindows.insert(newId, dlgPtr);
                dlgPtr->beginFollowUpStream(newId, followUpUserPrompt, seamlessAppend);
            });
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
                    m_tabWidget->refreshEditorModeBar(ed);
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
        m_projectMgr->reconcileProjectStructure();
        statusBar()->showMessage("Saved: " + QDir::toNativeSeparators(savedPath), 4000);
    });

    resWin->show();
    resWin->raise();
    resWin->activateWindow();
}

void MainWindow::onRenameProjectFile(const QString& filePath) {
    if (filePath.isEmpty() || !m_projectMgr->hasOpenProject()) return;
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return;

    bool ok = false;
    QString newName = QInputDialog::getText(
                          this,
                          "Rename File",
                          "Enter new file name:",
                          QLineEdit::Normal,
                          fi.fileName(),
                          &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == fi.fileName()) return;

    QString newAbsPath = QDir(fi.absolutePath()).filePath(newName);
    if (QFileInfo::exists(newAbsPath)) {
        QMessageBox::warning(this, "Rename File", "A file with this name already exists.");
        return;
    }

    if (m_projectMgr->renameProjectFile(filePath, newAbsPath)) {
        statusBar()->showMessage(QString("Renamed '%1' to '%2' (summary preserved).")
                                     .arg(fi.fileName(), newName), 4000);
    } else {
        QMessageBox::warning(this, "Rename Failed", "Could not rename the file on disk.");
    }
}

void MainWindow::onDeleteProjectFile(const QString& filePath) {
    if (filePath.isEmpty() || !m_projectMgr->hasOpenProject()) return;
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return;

    const QString relPath = m_projectMgr->toRelativePath(filePath);
    auto ans = QMessageBox::question(
        this,
        "Delete File",
        QString("Are you sure you want to delete file '%1' from disk and remove its AI summary from the project?")
            .arg(relPath),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ans != QMessageBox::Yes) return;

    if (m_projectMgr->deleteProjectFile(filePath)) {
        statusBar()->showMessage("Deleted file and removed its AI summary: " + relPath, 4000);
    } else {
        QMessageBox::warning(this, "Delete Failed", "Could not delete file from disk:\n" + filePath);
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
        menu.addSeparator();
        menu.addAction("Generate / Regenerate File Summary", this, [this, clickedPath]() {
            rebuildSingleFileSummary(clickedPath);
        });
        menu.addAction("Continue File Summary", this, [this, clickedPath]() {
            continueSingleFileSummary(clickedPath);
        });
        menu.addAction("Clear File Summary (Mark Empty)", this, [this, clickedPath]() {
            clearSingleFileSummary(clickedPath);
        });
        menu.addAction("Remove File Summary", this, [this, clickedPath]() {
            removeSingleFileSummary(clickedPath);
        });
        menu.addSeparator();
        menu.addAction("Rename File...", this, [this, clickedPath]() {
            onRenameProjectFile(clickedPath);
        });
        menu.addAction("Delete File...", this, [this, clickedPath]() {
            onDeleteProjectFile(clickedPath);
        });
        menu.addSeparator();
    }

    if (isDir && QDir::cleanPath(clickedPath).compare(QDir::cleanPath(m_projectMgr->projectRootPath()), Qt::CaseInsensitive) != 0) {
        const QString folderName = QFileInfo(clickedPath).fileName();
        const QString subRel = m_projectMgr->toRelativePath(clickedPath);
        const bool hasStandalone = m_projectMgr->hasStandaloneAideFile(clickedPath);
        const bool isIndexedSub = m_projectMgr->isSubprojectDir(subRel);

        if (isIndexedSub) {
            menu.addAction(QString("Inspect Subproject '%1' in AI Inspector").arg(folderName), this, [this, subRel]() {
                m_inspectorDock->inspectProjectScope(subRel);
            });
        }
        menu.addAction(QString("Rebuild Subproject '%1' Summary").arg(folderName), this, [this, subRel]() {
            onRebuildProjectSummary(subRel);
            m_inspectorDock->inspectProjectScope(subRel);
        });
        menu.addAction(QString("Generate File Summaries in '%1' (Recursive)...").arg(folderName),
                       this, [this, clickedPath]() { onRecursiveProjectSummary(clickedPath); });

        menu.addSeparator();
        if (!hasStandalone) {
            menu.addAction(QString("Create Standalone .aide File in '%1'").arg(folderName), this, [this, clickedPath, subRel]() {
                if (m_projectMgr->createSubproject(clickedPath)) {
                    m_inspectorDock->inspectProjectScope(subRel);
                    m_projectTree->viewport()->update();
                    statusBar()->showMessage(
                        QString("Created standalone .aide file in '%1' and migrated its summaries.").arg(subRel), 4000);
                }
            });
        } else {
            menu.addAction(QString("Merge '%1' .aide into Parent Project (Keep Virtual Summary)").arg(folderName), this, [this, clickedPath, subRel]() {
                auto ans = QMessageBox::question(
                    this,
                    "Merge Subproject (.aide) into Parent",
                    QString("Remove standalone .aide file from folder '%1' and keep its summaries as a Virtual Subproject inside the parent .aide file?\n\n"
                            "• The folder and all source files on disk will NOT be touched.\n"
                            "• The subproject summary, system prompt, and all file summaries will be preserved in the parent .aide file.")
                        .arg(subRel),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (ans == QMessageBox::Yes && m_projectMgr->removeSubproject(clickedPath)) {
                    m_inspectorDock->inspectProjectScope(subRel);
                    m_projectTree->viewport()->update();
                    statusBar()->showMessage(
                        QString("Merged .aide from '%1' into parent project (virtual subproject preserved).").arg(subRel), 4000);
                }
            });
        }
        menu.addSeparator();
    } else if (isDir) {
        menu.addAction("Inspect Root Project Summary in AI Inspector", this, [this]() {
            m_inspectorDock->inspectProjectScope({});
        });
        menu.addAction("Rebuild Root Project Summary", this, [this]() {
            m_inspectorDock->inspectProjectScope({});
            onRebuildProjectSummary({});
        });
        menu.addSeparator();
    }

    menu.addAction("Custom Prompt for Project...", this, [this]() {
        onProjectCustomPrompt();
    });
    menu.addAction("Generate Summaries for Entire Project (Recursive)...", this, [this]() {
        onRecursiveProjectSummary(m_projectMgr->projectRootPath());
    });
    menu.addAction("Export Project Summaries as Wiki (Markdown)...", this, &MainWindow::onExportProjectWiki);
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
    updateAiActionsState();
}

void MainWindow::updateAiActionsState() {
    const bool hasProj = (m_projectMgr && m_projectMgr->hasOpenProject());
    CodeEditor* ed = m_tabWidget ? m_tabWidget->currentEditor() : nullptr;
    const bool hasDoc = (ed != nullptr) && !ed->isAiBusy();
    const bool hasSavedFileDoc = (ed != nullptr) && !ed->filePath().isEmpty();
    const bool hasCanContinue = hasDoc && ed->hasLastAiContext();

    if (m_actFileAiContinueGen)   m_actFileAiContinueGen->setEnabled(hasCanContinue);
    if (m_actFileAiFollowUp)      m_actFileAiFollowUp->setEnabled(hasCanContinue);
    if (m_actFileSummaryRemove)   m_actFileSummaryRemove->setEnabled(hasSavedFileDoc);

    if (m_actProjCustomPrompt)     m_actProjCustomPrompt->setEnabled(hasProj);
    if (m_actProjSummaryRebuild)   m_actProjSummaryRebuild->setEnabled(hasProj);
    if (m_actProjRecursiveSummary) m_actProjRecursiveSummary->setEnabled(hasProj);
    if (m_actProjExportWiki)       m_actProjExportWiki->setEnabled(hasProj);

    if (m_actFileCustomPrompt)     m_actFileCustomPrompt->setEnabled(hasDoc);
    if (m_actFileSummaryRebuild)   m_actFileSummaryRebuild->setEnabled(hasSavedFileDoc);
    if (m_actCodeExplain)          m_actCodeExplain->setEnabled(hasDoc);
    if (m_actAddDocs)              m_actAddDocs->setEnabled(hasDoc);

    for (QAction* act : m_otherFileAiActions) {
        if (act) act->setEnabled(hasDoc);
    }
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

    QProgressDialog progress(
        QString("Scanning project directory:\n%1").arg(QDir::toNativeSeparators(dirPath)),
        "Cancel",
        0,
        0,
        this);
    progress.setWindowTitle("Opening Project");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(250);
    progress.setMinimumWidth(420);

    auto progressCb = [&](int processedDirs, int foundFiles, const QString& currentRelDir) -> bool {
        progress.setLabelText(
            QString("Opening project...\nScanned %1 folder(s), %2 file(s)\n%3")
                .arg(processedDirs)
                .arg(foundFiles)
                .arg(currentRelDir));
        QApplication::processEvents();
        return !progress.wasCanceled();
    };

    const bool opened = m_projectMgr->openProjectFolder(dirPath, progressCb);
    progress.close();

    if (!opened) {
        updateCentralViewState();
        statusBar()->showMessage("Opening project cancelled.", 4000);
        return;
    }

    QString nativePath = QDir::toNativeSeparators(m_projectMgr->projectRootPath());
    m_projectPathLabel->setText(nativePath);
    m_projectPathLabel->setToolTip(nativePath);
    m_tabWidget->setProjectRootPath(m_projectMgr->projectRootPath());
    m_fsModel->setRootPath(dirPath);
    m_projectTree->setRootIndex(m_fsModel->index(dirPath));
    m_projectSearchEdit->clear();
    m_inspectorDock->setSelectedProjectScope({});
    m_inspectorDock->refreshDisplay();
    ConfigManager::instance().addRecentProject(dirPath);
    updateCentralViewState();
    statusBar()->showMessage("Opened project: " + dirPath, 4000);
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
    m_tabWidget->setProjectRootPath({});
    m_projectPathLabel->setText("-");
    m_projectPathLabel->setToolTip({});
    m_projectSearchEdit->clear();
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
                if (!ed->filePath().isEmpty()) {
                    ed->setSyntaxLanguage(ConfigManager::instance().languageIdForFile(ed->filePath()));
                }
                m_tabWidget->refreshEditorModeBar(ed);
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
    auto rules = ConfigManager::instance().fileTypeRules();
    std::sort(rules.begin(), rules.end(), [](const FileTypeRule& a, const FileTypeRule& b) {
        int cmp = a.displayName.compare(b.displayName, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        return a.langId.compare(b.langId, Qt::CaseInsensitive) < 0;
    });

    for (const auto& r : rules) {
        QAction* act = m_langMenu->addAction(QString("%1 (%2)").arg(r.displayName, r.langId));
        if (auto* e = m_tabWidget->currentEditor()) {
            act->setCheckable(true);
            act->setChecked(e->syntaxLanguage() == r.langId);
        }
        connect(act, &QAction::triggered, this, [this, lang = r.langId]() {
            if (auto* e = m_tabWidget->currentEditor()) {
                e->setSyntaxLanguage(lang);
                m_tabWidget->refreshEditorModeBar(e);
                m_langStatusLabel->setText("Lang: " + lang);
            }
        });
    }
}

void MainWindow::onActiveEditorChanged(CodeEditor* editor) {
    updateAiActionsState();
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

void MainWindow::onEditorAiContinue(CodeEditor* editor, const QString& followUpInstruction, bool seamlessAppend) {
    if (!editor || !editor->hasLastAiContext()) return;

    const auto& cfg = ConfigManager::instance().aiConfig();
    QString customSuffix = ConfigManager::instance().promptTemplate("CustomPrompt");

    QString userPrompt;
    if (seamlessAppend) {
        userPrompt = QStringLiteral(
            "Continue outputting ONLY the remaining raw code from the exact point where you stopped, "
            "without repeating any already generated lines and without markdown fences:");
    } else {
        QTextCursor c = editor->textCursor();
        QString currentCode = c.hasSelection() ? c.selectedText().replace(QChar(0x2029), '\n') : editor->toPlainText();
        userPrompt = followUpInstruction
                     + (customSuffix.isEmpty() ? QString() : ("\n" + customSuffix))
                     + "\n\nCurrent code state:\n" + currentCode;
    }

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = (editor->lastAiAction() == AiEditAction::ExplainCode)
                          ? TaskTargetType::ExplainToDialog
                          : TaskTargetType::ModifyEditorCode;
    task.targetEditor = editor;
    task.filePath = editor->filePath();
    task.llmModel = cfg.llmIdentifier();
    task.maxOutputTokens = cfg.maxOutputCodeTokens;
    task.contextWindow = cfg.contextWindow;
    task.systemPrompt = editor->lastAiSystemPrompt();
    task.conversationHistory = editor->lastAiHistory();
    task.userPrompt = userPrompt;
    task.isContinuation = true;
    task.appendToPreviousOutput = seamlessAppend;
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
    if (task.targetEditor) {
        QVector<AiChatMessage> updatedHistory = task.conversationHistory;
        updatedHistory.append({"user", task.userPrompt});
        updatedHistory.append({"assistant", resultText});
        task.targetEditor->setLastAiConversationContext(
            task.systemPrompt,
            updatedHistory,
            task.targetType == TaskTargetType::ExplainToDialog ? AiEditAction::ExplainCode : AiEditAction::CustomPrompt);
        updateAiActionsState();
    }

    if (task.targetType == TaskTargetType::ProjectCustomPrompt) {
        if (auto win = m_projectPromptWindows.take(task.taskId)) {
            win->finalizeResponse(resultText);
        }
        return;
    }

    if (task.targetType != TaskTargetType::ExplainToDialog) return;

    auto* resWin = new ProjectPromptResultDialog(
        "Explain Code: " + (task.filePath.isEmpty() ? QStringLiteral("Selection") : QFileInfo(task.filePath).fileName()),
        m_projectMgr->projectRootPath(),
        task.taskId,
        task.systemPrompt,
        task.userPrompt,
        this);
    resWin->setWindowTitle("AI Code Explanation");
    resWin->finalizeResponse(resultText);

    connect(resWin, &ProjectPromptResultDialog::cancelTaskRequested, m_taskQueue, &AiTaskQueue::cancelTaskById);
    connect(resWin, &ProjectPromptResultDialog::continueRequestTriggered, this,
            [this](ProjectPromptResultDialog* dlgPtr,
                   const QString& sysP,
                   const QVector<AiChatMessage>& history,
                   const QString& followUpUserPrompt,
                   bool seamlessAppend) {
                if (!dlgPtr) return;
                const auto& aiCfg = ConfigManager::instance().aiConfig();
                AiTask contTask;
                contTask.priority = TaskPriority::High_UserAction;
                contTask.targetType = TaskTargetType::ProjectCustomPrompt;
                contTask.filePath = "[Explain Code — Continue]";
                contTask.llmModel = aiCfg.llmIdentifier();
                contTask.maxOutputTokens = aiCfg.maxOutputCodeTokens;
                contTask.contextWindow = aiCfg.contextWindow;
                contTask.systemPrompt = sysP;
                contTask.conversationHistory = history;
                contTask.userPrompt = followUpUserPrompt;
                contTask.isContinuation = true;
                contTask.appendToPreviousOutput = seamlessAppend;
                contTask.stream = true;

                QString newId = m_taskQueue->enqueueTask(contTask);
                m_projectPromptWindows.insert(newId, dlgPtr);
                dlgPtr->beginFollowUpStream(newId, followUpUserPrompt, seamlessAppend);
            });

    resWin->show();
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
    const AiConnectionConfig prevCfg = ConfigManager::instance().aiConfig();
    AiPreferencesDialog dlg(m_aiProvider, this);
    if (dlg.exec() == QDialog::Accepted) {
        auto& cfgMgr = ConfigManager::instance();
        cfgMgr.setAiProfiles(dlg.resultProfiles(), dlg.resultActiveProfileName());
        cfgMgr.setAiConfig(dlg.resultConfig());
        cfgMgr.setAiBehavior(dlg.resultBehavior());
        cfgMgr.setPromptTemplates(dlg.resultPrompts());

        refreshProfileSelectors();

        if (cfgMgr.aiBehavior().autoStartLocalExecutable) {
            LocalLlmProcessManager::instance().syncWithProfile(cfgMgr.aiConfig(), true);
        }

        // Если изменился профиль, URL, ключ, бэкенд или мы ещё не подключены — запускаем Test/Fetch
        const AiConnectionConfig newCfg = cfgMgr.aiConfig();
        if (m_llmConnState != LlmConnectionState::Connected ||
            prevCfg.profileName != newCfg.profileName ||
            prevCfg.backend != newCfg.backend ||
            prevCfg.endpointUrl != newCfg.endpointUrl ||
            prevCfg.apiKey != newCfg.apiKey ||
            prevCfg.localExecutablePath != newCfg.localExecutablePath ||
            prevCfg.localCommandLineArgs != newCfg.localCommandLineArgs) {
            startLlmConnectionCheck(true);
        }

        m_projectMgr->saveProjectFile();
        m_inspectorDock->refreshDisplay();
        m_projectTree->viewport()->update();
        if (cfgMgr.aiBehavior().autoSummaryMode != AutoSummaryMode::None) {
            for (int i = 0; i < m_tabWidget->count(); ++i) {
                if (auto* ed = m_tabWidget->editorAt(i); ed && !ed->filePath().isEmpty()) {
                    m_summaryWorker->checkAndScheduleFileSummary(ed->filePath(), ed->toPlainText().toUtf8());
                }
            }
        }
    } else {
        const auto& cfgMgr = ConfigManager::instance();
        if (cfgMgr.aiBehavior().autoStartLocalExecutable) {
            LocalLlmProcessManager::instance().syncWithProfile(cfgMgr.aiConfig(), true);
        }
        if (m_llmConnState != LlmConnectionState::Connected) {
            startLlmConnectionCheck(true);
        }
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

void MainWindow::removeSingleFileSummary(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QString relPath = m_projectMgr->toRelativePath(filePath);
    m_taskQueue->cancelFileSummaryTask(relPath);
    m_inspectorDock->resetLiveSummary(relPath, false);
    m_projectMgr->removeFileSummary(filePath);
    statusBar()->showMessage("File summary removed: " + relPath, 3000);
}

void MainWindow::continueSingleFileSummary(const QString& filePath) {
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
    m_summaryWorker->continueFileSummary(filePath, content);
    statusBar()->showMessage("Continuing file summary generation...", 3000);
}
