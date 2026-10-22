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
#include <QContextMenuEvent>
#include <QResizeEvent>

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent),
    m_lineNumberArea(new LineNumberArea(this)),
    m_highlighter(new LexicalHighlighter(document())) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    setupOverlayBanner();

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(&ConfigManager::instance(), &ConfigManager::themeChanged, this, [this](const QString& name) {
        applyColorScheme(ColorScheme::loadByName(name));
    });

    applyColorScheme(ColorScheme::loadByName(ConfigManager::instance().activeTheme()));
    updateLineNumberAreaWidth(0);
}

void CodeEditor::setupOverlayBanner() {
    m_busyBanner = new QWidget(this);
    m_busyBanner->setStyleSheet(
        "QWidget { background-color: rgba(40, 42, 54, 235); border: 1px solid #bd93f9; border-radius: 6px; }"
        "QLabel { color: #f8f8f2; font-weight: bold; border: none; background: transparent; }"
        "QPushButton { background-color: #ff5555; color: #ffffff; border: none; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: #ff6e6e; }");

    auto* layout = new QHBoxLayout(m_busyBanner);
    layout->setContentsMargins(12, 6, 12, 6);
    m_busyLabel = new QLabel("AI: Processing...", m_busyBanner);
    m_cancelBtn = new QPushButton("Cancel", m_busyBanner);
    layout->addWidget(m_busyLabel, 1);
    layout->addWidget(m_cancelBtn);

    m_busyBanner->hide();
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() { emit cancelAiRequested(this); });
}

void CodeEditor::applyColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    m_highlighter->setColorScheme(scheme);
    setStyleSheet(QString("QPlainTextEdit { background-color: %1; color: %2; selection-background-color: %3; }")
                      .arg(scheme.editorBg.name(), scheme.editorFg.name(), scheme.selectionBg.name()));
    highlightCurrentLine();
    m_lineNumberArea->update();
}

void CodeEditor::setSyntaxLanguage(const QString& langId) { m_highlighter->setLanguage(langId); }
QString CodeEditor::syntaxLanguage() const { return m_highlighter->language(); }

bool CodeEditor::loadFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    m_filePath = filePath;
    m_savedDiskContent = QString::fromUtf8(f.readAll());
    setPlainText(m_savedDiskContent);
    document()->setModified(false);
    m_highlighter->setLanguageByFilePath(filePath);
    return true;
}

bool CodeEditor::saveFile() {
    return m_filePath.isEmpty() ? false : saveFileAs(m_filePath);
}

bool CodeEditor::saveFileAs(const QString& newFilePath) {
    QSaveFile f(newFilePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QString currentText = toPlainText();
    QByteArray raw = currentText.toUtf8();
    f.write(raw);
    if (!f.commit()) return false;
    m_filePath = newFilePath;
    m_savedDiskContent = currentText;
    document()->setModified(false);
    m_highlighter->setLanguageByFilePath(newFilePath);
    emit fileSaved(m_filePath, raw);
    return true;
}

QString CodeEditor::displayName() const {
    return m_filePath.isEmpty() ? "Untitled" : QFileInfo(m_filePath).fileName();
}

void CodeEditor::setAiBusy(bool busy, const QString& statusText) {
    m_isAiBusy = busy;
    setReadOnly(busy);
    if (busy) {
        m_busyLabel->setText("AI: " + (statusText.isEmpty() ? "Generating..." : statusText));
        int w = qMin(420, viewport()->width() - 24);
        m_busyBanner->setGeometry(viewport()->width() - w - 16, 12, w, 40);
        m_busyBanner->show();
        m_busyBanner->raise();
    } else {
        m_busyBanner->hide();
    }
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
    auto commitText = [this](const QString& txt) {
        if (txt.isEmpty()) return;
        if (m_isFirstChunk) {
            m_streamCursor.beginEditBlock();
            m_streamCursor.removeSelectedText();
            m_streamCursor.insertText(txt);
            m_streamCursor.endEditBlock();
            m_isFirstChunk = false;
        } else {
            m_streamCursor.joinPreviousEditBlock();
            m_streamCursor.insertText(txt);
            m_streamCursor.endEditBlock();
        }
        setTextCursor(m_streamCursor);
        ensureCursorVisible();
    };

    QString pending = chunk;
    if (!m_headerResolved) {
        m_headerBuf += pending;
        if (m_headerBuf.startsWith("```")) {
            int nl = m_headerBuf.indexOf('\n');
            if (nl == -1) return;
            pending = m_headerBuf.mid(nl + 1);
            m_headerResolved = true;
        } else if (m_headerBuf.length() >= 3 || !QString("```").startsWith(m_headerBuf)) {
            pending = m_headerBuf;
            m_headerResolved = true;
        } else return;
    }

    m_tailBuf += pending;
    if (m_tailBuf.length() > 12) {
        int flushLen = m_tailBuf.length() - 12;
        commitText(m_tailBuf.left(flushLen));
        m_tailBuf.remove(0, flushLen);
    }
}

void CodeEditor::finishAiStreamEdit() {
    if (!m_headerResolved && !m_headerBuf.isEmpty()) m_tailBuf += m_headerBuf;
    QString trimmed = m_tailBuf.trimmed();
    if (trimmed.endsWith("```")) {
        int fenceIdx = m_tailBuf.lastIndexOf("```");
        m_tailBuf = m_tailBuf.left(fenceIdx);
        if (m_tailBuf.endsWith('\n')) m_tailBuf.chop(1);
    }
    if (!m_tailBuf.isEmpty()) {
        if (m_isFirstChunk) {
            m_streamCursor.beginEditBlock();
            m_streamCursor.removeSelectedText();
            m_streamCursor.insertText(m_tailBuf);
            m_streamCursor.endEditBlock();
            m_isFirstChunk = false;
        } else {
            m_streamCursor.joinPreviousEditBlock();
            m_streamCursor.insertText(m_tailBuf);
            m_streamCursor.endEditBlock();
        }
    }
    m_tailBuf.clear();
    setAiBusy(false);
}

void CodeEditor::cancelAndRollbackAiEdit() {
    m_tailBuf.clear();
    m_headerBuf.clear();
    setAiBusy(false);
    if (!m_isFirstChunk) undo();
}

void CodeEditor::triggerAiAction(AiEditAction action) {
    if (m_isAiBusy) return;
    QString customInst;
    if (action == AiEditAction::CustomPrompt) {
        bool ok = false;
        QString lastPrompt = ConfigManager::instance().lastCustomPrompt();
        customInst = QInputDialog::getMultiLineText(
            this,
            "AI Custom Prompt",
            "Enter instructions for the LLM:",
            lastPrompt,
            &ok);
        if (!ok || customInst.trimmed().isEmpty()) return;
        customInst = customInst.trimmed();
        ConfigManager::instance().setLastCustomPrompt(customInst);
    }

    QTextCursor c = textCursor();
    QString targetText = c.hasSelection() ? c.selectedText().replace(QChar(0x2029), '\n') : toPlainText();
    if (targetText.trimmed().isEmpty() && action != AiEditAction::CustomPrompt && action != AiEditAction::CompleteCode)
        return;

    emit aiActionRequested(this, action, targetText, customInst);
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    menu->addSeparator();
    QAction* diffAct = menu->addAction("Compare Unsaved Changes (Diff)...", this, [this]() {
        emit diffRequested(this);
    });
    diffAct->setEnabled(document()->isModified() || !m_filePath.isEmpty());

    menu->addSeparator();
    QMenu* aiMenu = menu->addMenu("AI ▸");
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
    return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(digits, 3);
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
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
    if (m_busyBanner->isVisible()) {
        int w = qMin(420, viewport()->width() - 24);
        m_busyBanner->setGeometry(viewport()->width() - w - 16, 12, w, 40);
    }
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(m_scheme.currentLineBg);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    extraSelections.append(selection);
    setExtraSelections(extraSelections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_scheme.lineNumberBg);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(m_scheme.lineNumberFg);
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
