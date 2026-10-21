// src/editor/CodeEditor.cpp
#include "CodeEditor.h"
#include "../syntax/LexicalHighlighter.h"
#include "../core/ConfigManager.h"
#include <QPainter>
#include <QTextBlock>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QMenu>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFontDatabase>
#include <QRegularExpression>

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent),
      m_lineNumberArea(new LineNumberArea(this)),
      m_highlighter(new LexicalHighlighter(document())),
      m_scheme(ColorScheme::loadByName(ConfigManager::instance().activeTheme())) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    setupOverlayBanner();
    applyColorScheme(m_scheme);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(&ConfigManager::instance(), &ConfigManager::themeChanged, this, [this](const QString& t) {
        applyColorScheme(ColorScheme::loadByName(t));
    });

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

void CodeEditor::setupOverlayBanner() {
    m_busyBanner = new QWidget(this);
    m_busyBanner->setStyleSheet("background-color: rgba(30, 31, 41, 235); border-bottom: 1px solid #ff79c6;");
    auto* layout = new QHBoxLayout(m_busyBanner);
    layout->setContentsMargins(12, 4, 12, 4);

    m_busyLabel = new QLabel("AI is processing...", m_busyBanner);
    m_busyLabel->setStyleSheet("color: #f8f8f2; font-weight: bold; border: none; background: transparent;");

    m_cancelBtn = new QPushButton("Cancel", m_busyBanner);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet("QPushButton { background: #ff5555; color: #ffffff; border: none; padding: 3px 10px; border-radius: 3px; font-weight: bold; } QPushButton:hover { background: #ff6e6e; }");

    layout->addWidget(m_busyLabel);
    layout->addStretch();
    layout->addWidget(m_cancelBtn);
    m_busyBanner->hide();

    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() { emit cancelAiRequested(this); });
}

bool CodeEditor::loadFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    m_filePath = QFileInfo(filePath).absoluteFilePath();
    setPlainText(QString::fromUtf8(f.readAll()));
    document()->setModified(false);
    m_highlighter->setLanguageByFilePath(m_filePath);
    return true;
}

bool CodeEditor::saveFile() {
    if (m_filePath.isEmpty()) return false;
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QByteArray raw = toPlainText().toUtf8();
    f.write(raw);
    if (!f.commit()) return false;
    document()->setModified(false);
    emit fileSaved(m_filePath, raw);
    return true;
}

bool CodeEditor::saveFileAs(const QString& newFilePath) {
    m_filePath = QFileInfo(newFilePath).absoluteFilePath();
    m_highlighter->setLanguageByFilePath(m_filePath);
    return saveFile();
}

QString CodeEditor::displayName() const {
    return m_filePath.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_filePath).fileName();
}

void CodeEditor::applyColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    QPalette p = palette();
    p.setColor(QPalette::Base, m_scheme.editorBg);
    p.setColor(QPalette::Text, m_scheme.editorFg);
    p.setColor(QPalette::Highlight, m_scheme.selectionBg);
    p.setColor(QPalette::HighlightedText, m_scheme.editorFg);
    setPalette(p);
    m_highlighter->setColorScheme(m_scheme);
    highlightCurrentLine();
    m_lineNumberArea->update();
}

void CodeEditor::setSyntaxLanguage(const QString& langId) { m_highlighter->setLanguage(langId); }
QString CodeEditor::syntaxLanguage() const { return m_highlighter->language(); }

void CodeEditor::setAiBusy(bool busy, const QString& statusText) {
    m_isAiBusy = busy;
    setReadOnly(busy);
    if (busy) {
        m_busyLabel->setText(statusText.isEmpty() ? "AI is processing..." : statusText);
        m_busyBanner->setGeometry(lineNumberAreaWidth(), 0, width() - lineNumberAreaWidth(), 32);
        m_busyBanner->show();
        m_busyBanner->raise();
    } else m_busyBanner->hide();
    updateLineNumberAreaWidth();
    emit busyStateChanged(this, busy);
}

void CodeEditor::beginAiStreamEdit() {
    m_streamCursor = textCursor();
    if (!m_streamCursor.hasSelection()) m_streamCursor.select(QTextCursor::Document);
    m_isFirstChunk = true;
    m_headerResolved = false;
    m_headerBuf.clear();
    m_tailBuf.clear();
}

void CodeEditor::appendAiStreamChunk(const QString& chunk) {
    if (chunk.isEmpty()) return;
    QString data = chunk;

    // Отсечение стартовой строки ```lang\n, если LLM вернула Markdown-блок
    if (!m_headerResolved) {
        m_headerBuf += data;
        if (m_headerBuf.startsWith("```")) {
            int nl = m_headerBuf.indexOf('\n');
            if (nl == -1) return;
            data = m_headerBuf.mid(nl + 1);
            m_headerResolved = true;
        } else if (m_headerBuf.size() >= 3 || !QStringLiteral("```").startsWith(m_headerBuf)) {
            data = m_headerBuf;
            m_headerResolved = true;
        } else return;
    }

    // Удержание последних 8 символов в буфере для перехвата закрывающего ```
    m_tailBuf += data;
    if (m_tailBuf.size() <= 8) return;
    QString toInsert = m_tailBuf.left(m_tailBuf.size() - 8);
    m_tailBuf = m_tailBuf.right(8);

    if (toInsert.isEmpty()) return;
    if (m_isFirstChunk) {
        m_streamCursor.beginEditBlock();
        m_streamCursor.removeSelectedText();
        m_streamCursor.insertText(toInsert);
        m_streamCursor.endEditBlock();
        m_isFirstChunk = false;
    } else {
        m_streamCursor.joinPreviousEditBlock();
        m_streamCursor.insertText(toInsert);
        m_streamCursor.endEditBlock();
    }
    ensureCursorVisible();
}

void CodeEditor::finishAiStreamEdit() {
    QString rem = m_headerResolved ? m_tailBuf : m_headerBuf;
    if (!m_headerResolved && rem.startsWith("```")) {
        int nl = rem.indexOf('\n');
        rem = (nl != -1) ? rem.mid(nl + 1) : QString();
    }
    rem.replace(QRegularExpression(R"(\n?```\s*$)"), "");

    if (!rem.isEmpty()) {
        if (m_isFirstChunk) {
            m_streamCursor.beginEditBlock();
            m_streamCursor.removeSelectedText();
            m_streamCursor.insertText(rem);
            m_streamCursor.endEditBlock();
            m_isFirstChunk = false;
        } else {
            m_streamCursor.joinPreviousEditBlock();
            m_streamCursor.insertText(rem);
            m_streamCursor.endEditBlock();
        }
    }
    m_headerBuf.clear();
    m_tailBuf.clear();
    setAiBusy(false);
}

void CodeEditor::cancelAndRollbackAiEdit() {
    bool modifiedByStream = !m_isFirstChunk;
    m_headerBuf.clear();
    m_tailBuf.clear();
    m_isFirstChunk = true;
    setAiBusy(false);
    if (modifiedByStream && document()->isUndoAvailable()) undo();
}

void CodeEditor::triggerAiAction(AiEditAction action) {
    if (m_isAiBusy) return;
    QString custom;
    if (action == AiEditAction::CustomPrompt) {
        bool ok = false;
        custom = QInputDialog::getMultiLineText(this, "AI Custom Instruction", "Enter instructions for AI:", {}, &ok);
        if (!ok || custom.trimmed().isEmpty()) return;
    }
    QString sel = textCursor().hasSelection() ? textCursor().selectedText().replace(QChar::ParagraphSeparator, '\n') : toPlainText();
    emit aiActionRequested(this, action, sel, custom);
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    menu->addSeparator();
    QMenu* aiMenu = menu->addMenu("AI");
    aiMenu->setEnabled(!m_isAiBusy);

    const QList<QPair<QString, AiEditAction>> actions = {
        {"Explain Code", AiEditAction::ExplainCode},
        {"Refactor && Clean Up", AiEditAction::RefactorCode},
        {"Find Bugs && Edge Cases", AiEditAction::FindBugs},
        {"Optimize Performance", AiEditAction::OptimizePerformance},
        {"Add Documentation / Comments", AiEditAction::AddDocumentation},
        {"Generate Unit Tests", AiEditAction::GenerateUnitTests},
        {"Complete / Continue Code", AiEditAction::CompleteCode},
        {"Custom Prompt...", AiEditAction::CustomPrompt}
    };
    for (const auto& [title, act] : actions) {
        if (act == AiEditAction::CustomPrompt) aiMenu->addSeparator();
        aiMenu->addAction(title, this, [this, act]() { triggerAiAction(act); });
    }
    menu->exec(event->globalPos());
    delete menu;
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1, max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(digits, 3);
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), m_isAiBusy ? 32 : 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    if (m_isAiBusy) m_busyBanner->setGeometry(cr.left() + lineNumberAreaWidth(), cr.top(), cr.width() - lineNumberAreaWidth(), 32);
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extra;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(m_scheme.currentLineBg);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        extra.append(sel);
    }
    setExtraSelections(extra);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_scheme.lineNumberBg);
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(m_scheme.lineNumberFg);
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}