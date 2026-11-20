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
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFontDatabase>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextOption>

namespace {

bool showWrappedPromptDialog(QWidget* parent,
                             const QString& windowTitle,
                             const QString& labelText,
                             const QString& initialText,
                             QString* outText) {
    QDialog dlg(parent);
    dlg.setWindowTitle(windowTitle);
    dlg.resize(620, 320);

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    auto* lbl = new QLabel(labelText, &dlg);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);

    auto* edit = new QPlainTextEdit(&dlg);
    edit->setPlainText(initialText);
    edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    edit->setPlaceholderText("Enter instructions for the LLM...");
    edit->selectAll();
    lay->addWidget(edit, 1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto* okBtn = btns->button(QDialogButtonBox::Ok)) {
        okBtn->setText("Run Prompt");
    }
    lay->addWidget(btns);

    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    edit->setFocus();

    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }

    if (outText) {
        *outText = edit->toPlainText().trimmed();
    }
    return true;
}

} // namespace

QString aiEditActionKey(AiEditAction action) {
    switch (action) {
    case AiEditAction::ExplainCode:         return "ExplainCode";
    case AiEditAction::RefactorCode:        return "RefactorCode";
    case AiEditAction::FindBugs:            return "FindBugs";
    case AiEditAction::OptimizePerformance: return "OptimizePerformance";
    case AiEditAction::AddDocumentation:    return "AddDocumentation";
    case AiEditAction::GenerateUnitTests:   return "GenerateUnitTests";
    case AiEditAction::CompleteCode:        return "CompleteCode";
    case AiEditAction::CustomPrompt:        return "CustomPrompt";
    }
    return "CustomPrompt";
}

QString aiEditActionLabel(AiEditAction action, ProjectMode projectMode) {
    if (projectMode == ProjectMode::Documents) {
        switch (action) {
        case AiEditAction::ExplainCode:         return QStringLiteral("Explain Text");
        case AiEditAction::RefactorCode:        return QStringLiteral("Edit && Improve Text");
        case AiEditAction::FindBugs:            return QStringLiteral("Check Consistency && Gaps");
        case AiEditAction::OptimizePerformance: return QStringLiteral("Improve Clarity && Flow");
        case AiEditAction::AddDocumentation:    return QStringLiteral("Add Annotations / Notes");
        case AiEditAction::GenerateUnitTests:   return QStringLiteral("Generate Review Questions");
        case AiEditAction::CompleteCode:        return QStringLiteral("Continue Document / Chapter");
        case AiEditAction::CustomPrompt:        return QStringLiteral("Custom Prompt...");
        }
    }

    switch (action) {
    case AiEditAction::ExplainCode:         return QStringLiteral("Explain Code");
    case AiEditAction::RefactorCode:        return QStringLiteral("Refactor && Clean Up");
    case AiEditAction::FindBugs:            return QStringLiteral("Find Bugs && Edge Cases");
    case AiEditAction::OptimizePerformance: return QStringLiteral("Optimize Performance");
    case AiEditAction::AddDocumentation:    return QStringLiteral("Add Documentation / Comments");
    case AiEditAction::GenerateUnitTests:   return QStringLiteral("Generate Unit Tests");
    case AiEditAction::CompleteCode:        return QStringLiteral("Complete / Continue Code");
    case AiEditAction::CustomPrompt:        return QStringLiteral("Custom Prompt...");
    }
    return QStringLiteral("Custom Prompt...");
}

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

    // Отслеживаем ручную прокрутку во время стриминга, чтобы не сдёргивать скролл вниз
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (!m_isAiBusy || m_updatingStreamText) return;
        QScrollBar* vbar = verticalScrollBar();
        if (value < m_lastAutoScrollValue) {
            m_userScrolledUpDuringStream = true;
        } else if (value >= vbar->maximum() - 2 ||
                   cursorRect(m_streamCursor).top() <= viewport()->rect().bottom()) {
            m_userScrolledUpDuringStream = false;
            m_lastAutoScrollValue = value;
        }
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

void CodeEditor::replaceContentUnsaved(const QString& newContent) {
    if (toPlainText() == newContent) return;
    QTextCursor c(document());
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(newContent);
    c.endEditBlock();
    document()->setModified(true);
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
    m_userScrolledUpDuringStream = false;
    m_updatingStreamText = false;
    m_lastAutoScrollValue = verticalScrollBar()->value();
    m_headerBuf.clear();
    m_tailBuf.clear();
}

void CodeEditor::beginAiStreamContinueEdit() {
    if (m_streamCursor.isNull()) {
        m_streamCursor = textCursor();
        m_streamCursor.movePosition(QTextCursor::End);
    } else {
        m_streamCursor.clearSelection();
    }
    // Продолжаем присоединять к предыдущему блоку Undo
    m_isFirstChunk = false;
    m_headerResolved = false;
    m_userScrolledUpDuringStream = false;
    m_updatingStreamText = false;
    m_lastAutoScrollValue = verticalScrollBar()->value();
    m_headerBuf.clear();
    m_tailBuf.clear();
}

void CodeEditor::appendAiStreamChunk(const QString& chunk) {
    auto commitText = [this](const QString& txt) {
        if (txt.isEmpty()) return;
        m_updatingStreamText = true;
        QScrollBar* vbar = verticalScrollBar();
        QScrollBar* hbar = horizontalScrollBar();
        int savedV = vbar->value();
        int savedH = hbar->value();

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

        if (!m_userScrolledUpDuringStream) {
            setTextCursor(m_streamCursor);
            ensureCursorVisible();
            m_lastAutoScrollValue = vbar->value();
        } else {
            vbar->setValue(savedV);
            hbar->setValue(savedH);
        }
        m_updatingStreamText = false;
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
        m_updatingStreamText = true;
        QScrollBar* vbar = verticalScrollBar();
        QScrollBar* hbar = horizontalScrollBar();
        int savedV = vbar->value();
        int savedH = hbar->value();

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

        if (!m_userScrolledUpDuringStream) {
            setTextCursor(m_streamCursor);
            ensureCursorVisible();
        } else {
            vbar->setValue(savedV);
            hbar->setValue(savedH);
        }
        m_updatingStreamText = false;
    }
    m_tailBuf.clear();
    m_userScrolledUpDuringStream = false;
    setAiBusy(false);
}

void CodeEditor::cancelAndRollbackAiEdit() {
    m_tailBuf.clear();
    m_headerBuf.clear();
    m_userScrolledUpDuringStream = false;
    setAiBusy(false);
    if (!m_isFirstChunk) undo();
}

void CodeEditor::triggerAiAction(AiEditAction action) {
    if (action == AiEditAction::CustomPrompt) {
        triggerCustomPromptWithText(ConfigManager::instance().lastCustomPrompt());
        return;
    }
    if (m_isAiBusy) return;

    QTextCursor c = textCursor();
    QString targetText = c.hasSelection() ? c.selectedText().replace(QChar(0x2029), '\n') : toPlainText();
    if (targetText.trimmed().isEmpty() && action != AiEditAction::CompleteCode)
        return;

    emit aiActionRequested(this, action, targetText, {});
}

void CodeEditor::triggerCustomPromptWithText(const QString& initialPromptText) {
    if (m_isAiBusy) return;

    QTextCursor c = textCursor();
    const bool hasSel = c.hasSelection();
    const bool documentMode = (m_projectMode == ProjectMode::Documents);
    const QString labelText = hasSel
                                  ? (documentMode
                                         ? QStringLiteral("Enter instructions for the LLM (will be applied to selected text):")
                                         : QStringLiteral("Enter instructions for the LLM (will be applied to selected code):"))
                                  : QStringLiteral("Enter instructions for the LLM (will be applied to current document):");

    QString customInst;
    if (!showWrappedPromptDialog(this,
                                 "AI Custom Prompt (Document)",
                                 labelText,
                                 initialPromptText,
                                 &customInst) ||
        customInst.isEmpty()) {
        return;
    }

    // Сохраняет промпт и автоматически поднимает его в глобальной истории по SHA-256
    ConfigManager::instance().setLastCustomPrompt(customInst);

    QString targetText = hasSel ? c.selectedText().replace(QChar(0x2029), '\n') : toPlainText();
    emit aiActionRequested(this, AiEditAction::CustomPrompt, targetText, customInst);
}

void CodeEditor::triggerContinueAiGeneration() {
    if (m_isAiBusy || m_lastAiHistory.isEmpty()) return;
    emit aiContinueRequested(this, {}, true);
}

void CodeEditor::triggerFollowUpAiPrompt() {
    if (m_isAiBusy || !hasLastAiContext()) return;

    QString followUpInst;
    if (!showWrappedPromptDialog(
            this,
            "Continue Last AI Request (Follow-up)",
            "Enter follow-up instructions for the LLM (previous conversation history is preserved):",
            ConfigManager::instance().lastCustomPrompt(),
            &followUpInst) ||
        followUpInst.isEmpty()) {
        return;
    }

    ConfigManager::instance().setLastCustomPrompt(followUpInst);
    emit aiContinueRequested(this, followUpInst, false);
}

void CodeEditor::setLastAiConversationContext(const QString& systemPrompt,
                                              const QVector<AiChatMessage>& history,
                                              AiEditAction lastAction) {
    m_lastAiSystemPrompt = systemPrompt;
    m_lastAiHistory = history;
    m_lastAiAction = lastAction;
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    menu->addSeparator();
    QAction* diffAct = menu->addAction("Compare Unsaved Changes (Diff)...", this, [this]() {
        emit diffRequested(this);
    });
    diffAct->setEnabled(document()->isModified() || !m_filePath.isEmpty());

    menu->addSeparator();
    QMenu* aiMenu = menu->addMenu("AI");
    aiMenu->setEnabled(!m_isAiBusy);

    const QList<AiEditAction> actions = {
        AiEditAction::ExplainCode,
        AiEditAction::RefactorCode,
        AiEditAction::FindBugs,
        AiEditAction::OptimizePerformance,
        AiEditAction::AddDocumentation,
        AiEditAction::GenerateUnitTests,
        AiEditAction::CompleteCode,
        AiEditAction::CustomPrompt
    };
    for (const AiEditAction act : actions) {
        if (act == AiEditAction::CustomPrompt) aiMenu->addSeparator();
        aiMenu->addAction(aiEditActionLabel(act, m_projectMode), this, [this, act]() { triggerAiAction(act); });
    }
    if (hasLastAiContext()) {
        aiMenu->addSeparator();
        aiMenu->addAction("Continue Last Generation (From End)", this, &CodeEditor::triggerContinueAiGeneration);
        aiMenu->addAction("Continue Last Request (Follow-up)...", this, &CodeEditor::triggerFollowUpAiPrompt);
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
