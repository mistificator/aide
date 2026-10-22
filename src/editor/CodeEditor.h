// src/editor/CodeEditor.h
#pragma once
#include <QPlainTextEdit>
#include <QTextCursor>
#include "../syntax/ColorScheme.h"

class QWidget;
class QLabel;
class QPushButton;
class LexicalHighlighter;

enum class AiEditAction {
    ExplainCode, RefactorCode, FindBugs, OptimizePerformance,
    AddDocumentation, GenerateUnitTests, CompleteCode, CustomPrompt
};

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    bool loadFile(const QString& filePath);
    bool saveFile();
    bool saveFileAs(const QString& newFilePath);
    QString filePath() const { return m_filePath; }
    QString savedDiskContent() const { return m_savedDiskContent; }
    QString displayName() const;

    void applyColorScheme(const ColorScheme& scheme);
    void setSyntaxLanguage(const QString& langId);
    QString syntaxLanguage() const;
    void triggerAiAction(AiEditAction action);

    void setAiBusy(bool busy, const QString& statusText = {});
    bool isAiBusy() const { return m_isAiBusy; }

    void beginAiStreamEdit();
    void appendAiStreamChunk(const QString& chunk);
    void finishAiStreamEdit();
    void cancelAndRollbackAiEdit();

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth() const;

signals:
    void aiActionRequested(CodeEditor* editor, AiEditAction action, const QString& selectedText, const QString& customInstruction);
    void cancelAiRequested(CodeEditor* editor);
    void diffRequested(CodeEditor* editor);
    void fileSaved(const QString& filePath, const QByteArray& content);
    void busyStateChanged(CodeEditor* editor, bool busy);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount = 0);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    void setupOverlayBanner();

    QString m_filePath;
    QString m_savedDiskContent;
    QWidget* m_lineNumberArea = nullptr;
    LexicalHighlighter* m_highlighter = nullptr;
    ColorScheme m_scheme;

    QWidget* m_busyBanner = nullptr;
    QLabel* m_busyLabel = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    bool m_isAiBusy = false;

    QTextCursor m_streamCursor;
    bool m_isFirstChunk = true, m_headerResolved = false;
    QString m_headerBuf, m_tailBuf;
};

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }
protected:
    void paintEvent(QPaintEvent* event) override { m_editor->lineNumberAreaPaintEvent(event); }
private:
    CodeEditor* m_editor;
};
