// src/editor/EditorTabWidget.cpp
#include "EditorTabWidget.h"
#include "../core/ConfigManager.h"
#include "../ui/MarkdownRenderer.h"
#include "../ui/ProjectPromptResultDialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QTabBar>
#include <QMenu>
#include <QProcess>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextEdit>
#include <QScrollBar>

namespace {
class EditorTabPage : public QWidget {
public:
    explicit EditorTabPage(CodeEditor* ed, QWidget* parent = nullptr)
        : QWidget(parent), editor(ed) {}

    CodeEditor* editor = nullptr;
    QWidget* modeBar = nullptr;
    QPushButton* rawViewBtn = nullptr;
    QPushButton* renderedViewBtn = nullptr;
    QPushButton* extendedViewBtn = nullptr;
    QStackedWidget* stack = nullptr;
    QTextEdit* renderedViewer = nullptr;
    ProjectPromptResultDialog* mdViewer = nullptr;
    MarkdownTabViewMode viewMode = MarkdownTabViewMode::RawMarkdown;
};
} // namespace

EditorTabWidget::EditorTabWidget(QWidget* parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QTabBar::customContextMenuRequested, this, &EditorTabWidget::onTabBarContextMenu);
    connect(this, &QTabWidget::tabCloseRequested, this, &EditorTabWidget::closeDocument);
    connect(this, &QTabWidget::currentChanged, this, [this](int) {
        if (auto* ed = currentEditor()) {
            refreshEditorModeBar(ed);
        }
        emit activeEditorChanged(currentEditor());
    });
}

bool EditorTabWidget::isMarkdownEditor(const CodeEditor* ed) {
    if (!ed) return false;
    const QString lang = ed->syntaxLanguage().trimmed().toLower();
    if (lang == "markdown" || lang == "md") {
        return true;
    }
    const QString ext = QFileInfo(ed->filePath()).suffix().toLower();
    return (ext == "md" || ext == "markdown" || ext == "mdown" || ext == "mkd");
}

bool EditorTabWidget::isCurrentTabInRawEditMode() const {
    int idx = currentIndex();
    if (idx < 0) return false;
    auto* page = static_cast<EditorTabPage*>(widget(idx));
    if (!page || !page->editor) return false;
    return page->viewMode == MarkdownTabViewMode::RawMarkdown;
}

QWidget* EditorTabWidget::createTabPageForEditor(CodeEditor* ed) {
    auto* page = new EditorTabPage(ed, this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Верхняя панель переключения 3 режимов просмотра для Markdown-документов
    page->modeBar = new QWidget(page);
    page->modeBar->setStyleSheet(
        "QWidget { background-color: #191a22; border-bottom: 1px solid #44475a; }");
    auto* barLay = new QHBoxLayout(page->modeBar);
    barLay->setContentsMargins(10, 4, 10, 4);
    barLay->setSpacing(6);

    auto* titleLbl = new QLabel("Markdown View:", page->modeBar);
    titleLbl->setStyleSheet("color: #888899; font-size: 11px; font-weight: bold; border: none; background: transparent;");

    const QString btnStyle =
        "QPushButton { background-color: #282a36; color: #f8f8f2; border: 1px solid #44475a; "
        "border-radius: 4px; padding: 3px 10px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: #343746; }"
        "QPushButton:checked { background-color: #bd93f9; color: #191a24; border-color: #bd93f9; }";

    page->rawViewBtn = new QPushButton("View Raw Markdown", page->modeBar);
    page->rawViewBtn->setCheckable(true);
    page->rawViewBtn->setChecked(true);
    page->rawViewBtn->setToolTip("Edit raw Markdown source text (editing is enabled only in Raw mode)");
    page->rawViewBtn->setStyleSheet(btnStyle);

    page->renderedViewBtn = new QPushButton("View Rendered Markdown", page->modeBar);
    page->renderedViewBtn->setCheckable(true);
    page->renderedViewBtn->setChecked(false);
    page->renderedViewBtn->setToolTip("Read-only formatted Markdown document view");
    page->renderedViewBtn->setStyleSheet(btnStyle);

    page->extendedViewBtn = new QPushButton("Extended Markdown Code View", page->modeBar);
    page->extendedViewBtn->setCheckable(true);
    page->extendedViewBtn->setChecked(false);
    page->extendedViewBtn->setToolTip("Read-only formatted Markdown with extracted files & code blocks inspector");
    page->extendedViewBtn->setStyleSheet(btnStyle);

    barLay->addWidget(titleLbl);
    barLay->addWidget(page->rawViewBtn);
    barLay->addWidget(page->renderedViewBtn);
    barLay->addWidget(page->extendedViewBtn);
    barLay->addStretch();

    page->stack = new QStackedWidget(page);
    ed->setParent(page->stack);
    page->stack->addWidget(ed);

    lay->addWidget(page->modeBar);
    lay->addWidget(page->stack, 1);

    page->modeBar->setVisible(isMarkdownEditor(ed));

    connect(page->rawViewBtn, &QPushButton::clicked, this, [this, page]() {
        setTabMarkdownViewMode(page, MarkdownTabViewMode::RawMarkdown);
    });
    connect(page->renderedViewBtn, &QPushButton::clicked, this, [this, page]() {
        setTabMarkdownViewMode(page, MarkdownTabViewMode::RenderedMarkdown);
    });
    connect(page->extendedViewBtn, &QPushButton::clicked, this, [this, page]() {
        setTabMarkdownViewMode(page, MarkdownTabViewMode::ExtendedCodeView);
    });

    connect(ed, &QPlainTextEdit::textChanged, this, [this, page]() {
        if (page->viewMode == MarkdownTabViewMode::RenderedMarkdown && page->renderedViewer) {
            int oldScroll = page->renderedViewer->verticalScrollBar()->value();
            page->renderedViewer->setHtml(MarkdownRenderer::toHtml(page->editor->toPlainText()));
            page->renderedViewer->verticalScrollBar()->setValue(oldScroll);
        } else if (page->viewMode == MarkdownTabViewMode::ExtendedCodeView && page->mdViewer) {
            page->mdViewer->setDocumentContent(page->editor->toPlainText(), m_projectRootPath);
        }
    });

    return page;
}

void EditorTabWidget::setTabMarkdownViewMode(QWidget* rawPage, MarkdownTabViewMode mode) {
    auto* page = static_cast<EditorTabPage*>(rawPage);
    if (!page || !page->editor) return;

    if (mode != MarkdownTabViewMode::RawMarkdown && !isMarkdownEditor(page->editor)) {
        mode = MarkdownTabViewMode::RawMarkdown;
    }

    page->viewMode = mode;
    page->rawViewBtn->setChecked(mode == MarkdownTabViewMode::RawMarkdown);
    page->renderedViewBtn->setChecked(mode == MarkdownTabViewMode::RenderedMarkdown);
    page->extendedViewBtn->setChecked(mode == MarkdownTabViewMode::ExtendedCodeView);

    if (mode == MarkdownTabViewMode::RawMarkdown) {
        page->stack->setCurrentWidget(page->editor);
        page->editor->setFocus();
        emit activeEditorChanged(page->editor);
        return;
    }

    if (mode == MarkdownTabViewMode::RenderedMarkdown) {
        if (!page->renderedViewer) {
            page->renderedViewer = new QTextEdit(page->stack);
            page->renderedViewer->setReadOnly(true);
            MarkdownRenderer::configureViewer(page->renderedViewer);
            page->stack->addWidget(page->renderedViewer);
        }
        int oldScroll = page->renderedViewer->verticalScrollBar()->value();
        page->renderedViewer->setHtml(MarkdownRenderer::toHtml(page->editor->toPlainText()));
        page->renderedViewer->verticalScrollBar()->setValue(oldScroll);
        page->stack->setCurrentWidget(page->renderedViewer);
        page->renderedViewer->setFocus();
        emit activeEditorChanged(page->editor);
        return;
    }

    // ExtendedTabViewMode::ExtendedCodeView — отображается прямо внутри page->stack без отдельного окна
    if (!page->mdViewer) {
        page->mdViewer = new ProjectPromptResultDialog(m_projectRootPath, page->stack);
        page->stack->addWidget(page->mdViewer);

        connect(page->mdViewer, &ProjectPromptResultDialog::openContentInEditorRequested, this,
                [this](const QString& suggestedPath, const QString& code, const QString& langId) {
                    CodeEditor* newEd = newDocument();
                    if (newEd) {
                        newEd->setPlainText(code);
                        if (!langId.isEmpty()) {
                            newEd->setSyntaxLanguage(langId);
                        } else if (!suggestedPath.isEmpty()) {
                            newEd->setSyntaxLanguage(ConfigManager::instance().languageIdForFile(suggestedPath));
                        }
                        refreshEditorModeBar(newEd);
                        emit activeEditorChanged(newEd);
                        newEd->setFocus();
                    }
                });

        connect(page->mdViewer, &ProjectPromptResultDialog::replaceFileInEditorRequested, this,
                [this](const QString& existingFilePath, const QString& newCode, const QString& langId) {
                    if (existingFilePath.isEmpty()) return;
                    if (auto* targetEd = openFile(existingFilePath)) {
                        targetEd->replaceContentUnsaved(newCode);
                        if (!langId.isEmpty()) {
                            targetEd->setSyntaxLanguage(langId);
                        }
                        refreshEditorModeBar(targetEd);
                        emit activeEditorChanged(targetEd);
                        targetEd->setFocus();
                    }
                });

        connect(page->mdViewer, &ProjectPromptResultDialog::fileSavedToDisk, this,
                [this](const QString& savedPath) {
                    if (auto* openEd = findEditorByPath(savedPath)) {
                        openEd->loadFile(savedPath);
                        refreshEditorModeBar(openEd);
                    }
                });
    }

    page->mdViewer->setDisplaySubMode(MarkdownDisplaySubMode::ExtendedCodeView);
    page->mdViewer->setDocumentContent(page->editor->toPlainText(), m_projectRootPath);
    page->stack->setCurrentWidget(page->mdViewer);
    page->mdViewer->setFocus();
    emit activeEditorChanged(page->editor);
}

void EditorTabWidget::setProjectRootPath(const QString& rootPath) {
    m_projectRootPath = rootPath.isEmpty() ? QString() : QDir::cleanPath(rootPath);
    for (int i = 0; i < count(); ++i) {
        auto* page = static_cast<EditorTabPage*>(widget(i));
        if (page && page->mdViewer && page->viewMode == MarkdownTabViewMode::ExtendedCodeView) {
            page->mdViewer->setDocumentContent(page->editor->toPlainText(), m_projectRootPath);
        }
    }
}

void EditorTabWidget::refreshEditorModeBar(CodeEditor* ed) {
    int idx = indexOfEditor(ed);
    if (idx < 0) return;
    auto* page = static_cast<EditorTabPage*>(widget(idx));
    if (!page) return;

    const bool isMd = isMarkdownEditor(ed);
    page->modeBar->setVisible(isMd);
    if (!isMd && page->viewMode != MarkdownTabViewMode::RawMarkdown) {
        setTabMarkdownViewMode(page, MarkdownTabViewMode::RawMarkdown);
    } else if (isMd) {
        if (page->viewMode == MarkdownTabViewMode::RenderedMarkdown && page->renderedViewer) {
            page->renderedViewer->setHtml(MarkdownRenderer::toHtml(ed->toPlainText()));
        } else if (page->viewMode == MarkdownTabViewMode::ExtendedCodeView && page->mdViewer) {
            page->mdViewer->setDocumentContent(ed->toPlainText(), m_projectRootPath);
        }
    }
}

void EditorTabWidget::toggleCurrentMarkdownViewMode() {
    int idx = currentIndex();
    if (idx < 0) return;
    auto* page = static_cast<EditorTabPage*>(widget(idx));
    if (!page || !page->editor || !isMarkdownEditor(page->editor)) return;

    MarkdownTabViewMode nextMode = MarkdownTabViewMode::RawMarkdown;
    if (page->viewMode == MarkdownTabViewMode::RawMarkdown) {
        nextMode = MarkdownTabViewMode::RenderedMarkdown;
    } else if (page->viewMode == MarkdownTabViewMode::RenderedMarkdown) {
        nextMode = MarkdownTabViewMode::ExtendedCodeView;
    } else {
        nextMode = MarkdownTabViewMode::RawMarkdown;
    }
    setTabMarkdownViewMode(page, nextMode);
}

CodeEditor* EditorTabWidget::currentEditor() const {
    return editorAt(currentIndex());
}

CodeEditor* EditorTabWidget::editorAt(int index) const {
    if (index < 0 || index >= count()) return nullptr;
    QWidget* w = widget(index);
    if (auto* directEd = qobject_cast<CodeEditor*>(w)) return directEd;
    auto* page = static_cast<EditorTabPage*>(w);
    return page ? page->editor : nullptr;
}

int EditorTabWidget::indexOfEditor(const CodeEditor* ed) const {
    if (!ed) return -1;
    for (int i = 0; i < count(); ++i) {
        if (editorAt(i) == ed) return i;
    }
    return -1;
}

CodeEditor* EditorTabWidget::findEditorByPath(const QString& filePath) const {
    QString clean = QFileInfo(filePath).absoluteFilePath();
    for (int i = 0; i < count(); ++i)
        if (auto* ed = editorAt(i); ed && ed->filePath() == clean) return ed;
    return nullptr;
}

void EditorTabWidget::openContainingFolder(const QString& fileOrDirPath) {
    if (fileOrDirPath.isEmpty()) return;
    QFileInfo fi(fileOrDirPath);
    QString dirPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
#if defined(Q_OS_WIN)
    if (fi.exists() && fi.isFile()) {
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(fi.absoluteFilePath())});
        return;
    }
#elif defined(Q_OS_MAC)
    if (fi.exists() && fi.isFile()) {
        QProcess::startDetached("open", {"-R", fi.absoluteFilePath()});
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void EditorTabWidget::openTerminalAtFolder(const QString& dirPath) {
    if (dirPath.isEmpty() || !QDir(dirPath).exists()) return;
#if defined(Q_OS_WIN)
    QString wt = QStandardPaths::findExecutable("wt");
    if (!wt.isEmpty()) {
        QProcess::startDetached(wt, {"-d", QDir::toNativeSeparators(dirPath)}, dirPath);
        return;
    }
    QProcess::startDetached("cmd.exe",
                            {"/C", "start", "", "/D", QDir::toNativeSeparators(dirPath), "cmd.exe"},
                            dirPath);
#elif defined(Q_OS_MAC)
    QProcess::startDetached("open", {"-a", "Terminal", dirPath});
#else
    const QStringList terminals = {
        "x-terminal-emulator", "konsole", "gnome-terminal", "xfce4-terminal",
        "alacritty", "kitty", "tilix", "mate-terminal", "lxterminal", "xterm"
    };
    for (const QString& term : terminals) {
        QString exe = QStandardPaths::findExecutable(term);
        if (!exe.isEmpty()) {
            QProcess::startDetached(exe, {}, dirPath);
            return;
        }
    }
#endif
}

void EditorTabWidget::onTabBarContextMenu(const QPoint& pos) {
    int idx = tabBar()->tabAt(pos);
    if (idx < 0) return;
    CodeEditor* ed = editorAt(idx);
    if (!ed) return;

    auto* page = static_cast<EditorTabPage*>(widget(idx));
    const bool isMd = isMarkdownEditor(ed);
    const bool hasFile = !ed->filePath().isEmpty();
    if (!isMd && !hasFile) return;

    QMenu menu(this);
    if (isMd && page) {
        QAction* rawAct = menu.addAction("View Raw Markdown", this, [this, page]() {
            setTabMarkdownViewMode(page, MarkdownTabViewMode::RawMarkdown);
        });
        rawAct->setCheckable(true);
        rawAct->setChecked(page->viewMode == MarkdownTabViewMode::RawMarkdown);

        QAction* rendAct = menu.addAction("View Rendered Markdown", this, [this, page]() {
            setTabMarkdownViewMode(page, MarkdownTabViewMode::RenderedMarkdown);
        });
        rendAct->setCheckable(true);
        rendAct->setChecked(page->viewMode == MarkdownTabViewMode::RenderedMarkdown);

        QAction* extAct = menu.addAction("Extended Markdown Code View", this, [this, page]() {
            setTabMarkdownViewMode(page, MarkdownTabViewMode::ExtendedCodeView);
        });
        extAct->setCheckable(true);
        extAct->setChecked(page->viewMode == MarkdownTabViewMode::ExtendedCodeView);

        if (hasFile) menu.addSeparator();
    }
    if (hasFile) {
        QString filePath = ed->filePath();
        QString folderPath = QFileInfo(filePath).absolutePath();
        menu.addAction("Open Folder in Explorer / File Manager", this, [filePath]() {
            openContainingFolder(filePath);
        });
        menu.addAction("Open Console Terminal Here", this, [folderPath]() {
            openTerminalAtFolder(folderPath);
        });
    }
    menu.exec(tabBar()->mapToGlobal(pos));
}

CodeEditor* EditorTabWidget::newDocument() {
    auto* ed = new CodeEditor(this);
    registerEditor(ed);
    QWidget* page = createTabPageForEditor(ed);
    setCurrentIndex(addTab(page, ed->displayName()));
    ed->setFocus();
    return ed;
}

CodeEditor* EditorTabWidget::openFile(const QString& filePath) {
    if (auto* existing = findEditorByPath(filePath)) {
        int idx = indexOfEditor(existing);
        if (idx >= 0) setCurrentIndex(idx);
        ConfigManager::instance().setLastDialogDir(filePath);
        return existing;
    }
    if (count() == 1) {
        auto* first = editorAt(0);
        if (first && first->filePath().isEmpty() && !first->document()->isModified() && first->toPlainText().isEmpty()) {
            if (first->loadFile(filePath)) {
                updateTabTitle(first);
                refreshEditorModeBar(first);
                ConfigManager::instance().setLastDialogDir(filePath);
                emit documentOpened(first->filePath(), first->toPlainText().toUtf8());
                emit activeEditorChanged(first);
                return first;
            }
        }
    }

    auto* ed = new CodeEditor(this);
    if (!ed->loadFile(filePath)) {
        delete ed;
        return nullptr;
    }
    registerEditor(ed);
    QWidget* page = createTabPageForEditor(ed);
    setCurrentIndex(addTab(page, ed->displayName()));
    updateTabTitle(ed);
    refreshEditorModeBar(ed);
    ConfigManager::instance().setLastDialogDir(filePath);
    emit documentOpened(ed->filePath(), ed->toPlainText().toUtf8());
    return ed;
}

void EditorTabWidget::registerEditor(CodeEditor* ed) {
    connect(ed->document(), &QTextDocument::modificationChanged, this, [this, ed](bool) {
        updateTabTitle(ed);
    });
    connect(ed, &CodeEditor::busyStateChanged, this, [this, ed](CodeEditor*, bool busy) {
        updateTabTitle(ed);
        int idx = indexOfEditor(ed);
        if (idx >= 0) {
            auto* page = static_cast<EditorTabPage*>(widget(idx));
            if (busy && page && page->viewMode != MarkdownTabViewMode::RawMarkdown) {
                setTabMarkdownViewMode(page, MarkdownTabViewMode::RawMarkdown);
            } else if (!busy) {
                refreshEditorModeBar(ed);
            }
        }
        if (ed == currentEditor()) {
            emit activeEditorChanged(ed);
        }
    });
    connect(ed, &CodeEditor::fileSaved, this, [this, ed](const QString& path, const QByteArray& content) {
        updateTabTitle(ed);
        refreshEditorModeBar(ed);
        emit documentSaved(path, content);
        if (ed == currentEditor()) {
            emit activeEditorChanged(ed);
        }
    });
    connect(ed, &CodeEditor::aiActionRequested, this, &EditorTabWidget::aiActionTriggered);
    connect(ed, &CodeEditor::cancelAiRequested, this, &EditorTabWidget::cancelAiTriggered);
    connect(ed, &CodeEditor::diffRequested, this, &EditorTabWidget::diffRequested);
    connect(ed, &CodeEditor::aiContinueRequested, this, &EditorTabWidget::aiContinueTriggered);
}

void EditorTabWidget::updateTabTitle(CodeEditor* ed) {
    int idx = indexOfEditor(ed);
    if (idx == -1) return;
    QString title = (ed->isAiBusy() ? "[AI] " : "") + ed->displayName() + (ed->document()->isModified() ? " *" : "");
    setTabText(idx, title);
    setTabToolTip(idx, ed->filePath().isEmpty() ? "Unsaved Document" : ed->filePath());
}

bool EditorTabWidget::saveCurrentDocument() {
    auto* ed = currentEditor();
    if (!ed) return false;
    return ed->filePath().isEmpty() ? saveCurrentDocumentAs() : ed->saveFile();
}

bool EditorTabWidget::saveCurrentDocumentAs() {
    auto* ed = currentEditor();
    if (!ed) return false;
    QString initialPath = ed->filePath().isEmpty()
                              ? ConfigManager::instance().resolveInitialDialogDir()
                              : ed->filePath();
    QString path = QFileDialog::getSaveFileName(this, "Save File As", initialPath);
    if (path.isEmpty()) return false;
    ConfigManager::instance().setLastDialogDir(path);
    bool ok = ed->saveFileAs(path);
    updateTabTitle(ed);
    refreshEditorModeBar(ed);
    if (ok) {
        emit activeEditorChanged(ed);
    }
    return ok;
}

bool EditorTabWidget::saveAllDocuments() {
    bool allOk = true;
    for (int i = 0; i < count(); ++i) {
        auto* ed = editorAt(i);
        if (ed && ed->document()->isModified()) {
            if (ed->filePath().isEmpty()) {
                setCurrentIndex(i);
                allOk &= saveCurrentDocumentAs();
            } else {
                allOk &= ed->saveFile();
            }
        }
    }
    return allOk;
}

bool EditorTabWidget::maybeSave(CodeEditor* ed) {
    if (!ed || !ed->document()->isModified()) return true;
    int idx = indexOfEditor(ed);
    if (idx >= 0) setCurrentIndex(idx);
    auto btn = QMessageBox::warning(
        this,
        "Unsaved Changes",
        QString("Document '%1' has unsaved changes.\nDo you want to save them?").arg(ed->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save) return saveCurrentDocument();
    return btn != QMessageBox::Cancel;
}

bool EditorTabWidget::closeDocument(int index) {
    auto* ed = editorAt(index);
    if (!ed) return false;
    if (ed->isAiBusy()) {
        auto res = QMessageBox::question(
            this,
            "AI Operation in Progress",
            "An AI operation is currently running on this document. Cancel it and close?",
            QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::No) return false;
        emit cancelAiTriggered(ed);
    }
    if (!maybeSave(ed)) return false;
    QWidget* page = widget(index);
    removeTab(index);
    if (page) page->deleteLater();
    return true;
}

bool EditorTabWidget::closeAllDocuments() {
    while (count() > 0)
        if (!closeDocument(0)) return false;
    return true;
}
