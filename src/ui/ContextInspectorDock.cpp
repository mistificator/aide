// src/ui/ContextInspectorDock.cpp
#include "ContextInspectorDock.h"
#include "../core/ProjectManager.h"
#include "../core/ConfigManager.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextDocument>
#include <QScrollBar>
#include <QPushButton>
#include <QSplitter>

ContextInspectorDock::ContextInspectorDock(ProjectManager* projMgr, QWidget* parent)
    : QDockWidget("AI Context Inspector", parent), m_projectMgr(projMgr) {
    setObjectName("ContextInspectorDock");
    auto* tabs = new QTabWidget(this);

    // Вкладка 1: Документ (верхние 2/3 — Document Summary, нижняя 1/3 — Document Prompt)
    auto* docSplit = new QSplitter(Qt::Vertical, tabs);

    auto* docTop = new QWidget(docSplit);
    auto* docTopLay = new QVBoxLayout(docTop);
    docTopLay->setContentsMargins(6, 6, 6, 6);
    m_fileHeaderLabel = new QLabel("Document: None", docTop);
    m_fileHeaderLabel->setStyleSheet("font-weight: bold;");
    m_fileMetaLabel = new QLabel("LLM: - | SHA-256: -", docTop);
    m_fileMetaLabel->setStyleSheet("color: #888899; font-size: 11px;");

    auto* docSumHdr = new QHBoxLayout();
    m_rebuildFileBtn = new QPushButton("Regenerate", docTop);
    docSumHdr->addWidget(new QLabel("Document AI Summary (Markdown):", docTop));
    docSumHdr->addStretch();
    docSumHdr->addWidget(m_rebuildFileBtn);

    m_fileSummaryView = new QTextEdit(docTop);
    m_fileSummaryView->setReadOnly(true);
    m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on first open...");
    setupMarkdownViewerStyle(m_fileSummaryView);

    docTopLay->addWidget(m_fileHeaderLabel);
    docTopLay->addWidget(m_fileMetaLabel);
    docTopLay->addLayout(docSumHdr);
    docTopLay->addWidget(m_fileSummaryView, 1);

    auto* docBot = new QWidget(docSplit);
    auto* docBotLay = new QVBoxLayout(docBot);
    docBotLay->setContentsMargins(6, 6, 6, 6);
    m_filePromptEdit = new QPlainTextEdit(docBot);
    m_filePromptEdit->setPlaceholderText("Enter specific AI instructions for this file...");
    docBotLay->addWidget(new QLabel("Document Prompt / Instructions:", docBot));
    docBotLay->addWidget(m_filePromptEdit, 1);

    docSplit->setStretchFactor(0, 2);
    docSplit->setStretchFactor(1, 1);
    docSplit->setSizes({420, 210});
    tabs->addTab(docSplit, "Document");

    // Вкладка 2: Проект (верхние 2/3 — Project Summary, нижняя 1/3 — Project System Prompt)
    auto* projSplit = new QSplitter(Qt::Vertical, tabs);

    auto* projTop = new QWidget(projSplit);
    auto* projTopLay = new QVBoxLayout(projTop);
    projTopLay->setContentsMargins(6, 6, 6, 6);
    auto* projSumHdr = new QHBoxLayout();
    m_editProjSummaryBtn = new QPushButton("Edit Raw MD", projTop);
    m_editProjSummaryBtn->setCheckable(true);
    m_rebuildProjBtn = new QPushButton("Rebuild Project Summary", projTop);
    projSumHdr->addWidget(new QLabel("Project Architecture Summary (Markdown):", projTop));
    projSumHdr->addStretch();
    projSumHdr->addWidget(m_editProjSummaryBtn);
    projSumHdr->addWidget(m_rebuildProjBtn);

    m_projectSummaryView = new QTextEdit(projTop);
    m_projectSummaryView->setReadOnly(true);
    m_projectSummaryView->setPlaceholderText("Synthesized project architecture summary...");
    setupMarkdownViewerStyle(m_projectSummaryView);
    projTopLay->addLayout(projSumHdr);
    projTopLay->addWidget(m_projectSummaryView, 1);

    auto* projBot = new QWidget(projSplit);
    auto* projBotLay = new QVBoxLayout(projBot);
    projBotLay->setContentsMargins(6, 6, 6, 6);
    m_projectPromptEdit = new QPlainTextEdit(projBot);
    m_projectPromptEdit->setPlaceholderText("Global project instructions (e.g. Use C++20, Qt6 style, no exceptions)...");
    projBotLay->addWidget(new QLabel("Project System Prompt (Global):", projBot));
    projBotLay->addWidget(m_projectPromptEdit, 1);

    projSplit->setStretchFactor(0, 2);
    projSplit->setStretchFactor(1, 1);
    projSplit->setSizes({420, 210});
    tabs->addTab(projSplit, "Project");

    // Вкладка 3: Итоговый контекст LLM
    auto* effPage = new QWidget(tabs);
    auto* effLay = new QVBoxLayout(effPage);
    effLay->setContentsMargins(6, 6, 6, 6);
    m_tokenEstimateLabel = new QLabel("Estimated size: 0 chars (~0 tokens)", effPage);
    m_effectiveContextPreview = new QTextEdit(effPage);
    m_effectiveContextPreview->setReadOnly(true);
    setupMarkdownViewerStyle(m_effectiveContextPreview);
    effLay->addWidget(m_tokenEstimateLabel);
    effLay->addWidget(m_effectiveContextPreview);
    tabs->addTab(effPage, "Effective Context");

    setWidget(tabs);

    connect(m_filePromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi || m_activeFilePath.isEmpty()) return;
        m_projectMgr->updateFileUserPrompt(m_activeFilePath, m_filePromptEdit->toPlainText());
        updateEffectivePreview();
    });

    connect(m_projectPromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi) return;
        m_projectMgr->setProjectSystemPrompt(m_projectPromptEdit->toPlainText());
        updateEffectivePreview();
    });

    connect(m_editProjSummaryBtn, &QPushButton::toggled, this, [this](bool editMode) {
        m_editingProjectMarkdown = editMode;
        m_updatingUi = true;
        m_projectSummaryView->setReadOnly(!editMode);
        if (editMode) {
            m_editProjSummaryBtn->setText("Apply MD");
            m_projectSummaryView->setPlainText(m_projectMgr->projectSummary());
        } else {
            m_editProjSummaryBtn->setText("Edit Raw MD");
            QString updatedMd = m_projectSummaryView->toPlainText();
            m_projectMgr->setProjectSummary(updatedMd);
            renderMarkdownPreservingScroll(m_projectSummaryView, updatedMd);
            updateEffectivePreview();
        }
        m_updatingUi = false;
    });

    connect(m_projectSummaryView, &QTextEdit::textChanged, this, [this]() {
        if (m_updatingUi || !m_editingProjectMarkdown) return;
        m_projectMgr->setProjectSummary(m_projectSummaryView->toPlainText());
        updateEffectivePreview();
    });

    connect(m_rebuildFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) {
            QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
            m_liveFileMarkdownByPath.remove(rel);
            m_fileSummaryView->clear();
            emit rebuildFileSummaryClicked(m_activeFilePath);
        }
    });

    connect(m_rebuildProjBtn, &QPushButton::clicked, this, [this]() {
        if (m_editProjSummaryBtn->isChecked()) m_editProjSummaryBtn->setChecked(false);
        m_liveProjectMarkdown.clear();
        m_projectSummaryView->clear();
        emit rebuildProjectSummaryClicked();
    });

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString& relPath) {
        if (!m_projectMgr->fileContext(relPath).isSummaryPending) {
            m_liveFileMarkdownByPath.remove(relPath);
        }
        refreshDisplay();
    });
    connect(m_projectMgr, &ProjectManager::projectContextChanged, this, [this]() {
        m_liveProjectMarkdown.clear();
        refreshDisplay();
    });
    connect(&ConfigManager::instance(), &ConfigManager::aiBehaviorChanged, this, [this](const AiBehaviorConfig&) {
        refreshDisplay();
    });
}

void ContextInspectorDock::setupMarkdownViewerStyle(QTextEdit* viewer) {
    viewer->document()->setIndentWidth(16);
    viewer->document()->setDefaultStyleSheet(
        "h1, h2, h3, h4 { color: #bd93f9; margin-top: 8px; margin-bottom: 4px; }"
        "code, pre { font-family: monospace; background-color: rgba(128, 128, 150, 0.18); }"
        "li { margin-bottom: 2px; }"
        );
}

void ContextInspectorDock::renderMarkdownPreservingScroll(QTextEdit* viewer, const QString& markdownText, bool scrollToBottom) {
    QScrollBar* vbar = viewer->verticalScrollBar();
    int oldScroll = vbar->value();
    int oldMax = vbar->maximum();
    // Прокручиваем вниз только если ползунок уже находился в самом низу (пользователь не отмотал текст вверх)
    bool wasAtBottom = (oldMax == 0) || (oldScroll >= oldMax - 6);

    if (markdownText.trimmed().isEmpty()) {
        viewer->clear();
    } else {
        viewer->setMarkdown(markdownText);
    }

    if (scrollToBottom && wasAtBottom) {
        vbar->setValue(vbar->maximum());
    } else {
        vbar->setValue(oldScroll);
    }
}

void ContextInspectorDock::setActiveFile(const QString& filePath) {
    m_activeFilePath = filePath;
    refreshDisplay();
}

void ContextInspectorDock::resetLiveSummary(const QString& relFilePath, bool isProjectSummary) {
    if (isProjectSummary) {
        m_liveProjectMarkdown.clear();
    } else if (!relFilePath.isEmpty()) {
        m_liveFileMarkdownByPath.remove(relFilePath);
    }
    refreshDisplay();
}

void ContextInspectorDock::appendLiveSummaryChunk(const QString& relFilePath, bool isProjectSummary, const QString& chunk) {
    m_updatingUi = true;
    if (isProjectSummary) {
        m_liveProjectMarkdown += chunk;
        if (!m_editingProjectMarkdown) {
            renderMarkdownPreservingScroll(m_projectSummaryView, m_liveProjectMarkdown, true);
        }
    } else if (!relFilePath.isEmpty()) {
        QString& liveBuf = m_liveFileMarkdownByPath[relFilePath];
        liveBuf += chunk;
        if (!m_activeFilePath.isEmpty() && m_projectMgr->toRelativePath(m_activeFilePath) == relFilePath) {
            renderMarkdownPreservingScroll(m_fileSummaryView, liveBuf, true);
        }
    }
    m_updatingUi = false;
}

void ContextInspectorDock::refreshDisplay() {
    m_updatingUi = true;

    if (m_projectPromptEdit->toPlainText() != m_projectMgr->projectSystemPrompt())
        m_projectPromptEdit->setPlainText(m_projectMgr->projectSystemPrompt());

    if (!m_editingProjectMarkdown) {
        if (!m_liveProjectMarkdown.isEmpty()) {
            renderMarkdownPreservingScroll(m_projectSummaryView, m_liveProjectMarkdown, true);
        } else {
            renderMarkdownPreservingScroll(m_projectSummaryView, m_projectMgr->projectSummary());
        }
    }

    bool hasFile = !m_activeFilePath.isEmpty();
    m_filePromptEdit->setEnabled(hasFile);
    m_rebuildFileBtn->setEnabled(hasFile);

    if (hasFile) {
        auto fc = m_projectMgr->fileContext(m_activeFilePath);
        QString skipReasonMd;
        bool summaryAllowed = ConfigManager::instance().shouldGenerateSummaryForFile(m_activeFilePath, &skipReasonMd);

        QString statusSuffix = fc.isSummaryPending ? " [Indexing...]"
                                                   : (!summaryAllowed && fc.aiSummary.isEmpty() ? " [Summary Skipped]" : "");
        m_fileHeaderLabel->setText(QString("Document: %1%2").arg(fc.relativePath, statusSuffix));

        QString llmInfo = fc.isSummaryPending
                              ? (fc.pendingLlmModel.isEmpty() ? "-" : fc.pendingLlmModel + " (pending)")
                              : (fc.llmModel.isEmpty() ? (summaryAllowed ? "-" : "Skipped by settings") : fc.llmModel);
        QString shaInfo = fc.isSummaryPending
                              ? (fc.pendingSha256.isEmpty() ? "-" : fc.pendingSha256.left(12))
                              : (fc.sha256.isEmpty() ? "-" : fc.sha256.left(12));
        m_fileMetaLabel->setText(QString("LLM: %1 | SHA-256: %2").arg(llmInfo, shaInfo));

        if (m_filePromptEdit->toPlainText() != fc.userPrompt)
            m_filePromptEdit->setPlainText(fc.userPrompt);

        if (!fc.isSummaryPending) {
            m_liveFileMarkdownByPath.remove(fc.relativePath);
            if (!fc.aiSummary.trimmed().isEmpty()) {
                renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary);
            } else if (!summaryAllowed) {
                renderMarkdownPreservingScroll(m_fileSummaryView, skipReasonMd);
            } else {
                m_fileSummaryView->clear();
            }
        } else {
            const QString liveMd = m_liveFileMarkdownByPath.value(fc.relativePath);
            if (!liveMd.isEmpty()) {
                renderMarkdownPreservingScroll(m_fileSummaryView, liveMd, true);
            } else {
                renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary);
            }
        }
    } else {
        m_fileHeaderLabel->setText("Document: None (Unsaved or Closed)");
        m_fileMetaLabel->setText("LLM: - | SHA-256: -");
        m_filePromptEdit->clear();
        m_fileSummaryView->clear();
    }

    updateEffectivePreview();
    m_updatingUi = false;
}

void ContextInspectorDock::updateEffectivePreview() {
    QString ctx = m_projectMgr->buildEffectiveSystemContext(m_activeFilePath);
    renderMarkdownPreservingScroll(m_effectiveContextPreview, ctx);
    int chars = ctx.length(), tokens = (chars + 3) / 4;
    m_tokenEstimateLabel->setText(QString("Estimated size: %1 chars (~%2 tokens)").arg(chars).arg(tokens));
}
