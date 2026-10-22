// src/ui/ContextInspectorDock.cpp
#include "ContextInspectorDock.h"
#include "../core/ProjectManager.h"
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
    auto* tabs = new QTabWidget(this);

    // Вкладка 1: Контекст текущего документа
    auto* docSplit = new QSplitter(Qt::Vertical, tabs);
    auto* docTop = new QWidget(docSplit);
    auto* docTopLay = new QVBoxLayout(docTop);
    docTopLay->setContentsMargins(6, 6, 6, 6);
    m_fileHeaderLabel = new QLabel("Document: None", docTop);
    m_fileHeaderLabel->setStyleSheet("font-weight: bold;");
    m_filePromptEdit = new QPlainTextEdit(docTop);
    m_filePromptEdit->setPlaceholderText("Enter specific AI instructions for this file...");
    docTopLay->addWidget(m_fileHeaderLabel);
    docTopLay->addWidget(new QLabel("Document Prompt / Instructions:", docTop));
    docTopLay->addWidget(m_filePromptEdit);

    auto* docBot = new QWidget(docSplit);
    auto* docBotLay = new QVBoxLayout(docBot);
    docBotLay->setContentsMargins(6, 6, 6, 6);
    auto* docSumHdr = new QHBoxLayout();
    m_rebuildFileBtn = new QPushButton("Regenerate", docBot);
    docSumHdr->addWidget(new QLabel("Document AI Summary (Markdown):", docBot));
    docSumHdr->addStretch();
    docSumHdr->addWidget(m_rebuildFileBtn);

    m_fileSummaryView = new QTextEdit(docBot);
    m_fileSummaryView->setReadOnly(true);
    m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on open/save...");
    setupMarkdownViewerStyle(m_fileSummaryView);

    docBotLay->addLayout(docSumHdr);
    docBotLay->addWidget(m_fileSummaryView);
    tabs->addTab(docSplit, "Document");

    // Вкладка 2: Глобальный контекст проекта
    auto* projSplit = new QSplitter(Qt::Vertical, tabs);
    auto* projTop = new QWidget(projSplit);
    auto* projTopLay = new QVBoxLayout(projTop);
    projTopLay->setContentsMargins(6, 6, 6, 6);
    m_projectPromptEdit = new QPlainTextEdit(projTop);
    m_projectPromptEdit->setPlaceholderText("Global project instructions (e.g. Use C++20, Qt6 style, no exceptions)...");
    projTopLay->addWidget(new QLabel("Project System Prompt (Global):", projTop));
    projTopLay->addWidget(m_projectPromptEdit);

    auto* projBot = new QWidget(projSplit);
    auto* projBotLay = new QVBoxLayout(projBot);
    projBotLay->setContentsMargins(6, 6, 6, 6);
    auto* projSumHdr = new QHBoxLayout();
    m_editProjSummaryBtn = new QPushButton("Edit Raw MD", projBot);
    m_editProjSummaryBtn->setCheckable(true);
    m_rebuildProjBtn = new QPushButton("Rebuild Project Summary", projBot);
    projSumHdr->addWidget(new QLabel("Project Architecture Summary (Markdown):", projBot));
    projSumHdr->addStretch();
    projSumHdr->addWidget(m_editProjSummaryBtn);
    projSumHdr->addWidget(m_rebuildProjBtn);

    m_projectSummaryView = new QTextEdit(projBot);
    m_projectSummaryView->setReadOnly(true);
    m_projectSummaryView->setPlaceholderText("Synthesized project architecture summary...");
    setupMarkdownViewerStyle(m_projectSummaryView);

    projBotLay->addLayout(projSumHdr);
    projBotLay->addWidget(m_projectSummaryView);
    tabs->addTab(projSplit, "Project");

    // Вкладка 3: Итоговый контекст, отправляемый в LLM
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

    // Переключение между просмотром отформатированного Markdown и редактированием исходного текста
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
            m_liveFileMarkdown.clear();
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

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString&) { refreshDisplay(); });
    connect(m_projectMgr, &ProjectManager::projectContextChanged, this, &ContextInspectorDock::refreshDisplay);
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
    int oldScroll = viewer->verticalScrollBar()->value();
    if (markdownText.trimmed().isEmpty()) {
        viewer->clear();
    } else {
        viewer->setMarkdown(markdownText);
    }
    if (scrollToBottom) {
        viewer->verticalScrollBar()->setValue(viewer->verticalScrollBar()->maximum());
    } else {
        viewer->verticalScrollBar()->setValue(oldScroll);
    }
}

void ContextInspectorDock::setActiveFile(const QString& filePath) {
    if (m_activeFilePath != filePath) {
        m_activeFilePath = filePath;
        m_liveFileMarkdown.clear();
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
    } else if (!m_activeFilePath.isEmpty() && m_projectMgr->toRelativePath(m_activeFilePath) == relFilePath) {
        m_liveFileMarkdown += chunk;
        renderMarkdownPreservingScroll(m_fileSummaryView, m_liveFileMarkdown, true);
    }
    m_updatingUi = false;
}

void ContextInspectorDock::refreshDisplay() {
    m_updatingUi = true;
    if (m_projectPromptEdit->toPlainText() != m_projectMgr->projectSystemPrompt())
        m_projectPromptEdit->setPlainText(m_projectMgr->projectSystemPrompt());

    if (!m_editingProjectMarkdown && m_liveProjectMarkdown.isEmpty()) {
        renderMarkdownPreservingScroll(m_projectSummaryView, m_projectMgr->projectSummary());
    } else if (!m_projectMgr->isProjectSummaryDirty() && !m_projectMgr->projectSummary().isEmpty()) {
        m_liveProjectMarkdown.clear();
        if (!m_editingProjectMarkdown)
            renderMarkdownPreservingScroll(m_projectSummaryView, m_projectMgr->projectSummary());
    }

    bool hasFile = !m_activeFilePath.isEmpty();
    m_filePromptEdit->setEnabled(hasFile);
    m_rebuildFileBtn->setEnabled(hasFile);

    if (hasFile) {
        auto fc = m_projectMgr->fileContext(m_activeFilePath);
        m_fileHeaderLabel->setText(QString("Document: %1%2").arg(fc.relativePath, fc.isSummaryPending ? " [Indexing...]" : ""));
        if (m_filePromptEdit->toPlainText() != fc.userPrompt)
            m_filePromptEdit->setPlainText(fc.userPrompt);

        if (!fc.isSummaryPending) {
            m_liveFileMarkdown.clear();
            renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary);
        } else if (m_liveFileMarkdown.isEmpty()) {
            m_fileSummaryView->clear();
        }
    } else {
        m_fileHeaderLabel->setText("Document: None (Unsaved or Closed)");
        m_filePromptEdit->clear();
        m_fileSummaryView->clear();
        m_liveFileMarkdown.clear();
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
