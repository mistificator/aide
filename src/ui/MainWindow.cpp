// src/ui/MainWindow.cpp
#include "MainWindow.h"
#include "ContextInspectorDock.h"
#include "AiPreferencesDialog.h"
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
#include <QLabel>
#include <QDir>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QApplication>

// Кастомная модель дерева файлов с цветовой и текстовой индикацией статуса ИИ-саммари
class ProjectTreeModel : public QFileSystemModel {
public:
    ProjectTreeModel(ProjectManager* pm, QObject* parent = nullptr) : QFileSystemModel(parent), m_pm(pm) {}
    QVariant data(const QModelIndex& index, int role) const override {
        if (index.column() == 0 && !isDir(index)) {
            auto fc = m_pm->fileContext(filePath(index));
            if (role == Qt::ForegroundRole) {
                if (fc.isSummaryPending) return QColor("#ffb86c");
                if (!fc.aiSummary.isEmpty()) return QColor("#50fa7b");
            } else if (role == Qt::ToolTipRole) {
                if (fc.isSummaryPending) return "AI Summary: Indexing in background...";
                if (!fc.aiSummary.isEmpty()) return "AI Summary Ready:\n" + fc.aiSummary.left(300);
                return "AI Summary: Not indexed yet (open file to index)";
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
    resize(1400, 860);

    setupUiAndDocks();
    setupMenus();
    setupConnections();

    m_downloader->ensureResourcesPresent();
    m_tabWidget->newDocument();
}

void MainWindow::setupUiAndDocks() {
    m_tabWidget = new EditorTabWidget(this);
    setCentralWidget(m_tabWidget);

    m_projectDock = new QDockWidget("Project Explorer", this);
    m_projectTree = new QTreeView(m_projectDock);
    m_fsModel = new ProjectTreeModel(m_projectMgr, this);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_projectTree->setModel(m_fsModel);
    for (int col = 1; col < 4; ++col) m_projectTree->hideColumn(col);
    m_projectTree->header()->hide();
    m_projectDock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);

    m_inspectorDock = new ContextInspectorDock(m_projectMgr, this);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    m_queueStatusLabel = new QLabel("AI: Idle", this);
    m_langStatusLabel = new QLabel("Lang: cpp", this);
    statusBar()->addPermanentWidget(m_langStatusLabel);
    statusBar()->addPermanentWidget(m_queueStatusLabel);
}

void MainWindow::setupMenus() {
    // 1. FILE
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("New File", QKeySequence::New, m_tabWidget, &EditorTabWidget::newDocument);
    fileMenu->addAction("Open File...", QKeySequence::Open, this, &MainWindow::onOpenFile);
    fileMenu->addAction("Open Folder / Project...", QKeySequence("Ctrl+Shift+O"), this, &MainWindow::onOpenFolder);
    fileMenu->addSeparator();
    fileMenu->addAction("Save", QKeySequence::Save, m_tabWidget, &EditorTabWidget::saveCurrentDocument);
    fileMenu->addAction("Save As...", QKeySequence::SaveAs, m_tabWidget, &EditorTabWidget::saveCurrentDocumentAs);
    fileMenu->addAction("Save All", QKeySequence("Ctrl+Shift+S"), m_tabWidget, &EditorTabWidget::saveAllDocuments);
    fileMenu->addAction("Save Project (.aide)", this, [this]() {
        if (m_projectMgr->saveProjectFile()) statusBar()->showMessage("Project .aide file saved.", 3000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction("Close Document", QKeySequence::Close, this, [this]() {
        if (m_tabWidget->currentIndex() != -1) m_tabWidget->closeDocument(m_tabWidget->currentIndex());
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

    // 3. VIEW
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_projectDock->toggleViewAction());
    viewMenu->addAction(m_inspectorDock->toggleViewAction());
    viewMenu->addSeparator();
    m_themeMenu = viewMenu->addMenu("Color Scheme");
    connect(m_themeMenu, &QMenu::aboutToShow, this, &MainWindow::populateThemeMenu);

    QMenu* langMenu = viewMenu->addMenu("Syntax Highlighting");
    for (const QString& lang : {"cpp", "c", "python", "bash", "sql", "javascript"}) {
        langMenu->addAction(lang.toUpper(), this, [this, lang]() {
            if (auto* e = m_tabWidget->currentEditor()) {
                e->setSyntaxLanguage(lang);
                m_langStatusLabel->setText("Lang: " + lang);
            }
        });
    }
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
    aiMenu->addAction("Rebuild Project Summary", m_summaryWorker, &SummaryWorker::forceRebuildProjectSummary);
    aiMenu->addAction("Download / Update Syntax && Themes", this, [this]() {
        statusBar()->showMessage("Downloading syntax & theme files...", 3000);
        m_downloader->forceDownloadAll();
    });
    aiMenu->addSeparator();
    aiMenu->addAction("Preferences...", this, &MainWindow::onAiPreferences);

    // 5. WINDOW
    m_windowMenu = menuBar()->addMenu("&Window");
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::populateWindowMenu);

    // 6. HELP
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About AIDE", this, [this]() {
        QMessageBox::about(this, "About AIDE",
            "<b>AIDE (AI Development Environment)</b><br>"
            "Qt6 Multi-Document Code Editor with Streaming LLM Integration, "
            "Two-Tier Priority Queue, and Automatic Hierarchical Project Context (.aide).");
    });
    helpMenu->addAction("About Qt", qApp, &QApplication::aboutQt);
}

void MainWindow::setupConnections() {
    connect(m_projectTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!m_fsModel->isDir(idx)) m_tabWidget->openFile(m_fsModel->filePath(idx));
    });

    connect(m_tabWidget, &EditorTabWidget::activeEditorChanged, this, &MainWindow::onActiveEditorChanged);
    connect(m_tabWidget, &EditorTabWidget::documentOpened, m_summaryWorker, &SummaryWorker::checkAndScheduleFileSummary);
    connect(m_tabWidget, &EditorTabWidget::documentSaved, m_summaryWorker, &SummaryWorker::checkAndScheduleFileSummary);
    connect(m_tabWidget, &EditorTabWidget::aiActionTriggered, this, &MainWindow::onEditorAiAction);
    connect(m_tabWidget, &EditorTabWidget::cancelAiTriggered, m_taskQueue, &AiTaskQueue::cancelTaskForEditor);

    connect(m_inspectorDock, &ContextInspectorDock::rebuildFileSummaryClicked, this, [this](const QString& path) {
        if (auto* ed = m_tabWidget->findEditorByPath(path))
            m_summaryWorker->forceRebuildFileSummary(path, ed->toPlainText().toUtf8());
    });
    connect(m_inspectorDock, &ContextInspectorDock::rebuildProjectSummaryClicked, m_summaryWorker, &SummaryWorker::forceRebuildProjectSummary);

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString&) {
        m_projectTree->viewport()->update();
    });

    connect(m_taskQueue, &AiTaskQueue::queueStatusChanged, this, [this](int high, int low, bool busy) {
        if (!busy && high == 0 && low == 0) m_queueStatusLabel->setText("AI: Idle");
        else m_queueStatusLabel->setText(QString("AI: Busy (User Queue: %1 | Background: %2)").arg(high).arg(low));
    });

    connect(m_taskQueue, &AiTaskQueue::taskCompleted, this, &MainWindow::onTaskCompleted);
    connect(m_taskQueue, &AiTaskQueue::taskFailed, this, &MainWindow::onTaskFailed);

    connect(m_downloader, &ThemeAndSyntaxDownloader::allDownloadsFinished, this, [this]() {
        statusBar()->showMessage("Syntax definitions and color schemes are up to date.", 4000);
        if (auto* e = m_tabWidget->currentEditor()) e->setSyntaxLanguage(e->syntaxLanguage());
    });
}

void MainWindow::onOpenFile() {
    QString path = QFileDialog::getOpenFileName(this, "Open File", m_projectMgr->projectRootPath());
    if (!path.isEmpty()) m_tabWidget->openFile(path);
}

void MainWindow::onOpenFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Project Folder", QDir::homePath());
    if (dir.isEmpty()) return;
    if (m_projectMgr->openProjectFolder(dir)) {
        m_fsModel->setRootPath(dir);
        m_projectTree->setRootIndex(m_fsModel->index(dir));
        m_inspectorDock->refreshDisplay();
        statusBar()->showMessage("Opened project: " + dir, 4000);
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
    QString instruction = (action == AiEditAction::CustomPrompt)
        ? (customInstruction + "\nIf modifying code, output ONLY raw code without markdown fences:")
        : ConfigManager::instance().promptTemplate(actionKeyForEnum(action));

    AiTask task;
    task.priority = TaskPriority::High_UserAction;
    task.targetType = (action == AiEditAction::ExplainCode) ? TaskTargetType::ExplainToDialog : TaskTargetType::ModifyEditorCode;
    task.targetEditor = editor;
    task.filePath = editor->filePath();
    task.systemPrompt = sysCtx;
    task.userPrompt = instruction + "\n\n" + selectedText;
    task.stream = (task.targetType == TaskTargetType::ModifyEditorCode);

    m_taskQueue->enqueueTask(task);
}

void MainWindow::onTaskCompleted(const AiTask& task, const QString& resultText) {
    if (task.targetType != TaskTargetType::ExplainToDialog) return;
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("AI Code Explanation");
    dlg->resize(680, 500);
    auto* lay = new QVBoxLayout(dlg);
    auto* view = new QPlainTextEdit(resultText, dlg);
    view->setReadOnly(true);
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
    if (dlg.exec() == QDialog::Accepted)
        ConfigManager::instance().setAiConfig(dlg.resultConfig());
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
    m_windowMenu->addAction("Close All Documents", m_tabWidget, &EditorTabWidget::closeAllDocuments);
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

void MainWindow::closeEvent(QCloseEvent* event) {
    m_projectMgr->saveProjectFile();
    if (m_tabWidget->closeAllDocuments()) event->accept();
    else event->ignore();
}