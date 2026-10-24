// src/ui/ContextInspectorDock.cpp
#include "ContextInspectorDock.h"
#include "MarkdownRenderer.h"
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
#include <QSizePolicy>

ContextInspectorDock::ContextInspectorDock(ProjectManager* projMgr, QWidget* parent)
    : QDockWidget("AI Context Inspector", parent), m_projectMgr(projMgr) {
    setObjectName("ContextInspectorDock");
    // Позволяем доку свободно сжиматься сплиттером до компактной ширины
    setMinimumWidth(220);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto* tabs = new QTabWidget(this);
    tabs->setMinimumWidth(0);
    tabs->setUsesScrollButtons(true);

    // Вкладка 1: Документ (верхние 2/3 — Document Summary, нижняя 1/3 — Document Prompt)
    auto* docSplit = new QSplitter(Qt::Vertical, tabs);
    docSplit->setMinimumWidth(0);
    docSplit->setChildrenCollapsible(false);

    auto* docTop = new QWidget(docSplit);
    docTop->setMinimumWidth(0);
    auto* docTopLay = new QVBoxLayout(docTop);
    docTopLay->setContentsMargins(6, 6, 6, 6);

    m_fileHeaderLabel = new QLabel("Document: None", docTop);
    m_fileHeaderLabel->setStyleSheet("font-weight: bold;");
    m_fileHeaderLabel->setWordWrap(true);
    m_fileHeaderLabel->setMinimumWidth(0);
    m_fileHeaderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_fileMetaLabel = new QLabel("LLM: - | SHA-256: -", docTop);
    m_fileMetaLabel->setStyleSheet("color: #888899; font-size: 11px;");
    m_fileMetaLabel->setWordWrap(true);
    m_fileMetaLabel->setMinimumWidth(0);
    m_fileMetaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* docSumHdr = new QHBoxLayout();
    auto* docSumTitle = new QLabel("Document Summary:", docTop);
    docSumTitle->setMinimumWidth(0);
    docSumTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_clearFileBtn = new QPushButton("Clear", docTop);
    m_clearFileBtn->setToolTip("Clear summary text while marking summary as created (empty)");
    m_rebuildFileBtn = new QPushButton("Regenerate", docTop);
    docSumHdr->addWidget(docSumTitle, 1);
    docSumHdr->addWidget(m_clearFileBtn);
    docSumHdr->addWidget(m_rebuildFileBtn);

    m_fileSummaryView = new QTextEdit(docTop);
    m_fileSummaryView->setMinimumWidth(0);
    m_fileSummaryView->setReadOnly(true);
    m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on first open...");
    setupMarkdownViewerStyle(m_fileSummaryView);

    docTopLay->addWidget(m_fileHeaderLabel);
    docTopLay->addWidget(m_fileMetaLabel);
    docTopLay->addLayout(docSumHdr);
    docTopLay->addWidget(m_fileSummaryView, 1);

    auto* docBot = new QWidget(docSplit);
    docBot->setMinimumWidth(0);
    auto* docBotLay = new QVBoxLayout(docBot);
    docBotLay->setContentsMargins(6, 6, 6, 6);
    auto* docPromptLbl = new QLabel("Document Prompt / Instructions:", docBot);
    docPromptLbl->setMinimumWidth(0);
    docPromptLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_filePromptEdit = new QPlainTextEdit(docBot);
    m_filePromptEdit->setMinimumWidth(0);
    m_filePromptEdit->setPlaceholderText("Enter specific AI instructions for this file...");
    docBotLay->addWidget(docPromptLbl);
    docBotLay->addWidget(m_filePromptEdit, 1);

    docSplit->setStretchFactor(0, 2);
    docSplit->setStretchFactor(1, 1);
    docSplit->setSizes({420, 210});
    tabs->addTab(docSplit, "Document");

    // Вкладка 2: Проект (верхние 2/3 — Project Summary, нижняя 1/3 — Project System Prompt)
    auto* projSplit = new QSplitter(Qt::Vertical, tabs);
    projSplit->setMinimumWidth(0);
    projSplit->setChildrenCollapsible(false);

    auto* projTop = new QWidget(projSplit);
    projTop->setMinimumWidth(0);
    auto* projTopLay = new QVBoxLayout(projTop);
    projTopLay->setContentsMargins(6, 6, 6, 6);
    auto* projSumHdr = new QHBoxLayout();
    auto* projSumTitle = new QLabel("Project Summary:", projTop);
    projSumTitle->setMinimumWidth(0);
    projSumTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_editProjSummaryBtn = new QPushButton("Edit MD", projTop);
    m_editProjSummaryBtn->setCheckable(true);
    m_rebuildProjBtn = new QPushButton("Rebuild", projTop);
    m_rebuildProjBtn->setToolTip("Rebuild Project Architecture Summary");
    projSumHdr->addWidget(projSumTitle, 1);
    projSumHdr->addWidget(m_editProjSummaryBtn);
    projSumHdr->addWidget(m_rebuildProjBtn);

    m_projectSummaryView = new QTextEdit(projTop);
    m_projectSummaryView->setMinimumWidth(0);
    m_projectSummaryView->setReadOnly(true);
    m_projectSummaryView->setPlaceholderText("Synthesized project architecture summary...");
    setupMarkdownViewerStyle(m_projectSummaryView);
    projTopLay->addLayout(projSumHdr);
    projTopLay->addWidget(m_projectSummaryView, 1);

    auto* projBot = new QWidget(projSplit);
    projBot->setMinimumWidth(0);
    auto* projBotLay = new QVBoxLayout(projBot);
    projBotLay->setContentsMargins(6, 6, 6, 6);
    auto* projPromptLbl = new QLabel("Project System Prompt (Global):", projBot);
    projPromptLbl->setMinimumWidth(0);
    projPromptLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_projectPromptEdit = new QPlainTextEdit(projBot);
    m_projectPromptEdit->setMinimumWidth(0);
    m_projectPromptEdit->setPlaceholderText("Global project instructions (e.g. Use C++20, Qt6 style, no exceptions)...");
    projBotLay->addWidget(projPromptLbl);
    projBotLay->addWidget(m_projectPromptEdit, 1);

    projSplit->setStretchFactor(0, 2);
    projSplit->setStretchFactor(1, 1);
    projSplit->setSizes({420, 210});
    tabs->addTab(projSplit, "Project");

    // Вкладка 3: Итоговый контекст LLM
    auto* effPage = new QWidget(tabs);
    effPage->setMinimumWidth(0);
    auto* effLay = new QVBoxLayout(effPage);
    effLay->setContentsMargins(6, 6, 6, 6);
    m_tokenEstimateLabel = new QLabel("Estimated size: 0 chars (~0 tokens)", effPage);
    m_tokenEstimateLabel->setMinimumWidth(0);
    m_tokenEstimateLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_effectiveContextPreview = new QTextEdit(effPage);
    m_effectiveContextPreview->setMinimumWidth(0);
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
            m_editProjSummaryBtn->setText("Edit MD");
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

    connect(m_clearFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) {
            QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
            m_liveFileMarkdownByPath.remove(rel);
            m_fileSummaryView->clear();
            emit clearFileSummaryClicked(m_activeFilePath);
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
    MarkdownRenderer::configureViewer(viewer);
}

void ContextInspectorDock::renderMarkdownPreservingScroll(QTextEdit* viewer, const QString& markdownText, bool scrollToBottom) {
    QScrollBar* vbar = viewer->verticalScrollBar();
    int oldScroll = vbar->value();
    int oldMax = vbar->maximum();
    bool wasAtBottom = (oldMax == 0) || (oldScroll >= oldMax - 6);

    if (markdownText.trimmed().isEmpty()) {
        viewer->clear();
    } else {
        viewer->setHtml(MarkdownRenderer::toHtml(markdownText));
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
    m_clearFileBtn->setEnabled(hasFile);

    if (hasFile) {
        auto fc = m_projectMgr->fileContext(m_activeFilePath);
        QString skipReasonMd;
        bool summaryAllowed = ConfigManager::instance().shouldGenerateSummaryForFile(m_activeFilePath, &skipReasonMd);
        bool isClearedEmpty = fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && !fc.isSummaryPending;

        QString statusSuffix = fc.isSummaryPending ? " [Indexing...]"
                               : isClearedEmpty ? " [Summary Empty]"
                                                   : (!summaryAllowed && fc.aiSummary.isEmpty() ? " [Summary Skipped]" : "");
        m_fileHeaderLabel->setText(QString("Document: %1%2").arg(fc.relativePath, statusSuffix));
        m_fileHeaderLabel->setToolTip(fc.relativePath);

        QString llmInfo = fc.isSummaryPending
                              ? (fc.pendingLlmModel.isEmpty() ? "-" : fc.pendingLlmModel + " (pending)")
                              : (fc.llmModel.isEmpty() ? (isClearedEmpty ? "cleared (empty)" : (summaryAllowed ? "-" : "Skipped by settings")) : fc.llmModel);
        QString shaInfo = fc.isSummaryPending
                              ? (fc.pendingSha256.isEmpty() ? "-" : fc.pendingSha256.left(12))
                              : (fc.sha256.isEmpty() ? "-" : fc.sha256.left(12));
        m_fileMetaLabel->setText(QString("LLM: %1 | SHA-256: %2").arg(llmInfo, shaInfo));

        if (m_filePromptEdit->toPlainText() != fc.userPrompt)
            m_filePromptEdit->setPlainText(fc.userPrompt);

        if (!fc.isSummaryPending) {
            m_liveFileMarkdownByPath.remove(fc.relativePath);
            if (!fc.aiSummary.trimmed().isEmpty()) {
                m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on first open...");
                renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary);
            } else if (isClearedEmpty) {
                m_fileSummaryView->clear();
                m_fileSummaryView->setPlaceholderText("Summary is created and intentionally empty (cleared). Click 'Regenerate' to rebuild.");
            } else if (!summaryAllowed) {
                renderMarkdownPreservingScroll(m_fileSummaryView, skipReasonMd);
            } else {
                m_fileSummaryView->clear();
                m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on first open...");
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
        m_fileHeaderLabel->setToolTip({});
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
