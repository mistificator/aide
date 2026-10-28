// src/editor/EditorTabWidget.cpp
#include "EditorTabWidget.h"
#include "../core/ConfigManager.h"
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

EditorTabWidget::EditorTabWidget(QWidget* parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QTabBar::customContextMenuRequested, this, &EditorTabWidget::onTabBarContextMenu);

    connect(this, &QTabWidget::tabCloseRequested, this, &EditorTabWidget::closeDocument);
    connect(this, &QTabWidget::currentChanged, this, [this](int) { emit activeEditorChanged(currentEditor()); });
}

CodeEditor* EditorTabWidget::currentEditor() const { return qobject_cast<CodeEditor*>(currentWidget()); }
CodeEditor* EditorTabWidget::editorAt(int index) const { return qobject_cast<CodeEditor*>(widget(index)); }

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
    // Контекстное меню показываем только для сохранённых на диске файлов
    if (!ed || ed->filePath().isEmpty()) return;

    QString filePath = ed->filePath();
    QString folderPath = QFileInfo(filePath).absolutePath();

    QMenu menu(this);
    menu.addAction("Open Folder in Explorer / File Manager", this, [filePath]() {
        openContainingFolder(filePath);
    });
    menu.addAction("Open Console Terminal Here", this, [folderPath]() {
        openTerminalAtFolder(folderPath);
    });
    menu.exec(tabBar()->mapToGlobal(pos));
}

CodeEditor* EditorTabWidget::newDocument() {
    auto* ed = new CodeEditor(this);
    registerEditor(ed);
    setCurrentIndex(addTab(ed, ed->displayName()));
    ed->setFocus();
    return ed;
}

CodeEditor* EditorTabWidget::openFile(const QString& filePath) {
    if (auto* existing = findEditorByPath(filePath)) {
        setCurrentWidget(existing);
        ConfigManager::instance().setLastDialogDir(filePath);
        return existing;
    }
    if (count() == 1) {
        auto* first = editorAt(0);
        if (first && first->filePath().isEmpty() && !first->document()->isModified() && first->toPlainText().isEmpty()) {
            if (first->loadFile(filePath)) {
                updateTabTitle(first);
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
    setCurrentIndex(addTab(ed, ed->displayName()));
    updateTabTitle(ed);
    ConfigManager::instance().setLastDialogDir(filePath);
    emit documentOpened(ed->filePath(), ed->toPlainText().toUtf8());
    return ed;
}

void EditorTabWidget::registerEditor(CodeEditor* ed) {
    connect(ed->document(), &QTextDocument::modificationChanged, this, [this, ed](bool) {
        updateTabTitle(ed);
    });
    connect(ed, &CodeEditor::busyStateChanged, this, [this, ed](CodeEditor*, bool) {
        updateTabTitle(ed);
        if (ed == currentEditor()) {
            emit activeEditorChanged(ed);
        }
    });
    connect(ed, &CodeEditor::fileSaved, this, [this, ed](const QString& path, const QByteArray& content) {
        updateTabTitle(ed);
        emit documentSaved(path, content);
        if (ed == currentEditor()) {
            emit activeEditorChanged(ed);
        }
    });
    connect(ed, &CodeEditor::aiActionRequested, this, &EditorTabWidget::aiActionTriggered);
    connect(ed, &CodeEditor::cancelAiRequested, this, &EditorTabWidget::cancelAiTriggered);
    connect(ed, &CodeEditor::diffRequested, this, &EditorTabWidget::diffRequested);
}

void EditorTabWidget::updateTabTitle(CodeEditor* ed) {
    int idx = indexOf(ed);
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
            if (ed->filePath().isEmpty()) { setCurrentIndex(i); allOk &= saveCurrentDocumentAs(); }
            else allOk &= ed->saveFile();
        }
    }
    return allOk;
}

bool EditorTabWidget::maybeSave(CodeEditor* ed) {
    if (!ed || !ed->document()->isModified()) return true;
    setCurrentWidget(ed);
    auto btn = QMessageBox::warning(this, "Unsaved Changes",
                                    QString("Document '%1' has unsaved changes.\nDo you want to save them?").arg(ed->displayName()),
                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save) return saveCurrentDocument();
    return btn != QMessageBox::Cancel;
}

bool EditorTabWidget::closeDocument(int index) {
    auto* ed = editorAt(index);
    if (!ed) return false;
    if (ed->isAiBusy()) {
        auto res = QMessageBox::question(this, "AI Operation in Progress",
                                         "An AI operation is currently running on this document. Cancel it and close?",
                                         QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::No) return false;
        emit cancelAiTriggered(ed);
    }
    if (!maybeSave(ed)) return false;
    removeTab(index);
    ed->deleteLater();
    return true;
}

bool EditorTabWidget::closeAllDocuments() {
    while (count() > 0)
        if (!closeDocument(0)) return false;
    return true;
}
