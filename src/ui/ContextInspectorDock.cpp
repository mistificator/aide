// src/ui/ContextInspectorDock.cpp
#include "ContextInspectorDock.h"
#include "../core/ProjectManager.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
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
    docSumHdr->addWidget(new QLabel("Document AI Summary:", docBot));
    docSumHdr->addStretch();
    docSumHdr->addWidget(m_rebuildFileBtn);
    m_fileSummaryView = new QPlainTextEdit(docBot);
    m_fileSummaryView->setReadOnly(true);
    m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on open/save...");
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
    m_rebuildProjBtn = new QPushButton("Rebuild Project Summary", projBot);
    projSumHdr->addWidget(new QLabel("Project Architecture Summary:", projBot));
    projSumHdr->addStretch();
    projSumHdr->addWidget(m_rebuildProjBtn);
    m_projectSummaryView = new QPlainTextEdit(projBot);
    m_projectSummaryView->setPlaceholderText("Synthesized project architecture summary...");
    projBotLay->addLayout(projSumHdr);
    projBotLay->addWidget(m_projectSummaryView);
    tabs->addTab(projSplit, "Project");

    // Вкладка 3: Итоговый контекст, отправляемый в LLM
    auto* effPage = new QWidget(tabs);
    auto* effLay = new QVBoxLayout(effPage);
    effLay->setContentsMargins(6, 6, 6, 6);
    m_tokenEstimateLabel = new QLabel("Estimated size: 0 chars (~0 tokens)", effPage);
    m_effectiveContextPreview = new QPlainTextEdit(effPage);
    m_effectiveContextPreview->setReadOnly(true);
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
    connect(m_projectSummaryView, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi) return;
        m_projectMgr->setProjectSummary(m_projectSummaryView->toPlainText());
        updateEffectivePreview();
    });
    connect(m_rebuildFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) emit rebuildFileSummaryClicked(m_activeFilePath);
    });
    connect(m_rebuildProjBtn, &QPushButton::clicked, this, &ContextInspectorDock::rebuildProjectSummaryClicked);
    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString&) { refreshDisplay(); });
    connect(m_projectMgr, &ProjectManager::projectContextChanged, this, &ContextInspectorDock::refreshDisplay);
}

void ContextInspectorDock::setActiveFile(const QString& filePath) {
    m_activeFilePath = filePath;
    refreshDisplay();
}

void ContextInspectorDock::refreshDisplay() {
    m_updatingUi = true;
    if (m_projectPromptEdit->toPlainText() != m_projectMgr->projectSystemPrompt())
        m_projectPromptEdit->setPlainText(m_projectMgr->projectSystemPrompt());
    if (m_projectSummaryView->toPlainText() != m_projectMgr->projectSummary())
        m_projectSummaryView->setPlainText(m_projectMgr->projectSummary());

    bool hasFile = !m_activeFilePath.isEmpty();
    m_filePromptEdit->setEnabled(hasFile);
    m_rebuildFileBtn->setEnabled(hasFile);

    if (hasFile) {
        auto fc = m_projectMgr->fileContext(m_activeFilePath);
        m_fileHeaderLabel->setText(QString("Document: %1%2").arg(fc.relativePath, fc.isSummaryPending ? " [Indexing...]" : ""));
        if (m_filePromptEdit->toPlainText() != fc.userPrompt) m_filePromptEdit->setPlainText(fc.userPrompt);
        m_fileSummaryView->setPlainText(fc.aiSummary);
    } else {
        m_fileHeaderLabel->setText("Document: None (Unsaved or Closed)");
        m_filePromptEdit->clear();
        m_fileSummaryView->clear();
    }
    updateEffectivePreview();
    m_updatingUi = false;
}

void ContextInspectorDock::updateEffectivePreview() {
    QString ctx = m_projectMgr->buildEffectiveSystemContext(m_activeFilePath);
    m_effectiveContextPreview->setPlainText(ctx);
    int chars = ctx.length(), tokens = (chars + 3) / 4;
    m_tokenEstimateLabel->setText(QString("Estimated size: %1 chars (~%2 tokens)").arg(chars).arg(tokens));
}