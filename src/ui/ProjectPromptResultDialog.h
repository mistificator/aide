// src/ui/ProjectPromptResultDialog.h
#pragma once
#include <QDialog>
#include <QVector>
#include <QString>

class QTextEdit;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QStackedWidget;
class LexicalHighlighter;

struct ExtractedCodeSnippet {
    int index = 0;
    QString language;
    QString suggestedFilePath; // Распознанный путь к файлу (если есть)
    QString title;
    QString code;
};

class ProjectPromptResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectPromptResultDialog(const QString& promptTitle,
                                       const QString& projectRootPath,
                                       const QString& taskId,
                                       QWidget* parent = nullptr);

    QString taskId() const { return m_taskId; }
    void setTaskId(const QString& id) { m_taskId = id; }

    void appendStreamChunk(const QString& chunk);
    void finalizeResponse(const QString& fullMarkdownText);
    void markFailedOrCancelled(const QString& message);

signals:
    void cancelTaskRequested(const QString& taskId);
    void openContentInEditorRequested(const QString& suggestedPath, const QString& code, const QString& langId);
    void fileSavedToDisk(const QString& filePath);

private slots:
    void onToggleRawView(bool showRaw);
    void onCopyFullMarkdown();
    void onSaveFullMarkdown();
    void onOpenFullMarkdownInEditor();
    void onSnippetSelectionChanged();
    void onCopySnippetCode();
    void onCopySelectedSnippetText();
    void onSaveSnippetAsFile();
    void onOpenSnippetInEditor();
    void onSaveAllDetectedFiles();

private:
    void renderMarkdownView(bool scrollToBottom = false);
    void parseCodeSnippetsFromMarkdown();
    static QVector<ExtractedCodeSnippet> extractSnippets(const QString& markdown);
    static QString detectFilePathFromLine(const QString& line);

    QString m_projectRootPath;
    QString m_taskId;
    QString m_markdownText;
    bool m_isStreaming = true;
    QVector<ExtractedCodeSnippet> m_snippets;

    QLabel* m_statusLabel = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_toggleRawBtn = nullptr;
    QPushButton* m_copyMdBtn = nullptr;
    QPushButton* m_saveMdBtn = nullptr;
    QPushButton* m_openMdInTabBtn = nullptr;

    QStackedWidget* m_leftStack = nullptr;
    QTextEdit* m_renderedView = nullptr;
    QPlainTextEdit* m_rawView = nullptr;

    QTreeWidget* m_snippetTree = nullptr;
    QLabel* m_snippetHeaderLabel = nullptr;
    QPlainTextEdit* m_snippetCodeEdit = nullptr;
    LexicalHighlighter* m_snippetHighlighter = nullptr;

    QPushButton* m_copySnippetBtn = nullptr;
    QPushButton* m_copySelectedSnippetBtn = nullptr;
    QPushButton* m_saveSnippetBtn = nullptr;
    QPushButton* m_openSnippetTabBtn = nullptr;
    QPushButton* m_saveAllFilesBtn = nullptr;
};
