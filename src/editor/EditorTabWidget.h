// src/editor/EditorTabWidget.h
#pragma once
#include <QTabWidget>
#include "CodeEditor.h"

enum class MarkdownTabViewMode {
    RawMarkdown = 0,
    RenderedMarkdown = 1,
    ExtendedCodeView = 2
};

class EditorTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit EditorTabWidget(QWidget* parent = nullptr);

    CodeEditor* newDocument();
    CodeEditor* openFile(const QString& filePath);
    CodeEditor* currentEditor() const;
    CodeEditor* editorAt(int index) const;
    CodeEditor* findEditorByPath(const QString& filePath) const;

    void setProjectRootPath(const QString& rootPath);
    void refreshEditorModeBar(CodeEditor* ed);
    void toggleCurrentMarkdownViewMode();
    bool isCurrentTabInRawEditMode() const;

    bool saveCurrentDocument();
    bool saveCurrentDocumentAs();
    bool saveAllDocuments();
    bool closeDocument(int index);
    bool closeAllDocuments();

    static bool isMarkdownEditor(const CodeEditor* ed);
    static void openContainingFolder(const QString& fileOrDirPath);
    static void openTerminalAtFolder(const QString& dirPath);

signals:
    void activeEditorChanged(CodeEditor* editor);
    void documentOpened(const QString& filePath, const QByteArray& content);
    void documentSaved(const QString& filePath, const QByteArray& content);
    void aiActionTriggered(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction);
    void aiContinueTriggered(CodeEditor* editor, const QString& followUpInstruction, bool seamlessAppend);
    void cancelAiTriggered(CodeEditor* editor);
    void diffRequested(CodeEditor* editor);

private slots:
    void onTabBarContextMenu(const QPoint& pos);

private:
    QWidget* createTabPageForEditor(CodeEditor* ed);
    int indexOfEditor(const CodeEditor* ed) const;
    void setTabMarkdownViewMode(QWidget* page, MarkdownTabViewMode mode);
    void registerEditor(CodeEditor* ed);
    void updateTabTitle(CodeEditor* ed);
    bool maybeSave(CodeEditor* ed);

    QString m_projectRootPath;
};
