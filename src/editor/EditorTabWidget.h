// src/editor/EditorTabWidget.h
#pragma once
#include <QTabWidget>
#include "CodeEditor.h"

class EditorTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit EditorTabWidget(QWidget* parent = nullptr);

    CodeEditor* newDocument();
    CodeEditor* openFile(const QString& filePath);
    CodeEditor* currentEditor() const;
    CodeEditor* editorAt(int index) const;
    CodeEditor* findEditorByPath(const QString& filePath) const;

    bool saveCurrentDocument();
    bool saveCurrentDocumentAs();
    bool saveAllDocuments();
    bool closeDocument(int index);
    bool closeAllDocuments();

signals:
    void activeEditorChanged(CodeEditor* editor);
    void documentOpened(const QString& filePath, const QByteArray& content);
    void documentSaved(const QString& filePath, const QByteArray& content);
    void aiActionTriggered(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction);
    void cancelAiTriggered(CodeEditor* editor);

private:
    void registerEditor(CodeEditor* editor);
    void updateTabTitle(CodeEditor* editor);
    bool maybeSave(CodeEditor* editor);
};