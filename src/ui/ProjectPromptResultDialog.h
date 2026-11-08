// src/ui/ProjectPromptResultDialog.h
#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QTimer>
#include "../core/ConfigManager.h"

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
    QString suggestedFilePath;
    QString matchedProjectFilePath;
    QString title;
    QString code;
};

enum class MarkdownViewerMode {
    ProjectPromptResult,     // Отдельное окно результата AI Custom Prompt / Explain Code
    MarkdownDocumentViewer   // Встроенный расширенный просмотр внутри вкладки редактора
};

enum class MarkdownDisplaySubMode {
    RawMarkdown = 0,
    RenderedMarkdown = 1,
    ExtendedCodeView = 2
};

class ProjectPromptResultDialog : public QWidget {
    Q_OBJECT
public:
    // Конструктор для отдельного окна результата промпта проекта / Explain Code
    explicit ProjectPromptResultDialog(const QString& promptTitle,
                                       const QString& projectRootPath,
                                       const QString& taskId,
                                       const QString& systemPrompt,
                                       const QString& initialUserPrompt,
                                       QWidget* parent = nullptr);

    // Конструктор для встроенного просмотра .md-документа прямо во вкладке редактора (без отдельного окна)
    explicit ProjectPromptResultDialog(const QString& projectRootPath,
                                       QWidget* parent = nullptr);

    MarkdownViewerMode viewerMode() const { return m_viewerMode; }

    QString taskId() const { return m_taskId; }
    void setTaskId(const QString& id) { m_taskId = id; }

    void setDocumentContent(const QString& markdownText, const QString& projectRootPath);
    void setDisplaySubMode(MarkdownDisplaySubMode subMode);
    MarkdownDisplaySubMode displaySubMode() const { return m_subMode; }

    void beginFollowUpStream(const QString& newTaskId, const QString& displayFollowUp, bool seamlessAppend);
    void appendStreamChunk(const QString& chunk);
    void finalizeResponse(const QString& fullMarkdownText);
    void markFailedOrCancelled(const QString& message);

signals:
    void cancelTaskRequested(const QString& taskId);
    void continueRequestTriggered(ProjectPromptResultDialog* dlg,
                                  const QString& systemPrompt,
                                  const QVector<AiChatMessage>& history,
                                  const QString& followUpUserPrompt,
                                  bool seamlessAppend);
    void openContentInEditorRequested(const QString& suggestedPath, const QString& code, const QString& langId);
    void replaceFileInEditorRequested(const QString& existingFilePath, const QString& newCode, const QString& langId);
    void fileSavedToDisk(const QString& filePath);

private slots:
    void onStreamRenderTick();
    void onCopyFullMarkdown();
    void onSaveFullMarkdown();
    void onOpenFullMarkdownInEditor();
    void onContinueCutOffClicked();
    void onSendFollowUpClicked();
    void onSnippetSelectionChanged();
    void onSnippetContextMenu(const QPoint& pos);
    void onCopySnippetCode();
    void onCopySelectedSnippetText();
    void onSaveSnippetAsFile();
    void onOpenSnippetInEditor();
    void onReplaceSnippetInEditor();
    void onDiffSnippetWithProjectFile();
    void onSaveAllDetectedFiles();

private:
    void buildUi(const QString& promptTitle);
    void refreshProjectFilesIndex();
    QString resolveExistingProjectFile(const QString& suggestedPath) const;
    void renderMarkdownView(bool scrollToBottom = false);
    void parseCodeSnippetsFromMarkdown();

    static QVector<ExtractedCodeSnippet> extractSnippets(const QString& markdown);
    static QString detectFilePathFromLine(const QString& line);
    static QString detectFilePathFromCodeComment(const QString& firstCodeLine);
    static QString normalizeAndValidatePathCandidate(QString candidate, bool requireKnownOrLowerExt = false);

    MarkdownViewerMode m_viewerMode = MarkdownViewerMode::ProjectPromptResult;
    MarkdownDisplaySubMode m_subMode = MarkdownDisplaySubMode::ExtendedCodeView;
    QString m_projectRootPath;
    QStringList m_projectFiles;
    QString m_taskId;

    QString m_systemPrompt;
    QString m_currentTurnUserPrompt;
    QString m_currentTurnAssistantText;
    QVector<AiChatMessage> m_conversationHistory;

    QString m_markdownText;
    bool m_isStreaming = true;
    bool m_updatingRenderedView = false;
    bool m_followStreamScroll = true;
    QTimer m_streamRenderTimer;
    QVector<ExtractedCodeSnippet> m_snippets;

    QWidget* m_topBarWidget = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_continueGenBtn = nullptr;
    QPushButton* m_rawModeBtn = nullptr;
    QPushButton* m_renderedModeBtn = nullptr;
    QPushButton* m_extendedModeBtn = nullptr;
    QPushButton* m_copyMdBtn = nullptr;
    QPushButton* m_saveMdBtn = nullptr;
    QPushButton* m_openMdInTabBtn = nullptr;

    QStackedWidget* m_leftStack = nullptr;
    QTextEdit* m_renderedView = nullptr;
    QPlainTextEdit* m_rawView = nullptr;
    QWidget* m_rightPane = nullptr;

    QWidget* m_followUpBox = nullptr;
    QPlainTextEdit* m_followUpEdit = nullptr;
    QPushButton* m_sendFollowUpBtn = nullptr;

    QTreeWidget* m_snippetTree = nullptr;
    QLabel* m_snippetHeaderLabel = nullptr;
    QPlainTextEdit* m_snippetCodeEdit = nullptr;
    LexicalHighlighter* m_snippetHighlighter = nullptr;

    QPushButton* m_replaceInEditorBtn = nullptr;
    QPushButton* m_diffWithProjectBtn = nullptr;
    QPushButton* m_copySnippetBtn = nullptr;
    QPushButton* m_copySelectedSnippetBtn = nullptr;
    QPushButton* m_saveSnippetBtn = nullptr;
    QPushButton* m_openSnippetTabBtn = nullptr;
    QPushButton* m_saveAllFilesBtn = nullptr;
};
