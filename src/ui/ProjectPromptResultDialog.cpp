// src/ui/ProjectPromptResultDialog.cpp
#include "ProjectPromptResultDialog.h"
#include "MarkdownRenderer.h"
#include "../syntax/LexicalHighlighter.h"
#include "../syntax/ColorScheme.h"
#include "../core/ConfigManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QScrollBar>
#include <QFontDatabase>
#include <QRegularExpression>

ProjectPromptResultDialog::ProjectPromptResultDialog(const QString& promptTitle,
                                                     const QString& projectRootPath,
                                                     const QString& taskId,
                                                     QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint),
    m_projectRootPath(projectRootPath),
    m_taskId(taskId) {
    setAttribute(Qt::WA_DeleteOnClose);
    QString shortPrompt = promptTitle.simplified().left(70);
    if (promptTitle.simplified().length() > 70) shortPrompt += "...";
    setWindowTitle("AI Project Custom Prompt Result — " + shortPrompt);
    resize(1220, 780);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(8, 8, 8, 8);
    mainLay->setSpacing(6);

    // Верхняя панель статуса и действий со всем Markdown-документом
    auto* topBar = new QHBoxLayout();
    m_statusLabel = new QLabel("AI: Generating Markdown response...", this);
    m_statusLabel->setStyleSheet("color: #8be9fd; font-weight: bold;");

    m_cancelBtn = new QPushButton("Cancel Generation", this);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #ff5555; color: #ffffff; border: none; border-radius: 4px; padding: 4px 10px; font-weight: bold; }"
        "QPushButton:hover { background-color: #ff6e6e; }");

    m_toggleRawBtn = new QPushButton("View Raw Markdown", this);
    m_toggleRawBtn->setCheckable(true);

    m_copyMdBtn = new QPushButton("Copy Full Markdown", this);
    m_saveMdBtn = new QPushButton("Save Markdown Document (.md)...", this);
    m_saveMdBtn->setStyleSheet(
        "QPushButton { background-color: #bd93f9; color: #191a24; font-weight: bold; border-radius: 4px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #caa6ff; }");
    m_openMdInTabBtn = new QPushButton("Open .md in Editor Tab", this);

    topBar->addWidget(m_statusLabel);
    topBar->addWidget(m_cancelBtn);
    topBar->addStretch();
    topBar->addWidget(m_toggleRawBtn);
    topBar->addWidget(m_copyMdBtn);
    topBar->addWidget(m_openMdInTabBtn);
    topBar->addWidget(m_saveMdBtn);
    mainLay->addLayout(topBar);

    // Главный горизонтальный сплиттер: слева Markdown-документ, справа извлечённые файлы и фрагменты кода
    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setChildrenCollapsible(false);

    // Левая панель: Markdown-документ (Rendered / Raw)
    m_leftStack = new QStackedWidget(mainSplitter);
    m_renderedView = new QTextEdit(m_leftStack);
    m_renderedView->setReadOnly(true);
    MarkdownRenderer::configureViewer(m_renderedView);

    m_rawView = new QPlainTextEdit(m_leftStack);
    m_rawView->setReadOnly(false);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(10);
    m_rawView->setFont(monoFont);
    auto scheme = ColorScheme::loadByName(ConfigManager::instance().activeTheme());
    QString edStyle = QString("QPlainTextEdit { background-color: %1; color: %2; selection-background-color: %3; }")
                          .arg(scheme.editorBg.name(), scheme.editorFg.name(), scheme.selectionBg.name());
    m_rawView->setStyleSheet(edStyle);

    m_leftStack->addWidget(m_renderedView);
    m_leftStack->addWidget(m_rawView);

    // Правая панель: Извлечённые файлы и фрагменты кода
    auto* rightPane = new QWidget(mainSplitter);
    auto* rightLay = new QVBoxLayout(rightPane);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(6);

    auto* rightHdrRow = new QHBoxLayout();
    auto* rightTitle = new QLabel("<b>Extracted Files & Code Fragments</b>", rightPane);
    m_saveAllFilesBtn = new QPushButton("Save All Detected Files...", rightPane);
    m_saveAllFilesBtn->setToolTip("Save all extracted code blocks that have a detected file path");
    m_saveAllFilesBtn->setEnabled(false);
    rightHdrRow->addWidget(rightTitle, 1);
    rightHdrRow->addWidget(m_saveAllFilesBtn);
    rightLay->addLayout(rightHdrRow);

    auto* rightSplit = new QSplitter(Qt::Vertical, rightPane);
    rightSplit->setChildrenCollapsible(false);

    m_snippetTree = new QTreeWidget(rightSplit);
    m_snippetTree->setColumnCount(3);
    m_snippetTree->setHeaderLabels({"#", "File / Fragment", "Lang / Lines"});
    m_snippetTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_snippetTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_snippetTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_snippetTree->setAlternatingRowColors(true);
    m_snippetTree->setRootIsDecorated(false);

    auto* previewBox = new QWidget(rightSplit);
    auto* previewLay = new QVBoxLayout(previewBox);
    previewLay->setContentsMargins(0, 0, 0, 0);
    previewLay->setSpacing(4);

    m_snippetHeaderLabel = new QLabel("Select a file or code fragment above to preview, copy, or save.", previewBox);
    m_snippetHeaderLabel->setStyleSheet("color: #888899; font-size: 11px; font-weight: bold;");
    m_snippetHeaderLabel->setWordWrap(true);

    m_snippetCodeEdit = new QPlainTextEdit(previewBox);
    m_snippetCodeEdit->setFont(monoFont);
    m_snippetCodeEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_snippetCodeEdit->setStyleSheet(edStyle);
    m_snippetHighlighter = new LexicalHighlighter(m_snippetCodeEdit->document());
    m_snippetHighlighter->setColorScheme(scheme);

    auto* snippetBtnRow = new QHBoxLayout();
    m_copySnippetBtn = new QPushButton("Copy Block", previewBox);
    m_copySelectedSnippetBtn = new QPushButton("Copy Selected", previewBox);
    m_openSnippetTabBtn = new QPushButton("Open in Editor Tab", previewBox);
    m_saveSnippetBtn = new QPushButton("Save File / Fragment As...", previewBox);
    m_saveSnippetBtn->setStyleSheet(
        "QPushButton { background-color: #50fa7b; color: #191a24; font-weight: bold; border-radius: 4px; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #69ff94; }"
        "QPushButton:disabled { background-color: #44475a; color: #888899; }");

    snippetBtnRow->addWidget(m_copySnippetBtn);
    snippetBtnRow->addWidget(m_copySelectedSnippetBtn);
    snippetBtnRow->addWidget(m_openSnippetTabBtn);
    snippetBtnRow->addStretch();
    snippetBtnRow->addWidget(m_saveSnippetBtn);

    previewLay->addWidget(m_snippetHeaderLabel);
    previewLay->addWidget(m_snippetCodeEdit, 1);
    previewLay->addLayout(snippetBtnRow);

    rightSplit->addWidget(m_snippetTree);
    rightSplit->addWidget(previewBox);
    rightSplit->setStretchFactor(0, 1);
    rightSplit->setStretchFactor(1, 2);
    rightSplit->setSizes({210, 460});

    rightLay->addWidget(rightSplit, 1);

    mainSplitter->addWidget(m_leftStack);
    mainSplitter->addWidget(rightPane);
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setSizes({700, 500});

    mainLay->addWidget(mainSplitter, 1);

    m_copySnippetBtn->setEnabled(false);
    m_copySelectedSnippetBtn->setEnabled(false);
    m_openSnippetTabBtn->setEnabled(false);
    m_saveSnippetBtn->setEnabled(false);

    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (!m_taskId.isEmpty()) emit cancelTaskRequested(m_taskId);
    });
    connect(m_toggleRawBtn, &QPushButton::toggled, this, &ProjectPromptResultDialog::onToggleRawView);
    connect(m_copyMdBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onCopyFullMarkdown);
    connect(m_saveMdBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onSaveFullMarkdown);
    connect(m_openMdInTabBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onOpenFullMarkdownInEditor);

    connect(m_rawView, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_leftStack->currentWidget() == m_rawView && !m_isStreaming) {
            m_markdownText = m_rawView->toPlainText();
            parseCodeSnippetsFromMarkdown();
        }
    });

    connect(m_snippetTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) {
        onSnippetSelectionChanged();
    });
    connect(m_snippetCodeEdit, &QPlainTextEdit::copyAvailable, m_copySelectedSnippetBtn, &QPushButton::setEnabled);
    connect(m_copySnippetBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onCopySnippetCode);
    connect(m_copySelectedSnippetBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onCopySelectedSnippetText);
    connect(m_saveSnippetBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onSaveSnippetAsFile);
    connect(m_openSnippetTabBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onOpenSnippetInEditor);
    connect(m_saveAllFilesBtn, &QPushButton::clicked, this, &ProjectPromptResultDialog::onSaveAllDetectedFiles);
}

void ProjectPromptResultDialog::appendStreamChunk(const QString& chunk) {
    if (chunk.isEmpty()) return;
    m_markdownText += chunk;
    m_statusLabel->setText(QString("AI: Streaming Markdown response (%1 chars)...").arg(m_markdownText.length()));
    renderMarkdownView(true);
    parseCodeSnippetsFromMarkdown();
}

void ProjectPromptResultDialog::finalizeResponse(const QString& fullMarkdownText) {
    m_isStreaming = false;
    if (!fullMarkdownText.trimmed().isEmpty()) {
        m_markdownText = fullMarkdownText;
    }
    m_cancelBtn->hide();
    renderMarkdownView(false);
    parseCodeSnippetsFromMarkdown();
    m_statusLabel->setStyleSheet("color: #50fa7b; font-weight: bold;");
    m_statusLabel->setText(QString("Completed: %1 chars | Extracted %2 code block(s)")
                               .arg(m_markdownText.length())
                               .arg(m_snippets.size()));
}

void ProjectPromptResultDialog::markFailedOrCancelled(const QString& message) {
    m_isStreaming = false;
    m_cancelBtn->hide();
    m_statusLabel->setStyleSheet("color: #ff5555; font-weight: bold;");
    m_statusLabel->setText(message);
    parseCodeSnippetsFromMarkdown();
}

void ProjectPromptResultDialog::renderMarkdownView(bool scrollToBottom) {
    QScrollBar* vbar = m_renderedView->verticalScrollBar();
    int oldScroll = vbar->value();
    int oldMax = vbar->maximum();
    bool wasAtBottom = (oldMax == 0) || (oldScroll >= oldMax - 8);

    m_renderedView->setHtml(MarkdownRenderer::toHtml(m_markdownText));
    if (m_leftStack->currentWidget() == m_rawView) {
        m_rawView->setPlainText(m_markdownText);
    }

    if (scrollToBottom && wasAtBottom) {
        vbar->setValue(vbar->maximum());
    } else {
        vbar->setValue(oldScroll);
    }
}

void ProjectPromptResultDialog::onToggleRawView(bool showRaw) {
    if (showRaw) {
        m_toggleRawBtn->setText("View Rendered Markdown");
        m_rawView->setPlainText(m_markdownText);
        m_leftStack->setCurrentWidget(m_rawView);
    } else {
        m_toggleRawBtn->setText("View Raw Markdown");
        m_markdownText = m_rawView->toPlainText();
        m_renderedView->setHtml(MarkdownRenderer::toHtml(m_markdownText));
        m_leftStack->setCurrentWidget(m_renderedView);
    }
}

void ProjectPromptResultDialog::onCopyFullMarkdown() {
    QApplication::clipboard()->setText(m_markdownText);
    m_statusLabel->setText("Full Markdown copied to clipboard.");
}

void ProjectPromptResultDialog::onSaveFullMarkdown() {
    QString baseDir = ConfigManager::instance().resolveInitialDialogDir(m_projectRootPath);
    QString defaultFile = QDir(baseDir).filePath("project_ai_response.md");
    QString path = QFileDialog::getSaveFileName(
        this, "Save Markdown Document", defaultFile, "Markdown Files (*.md *.markdown);;All Files (*)");
    if (path.isEmpty()) return;

    ConfigManager::instance().setLastDialogDir(path);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Error", "Failed to open file for writing:\n" + path);
        return;
    }
    f.write(m_markdownText.toUtf8());
    if (f.commit()) {
        m_statusLabel->setText("Saved Markdown document to: " + QDir::toNativeSeparators(path));
        emit fileSavedToDisk(path);
    }
}

void ProjectPromptResultDialog::onOpenFullMarkdownInEditor() {
    emit openContentInEditorRequested("project_ai_response.md", m_markdownText, "markdown");
}

QString ProjectPromptResultDialog::normalizeAndValidatePathCandidate(QString candidate, bool requireKnownOrLowerExt) {
    candidate = candidate.trimmed();
    while (!candidate.isEmpty() &&
           (candidate.startsWith('`') || candidate.startsWith('\'') ||
            candidate.startsWith('"') || candidate.startsWith('*') ||
            candidate.startsWith('(') || candidate.startsWith('['))) {
        candidate.remove(0, 1);
    }
    while (!candidate.isEmpty() &&
           (candidate.endsWith('`') || candidate.endsWith('\'') ||
            candidate.endsWith('"') || candidate.endsWith('*') ||
            candidate.endsWith(':') || candidate.endsWith(';') ||
            candidate.endsWith(',') || candidate.endsWith(')') ||
            candidate.endsWith(']'))) {
        candidate.chop(1);
    }

    candidate.replace('\\', '/');
    while (candidate.startsWith("./")) candidate.remove(0, 2);
    while (candidate.startsWith('/')) candidate.remove(0, 1);

    if (candidate.isEmpty() || candidate.contains("..") || candidate.contains("//")) {
        return {};
    }
    if (candidate.startsWith("http:", Qt::CaseInsensitive) ||
        candidate.startsWith("https:", Qt::CaseInsensitive)) {
        return {};
    }

    QFileInfo fi(candidate);
    QString fileName = fi.fileName();
    QString ext = fi.suffix();

    // Проверка известных имён файлов без расширения (Makefile, Dockerfile и т.д.)
    const QStringList allowedNames = ConfigManager::instance().aiBehavior().summaryAllowedFilenames;
    if (ext.isEmpty()) {
        for (const QString& SpecialName : allowedNames) {
            if (fileName.compare(SpecialName.trimmed(), Qt::CaseInsensitive) == 0) {
                return candidate;
            }
        }
        return {};
    }

    // Расширение должно начинаться с буквы (исключает числа вроде 1.2) и иметь разумную длину
    if (ext.length() > 12 || !ext[0].isLetter()) {
        return {};
    }

    if (requireKnownOrLowerExt && !candidate.contains('/')) {
        // Если путь без '/' и не был выделен бэктиками, требуем строчное расширение
        // (отсекает конструкции вида Boost.Beast, Qt.Core и т.п.)
        if (ext != ext.toLower()) {
            return {};
        }
    }

    return candidate;
}

QString ProjectPromptResultDialog::detectFilePathFromLine(const QString& line) {
    QString clean = line.trimmed();
    if (clean.isEmpty()) return {};

    // 1. Наивысший приоритет: путь внутри обратных кавычек `path/to/file.ext`
    static const QRegularExpression rxBacktick(R"(`([^`]+)`)");
    auto btIt = rxBacktick.globalMatch(clean);
    while (btIt.hasNext()) {
        auto m = btIt.next();
        QString token = m.captured(1).trimmed();
        // Пропускаем вызовы функций вроде `readLine()` или `split(' ')`
        if (token.contains('(') || token.contains(')') || token.contains(' ')) continue;
        QString valid = normalizeAndValidatePathCandidate(token, false);
        if (!valid.isEmpty()) {
            return valid;
        }
    }

    // 2. Явная метка File: / Path: / Filename: / Файл:
    static const QRegularExpression rxExplicitLabel(
        R"((?:^|[\s\*#\-])(?:File(?:\s*name)?|Path|Файл)\s*[:\-]\s*[`\*\"']*([A-Za-z0-9_\-\./\\]+(?:\.[A-Za-z][A-Za-z0-9_+\-]*)?))",
        QRegularExpression::CaseInsensitiveOption);
    auto lm = rxExplicitLabel.match(clean);
    if (lm.hasMatch()) {
        QString valid = normalizeAndValidatePathCandidate(lm.captured(1), false);
        if (!valid.isEmpty()) {
            return valid;
        }
    }

    // 3. Поиск пути в заголовке или строке-метке (с проверкой, что это не вызов метода и не число)
    static const QRegularExpression rxPath(
        R"((?:^|[\s:`\*#\-"\(\[])([A-Za-z0-9_\-\./\\]+\.[A-Za-z][A-Za-z0-9_+\-]*)(?=$|[\s:`\*\"'\)\],;]))");
    auto it = rxPath.globalMatch(clean);
    while (it.hasNext()) {
        auto m = it.next();
        QString valid = normalizeAndValidatePathCandidate(m.captured(1), true);
        if (!valid.isEmpty()) {
            return valid;
        }
    }

    return {};
}

QString ProjectPromptResultDialog::detectFilePathFromCodeComment(const QString& firstCodeLine) {
    QString trimmed = firstCodeLine.trimmed();
    if (trimmed.isEmpty()) return {};

    QString commentBody;
    if (trimmed.startsWith("//")) {
        int i = 2;
        while (i < trimmed.length() && trimmed[i] == '/') ++i;
        commentBody = trimmed.mid(i).trimmed();
    } else if (trimmed.startsWith("/*")) {
        commentBody = trimmed.mid(2);
        if (commentBody.endsWith("*/")) commentBody.chop(2);
        commentBody = commentBody.trimmed();
    } else if (trimmed.startsWith("--")) {
        commentBody = trimmed.mid(2).trimmed();
    } else if (trimmed.startsWith('#')) {
        // Исключаем шебанг (#!/usr/bin/env ...)
        if (trimmed.startsWith("#!")) return {};

        // Исключаем директивы препроцессора C/C++/C# (#include, #ifndef, #define, #pragma и т.д.)
        static const QRegularExpression rxPreproc(
            R"(^#\s*(?:include|pragma|ifndef|ifdef|if|elif|else|endif|define|undef|error|warning|line|import|using|region|endregion)\b)",
            QRegularExpression::CaseInsensitiveOption);
        if (rxPreproc.match(trimmed).hasMatch()) {
            return {};
        }

        int i = 1;
        while (i < trimmed.length() && trimmed[i] == '#') ++i;
        commentBody = trimmed.mid(i).trimmed();
    } else {
        return {};
    }

    if (commentBody.isEmpty()) return {};

    // Убираем необязательный префикс "File:" или "Path:" в начале комментария
    static const QRegularExpression rxPrefix(
        R"(^(?:File(?:\s*name)?|Path|Файл)\s*[:\-]\s*)",
        QRegularExpression::CaseInsensitiveOption);
    commentBody.remove(rxPrefix);
    commentBody = commentBody.trimmed();

    // Комментарий с путём к файлу должен начинаться непосредственно с пути
    // и либо состоять только из него, либо отделяться дефисом/скобкой (например: "// src/main.cpp - entry point")
    static const QRegularExpression rxCommentPath(
        R"(^[`\*\"']*([A-Za-z0-9_\-\./\\]+(?:\.[A-Za-z][A-Za-z0-9_+\-]*)?)[`\*\"']*(?:\s*(?:[\-\—\|]|\(.*\)).*)?$)");
    auto m = rxCommentPath.match(commentBody);
    if (m.hasMatch()) {
        return normalizeAndValidatePathCandidate(m.captured(1), true);
    }

    return {};
}

QVector<ExtractedCodeSnippet> ProjectPromptResultDialog::extractSnippets(const QString& markdown) {
    QVector<ExtractedCodeSnippet> result;
    QString normalized = markdown;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    const QStringList lines = normalized.split('\n');

    bool inFence = false;
    QString fenceMarker;
    QString fenceLang;
    QString fenceHintPath;
    int fenceStartLine = 0;
    int lastFenceEndLine = -1;
    QStringList codeLines;

    for (int i = 0; i < lines.size(); ++i) {
        const QString& rawLine = lines[i];
        const QString trimmed = rawLine.trimmed();

        if (!inFence && trimmed.startsWith("```")) {
            int backticks = 0;
            while (backticks < trimmed.length() && trimmed[backticks] == '`') ++backticks;
            fenceMarker = QString(backticks, '`');
            QString info = trimmed.mid(backticks).trimmed();

            // Разбираем инфо-строку: ```cpp:src/main.cpp или ```cpp src/main.cpp
            fenceLang.clear();
            fenceHintPath.clear();
            if (!info.isEmpty()) {
                int colonIdx = info.indexOf(':');
                int spaceIdx = info.indexOf(' ');
                if (colonIdx > 0) {
                    fenceLang = info.left(colonIdx).trimmed();
                    fenceHintPath = detectFilePathFromLine(info.mid(colonIdx + 1));
                } else if (spaceIdx > 0) {
                    fenceLang = info.left(spaceIdx).trimmed();
                    fenceHintPath = detectFilePathFromLine(info.mid(spaceIdx + 1));
                } else {
                    if (info.contains('.') || info.contains('/')) {
                        fenceHintPath = detectFilePathFromLine(info);
                        fenceLang = QFileInfo(fenceHintPath).suffix().toLower();
                    } else {
                        fenceLang = info.toLower();
                    }
                }
            }

            inFence = true;
            fenceStartLine = i;
            codeLines.clear();
            continue;
        }

        if (inFence) {
            if (trimmed.startsWith(fenceMarker) && trimmed.mid(fenceMarker.length()).trimmed().isEmpty()) {
                ExtractedCodeSnippet snip;
                snip.index = result.size() + 1;
                snip.language = fenceLang.isEmpty() ? "txt" : fenceLang;
                snip.code = codeLines.join('\n');

                // 1. Приоритет 1: путь из открывающей строки ```lang:path
                QString detectedPath = fenceHintPath;

                // 2. Приоритет 2: заголовок секции (### File: `main.cpp`) или явная метка над блоком кода
                if (detectedPath.isEmpty()) {
                    static const QRegularExpression rxHr(R"(^(?:-\s*){3,}$|^(?:\*\s*){3,}$|^(?:_\s*){3,}$)");
                    static const QRegularExpression rxLabelLine(
                        R"(^(?:[#]+|\*{1,2}|[-+]\s+\*{0,2})?\s*(?:File(?:\s*name)?|Path|Файл)\s*[:\-])",
                        QRegularExpression::CaseInsensitiveOption);

                    int minLine = qMax(lastFenceEndLine + 1, fenceStartLine - 25);

                    // Проход А: ищем ближайший заголовок Markdown (# .. ######) или явную строку "File: ..."
                    // (пропуская обычные абзацы текста между заголовком и блоком кода)
                    for (int prev = fenceStartLine - 1; prev >= minLine; --prev) {
                        QString prevLine = lines[prev].trimmed();
                        if (prevLine.isEmpty()) continue;
                        if (prevLine.startsWith("```") || rxHr.match(prevLine).hasMatch()) break;

                        bool isHeading = prevLine.startsWith('#');
                        bool isExplicitFileLabel = rxLabelLine.match(prevLine).hasMatch();

                        if (isHeading || isExplicitFileLabel) {
                            detectedPath = detectFilePathFromLine(prevLine);
                            // Останавливаемся на ближайшем заголовке секции
                            break;
                        }
                    }

                    // Проход Б: если заголовка нет, проверяем строку непосредственно над блоком кода —
                    // вдруг она состоит ТОЛЬКО из имени файла (например, "**`src/main.cpp`**" или "`main.cpp`:")
                    if (detectedPath.isEmpty()) {
                        for (int prev = fenceStartLine - 1; prev >= qMax(minLine, fenceStartLine - 3); --prev) {
                            QString prevLine = lines[prev].trimmed();
                            if (prevLine.isEmpty()) continue;
                            if (prevLine.startsWith("```") || rxHr.match(prevLine).hasMatch()) break;

                            QString stripped = prevLine;
                            stripped.remove(QRegularExpression(R"(^[\s\*_`#\-:]+|[\s\*_`#\-:]+$)"));
                            if (!stripped.contains(' ')) {
                                detectedPath = normalizeAndValidatePathCandidate(stripped, true);
                            }
                            break;
                        }
                    }
                }

                // 3. Приоритет 3: комментарий в первой строке самого кода (// src/foo.cpp или # script.py)
                if (detectedPath.isEmpty() && !codeLines.isEmpty()) {
                    detectedPath = detectFilePathFromCodeComment(codeLines.first());
                }

                snip.suggestedFilePath = detectedPath;
                if (!detectedPath.isEmpty() && (snip.language == "txt" || snip.language.isEmpty())) {
                    snip.language = ConfigManager::instance().languageIdForFile(detectedPath);
                }

                snip.title = detectedPath.isEmpty()
                                 ? QString("Fragment #%1 (%2)").arg(snip.index).arg(snip.language)
                                 : detectedPath;

                result.append(snip);
                inFence = false;
                lastFenceEndLine = i;
                fenceMarker.clear();
                codeLines.clear();
            } else {
                codeLines.append(rawLine);
            }
        }
    }

    return result;
}

void ProjectPromptResultDialog::parseCodeSnippetsFromMarkdown() {
    int prevRow = m_snippetTree->currentIndex().row();
    m_snippets = extractSnippets(m_markdownText);

    m_snippetTree->clear();
    int detectedFileCount = 0;

    for (const auto& s : m_snippets) {
        int lineCount = s.code.isEmpty() ? 0 : (s.code.count('\n') + 1);
        auto* item = new QTreeWidgetItem(m_snippetTree);
        item->setText(0, QString::number(s.index));
        item->setText(1, s.title);
        item->setText(2, QString("%1 (%2 L)").arg(s.language).arg(lineCount));
        if (!s.suggestedFilePath.isEmpty()) {
            item->setForeground(1, QColor("#50fa7b"));
            ++detectedFileCount;
        } else {
            item->setForeground(1, QColor("#f8f8f2"));
        }
    }

    m_saveAllFilesBtn->setEnabled(detectedFileCount > 0);
    m_saveAllFilesBtn->setText(detectedFileCount > 0
                                   ? QString("Save All Detected Files (%1)...").arg(detectedFileCount)
                                   : "Save All Detected Files...");

    if (!m_snippets.isEmpty()) {
        int targetRow = (prevRow >= 0 && prevRow < m_snippets.size()) ? prevRow : 0;
        m_snippetTree->setCurrentItem(m_snippetTree->topLevelItem(targetRow));
    } else {
        m_snippetCodeEdit->clear();
        m_snippetHeaderLabel->setText("No fenced code blocks (```) found in Markdown yet.");
        m_copySnippetBtn->setEnabled(false);
        m_openSnippetTabBtn->setEnabled(false);
        m_saveSnippetBtn->setEnabled(false);
    }
}

void ProjectPromptResultDialog::onSnippetSelectionChanged() {
    int row = m_snippetTree->indexOfTopLevelItem(m_snippetTree->currentItem());
    if (row < 0 || row >= m_snippets.size()) {
        m_copySnippetBtn->setEnabled(false);
        m_openSnippetTabBtn->setEnabled(false);
        m_saveSnippetBtn->setEnabled(false);
        return;
    }

    const auto& s = m_snippets[row];
    m_snippetHeaderLabel->setText(
        s.suggestedFilePath.isEmpty()
            ? QString("Fragment #%1 | Language: %2 (you can edit or select lines below before saving/copying)")
                  .arg(s.index)
                  .arg(s.language)
            : QString("Detected File: %1 | Language: %2")
                  .arg(s.suggestedFilePath, s.language));

    QString langId = s.suggestedFilePath.isEmpty()
                         ? s.language
                         : ConfigManager::instance().languageIdForFile(s.suggestedFilePath);
    m_snippetHighlighter->setLanguage(langId);
    m_snippetCodeEdit->setPlainText(s.code);

    m_copySnippetBtn->setEnabled(true);
    m_openSnippetTabBtn->setEnabled(true);
    m_saveSnippetBtn->setEnabled(true);
}

void ProjectPromptResultDialog::onCopySnippetCode() {
    QApplication::clipboard()->setText(m_snippetCodeEdit->toPlainText());
    m_statusLabel->setText("Code block copied to clipboard.");
}

void ProjectPromptResultDialog::onCopySelectedSnippetText() {
    QTextCursor c = m_snippetCodeEdit->textCursor();
    QString txt = c.hasSelection() ? c.selectedText().replace(QChar(0x2029), '\n') : m_snippetCodeEdit->toPlainText();
    QApplication::clipboard()->setText(txt);
    m_statusLabel->setText("Selected code fragment copied to clipboard.");
}

void ProjectPromptResultDialog::onSaveSnippetAsFile() {
    int row = m_snippetTree->indexOfTopLevelItem(m_snippetTree->currentItem());
    if (row < 0 || row >= m_snippets.size()) return;

    const auto& s = m_snippets[row];
    QTextCursor c = m_snippetCodeEdit->textCursor();
    // Если пользователь выделил часть фрагмента в предпросмотре, спросим: сохранить весь блок или только выделенный фрагмент
    QString contentToSave = m_snippetCodeEdit->toPlainText();
    if (c.hasSelection()) {
        auto ans = QMessageBox::question(
            this,
            "Save Code Fragment",
            "You have a selection in the code preview.\nSave ONLY the selected lines (Yes) or the ENTIRE code block (No)?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (ans == QMessageBox::Cancel) return;
        if (ans == QMessageBox::Yes) {
            contentToSave = c.selectedText().replace(QChar(0x2029), '\n');
        }
    }

    QString baseDir = ConfigManager::instance().resolveInitialDialogDir(m_projectRootPath);
    QString initialPath;
    if (!s.suggestedFilePath.isEmpty()) {
        initialPath = QDir(baseDir).filePath(s.suggestedFilePath);
    } else {
        QString ext = s.language.isEmpty() ? "txt" : s.language;
        initialPath = QDir(baseDir).filePath(QString("fragment_%1.%2").arg(s.index).arg(ext));
    }

    QFileInfo targetFi(initialPath);
    if (!targetFi.dir().exists()) {
        QDir().mkpath(targetFi.absolutePath());
    }

    QString savePath = QFileDialog::getSaveFileName(this, "Save File or Code Fragment", initialPath);
    if (savePath.isEmpty()) return;

    ConfigManager::instance().setLastDialogDir(savePath);
    QSaveFile f(savePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Error", "Failed to write file:\n" + savePath);
        return;
    }
    f.write(contentToSave.toUtf8());
    if (f.commit()) {
        m_statusLabel->setText("Saved file: " + QDir::toNativeSeparators(savePath));
        emit fileSavedToDisk(savePath);
    }
}

void ProjectPromptResultDialog::onOpenSnippetInEditor() {
    int row = m_snippetTree->indexOfTopLevelItem(m_snippetTree->currentItem());
    if (row < 0 || row >= m_snippets.size()) return;
    const auto& s = m_snippets[row];
    emit openContentInEditorRequested(s.suggestedFilePath, m_snippetCodeEdit->toPlainText(), s.language);
}

void ProjectPromptResultDialog::onSaveAllDetectedFiles() {
    QString startDir = ConfigManager::instance().resolveInitialDialogDir(m_projectRootPath);
    QString targetRoot = QFileDialog::getExistingDirectory(
        this, "Select Target Root Folder to Save Detected Files", startDir);
    if (targetRoot.isEmpty()) return;

    ConfigManager::instance().setLastDialogDir(targetRoot);
    int savedCount = 0;
    for (const auto& s : m_snippets) {
        if (s.suggestedFilePath.isEmpty()) continue;
        QString absPath = QDir(targetRoot).filePath(s.suggestedFilePath);
        QFileInfo fi(absPath);
        QDir().mkpath(fi.absolutePath());

        QSaveFile f(absPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(s.code.toUtf8());
            if (f.commit()) {
                ++savedCount;
                emit fileSavedToDisk(absPath);
            }
        }
    }
    m_statusLabel->setText(QString("Saved %1 file(s) into %2").arg(savedCount).arg(QDir::toNativeSeparators(targetRoot)));
}
