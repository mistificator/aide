// src/ui/ContextInspectorDock.cpp
#include "ContextInspectorDock.h"
#include "MarkdownRenderer.h"
#include "../core/ProjectManager.h"
#include "../core/ConfigManager.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextDocument>
#include <QScrollBar>
#include <QPushButton>
#include <QSplitter>
#include <QSizePolicy>
#include <QDir>

ContextInspectorDock::ContextInspectorDock(ProjectManager* projMgr, QWidget* parent)
    : QDockWidget("AI Context Inspector", parent), m_projectMgr(projMgr) {
    setObjectName("ContextInspectorDock");
    setMinimumWidth(220);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    m_refreshDebounceTimer.setSingleShot(true);
    m_refreshDebounceTimer.setInterval(25);
    connect(&m_refreshDebounceTimer, &QTimer::timeout, this, &ContextInspectorDock::refreshDisplay);

    auto* tabs = new QTabWidget(this);
    tabs->setMinimumWidth(0);
    tabs->setUsesScrollButtons(true);

    // Вкладка 1: Document
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
    docSumHdr->setSpacing(4);
    auto* docSumTitle = new QLabel("Summary:", docTop);
    docSumTitle->setMinimumWidth(0);
    docSumTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_removeFileBtn = new QPushButton("Remove", docTop);
    m_removeFileBtn->setToolTip("Completely remove file summary (reset to unindexed state)");
    m_clearFileBtn = new QPushButton("Clear", docTop);
    m_clearFileBtn->setToolTip("Clear summary text while marking summary as created (empty)");
    m_continueFileBtn = new QPushButton("Continue", docTop);
    m_continueFileBtn->setToolTip("Continue / expand current file summary preserving previous context");
    m_rebuildFileBtn = new QPushButton("Regenerate", docTop);
    m_rebuildFileBtn->setToolTip("Rebuild file summary from scratch");

    docSumHdr->addWidget(docSumTitle, 1);
    docSumHdr->addWidget(m_removeFileBtn);
    docSumHdr->addWidget(m_clearFileBtn);
    docSumHdr->addWidget(m_continueFileBtn);
    docSumHdr->addWidget(m_rebuildFileBtn);

    m_fileSummaryView = new QTextEdit(docTop);
    m_fileSummaryView->setMinimumWidth(0);
    m_fileSummaryView->setReadOnly(true);
    m_fileSummaryView->setPlaceholderText("AI summary for this document...");
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

    // Вкладка 2: Project (с выбором корневого проекта или подпроекта .aide)
    auto* projSplit = new QSplitter(Qt::Vertical, tabs);
    projSplit->setMinimumWidth(0);
    projSplit->setChildrenCollapsible(false);

    auto* projTop = new QWidget(projSplit);
    projTop->setMinimumWidth(0);
    auto* projTopLay = new QVBoxLayout(projTop);
    projTopLay->setContentsMargins(6, 6, 6, 6);

    auto* scopeRow = new QHBoxLayout();
    scopeRow->setSpacing(4);
    auto* scopeLbl = new QLabel("Target (.aide):", projTop);
    scopeLbl->setStyleSheet("font-weight: bold;");
    m_projectScopeCombo = new QComboBox(projTop);
    m_projectScopeCombo->setMinimumWidth(0);
    m_projectScopeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_projectScopeCombo->setToolTip("Select Root Project or a nested Subproject (.aide)");
    scopeRow->addWidget(scopeLbl);
    scopeRow->addWidget(m_projectScopeCombo, 1);

    m_projectMetaLabel = new QLabel("LLM: - | Indexed Files: 0", projTop);
    m_projectMetaLabel->setStyleSheet("color: #888899; font-size: 11px;");
    m_projectMetaLabel->setWordWrap(true);
    m_projectMetaLabel->setMinimumWidth(0);
    m_projectMetaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* projSumHdr = new QHBoxLayout();
    projSumHdr->setSpacing(4);
    auto* projSumTitle = new QLabel("Project Summary:", projTop);
    projSumTitle->setMinimumWidth(0);
    projSumTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_editProjSummaryBtn = new QPushButton("Edit MD", projTop);
    m_editProjSummaryBtn->setCheckable(true);
    m_continueProjBtn = new QPushButton("Continue", projTop);
    m_continueProjBtn->setToolTip("Continue / expand summary for the selected project/subproject");
    m_rebuildProjBtn = new QPushButton("Rebuild", projTop);
    m_rebuildProjBtn->setToolTip("Rebuild Architecture Summary for the selected project/subproject");

    projSumHdr->addWidget(projSumTitle, 1);
    projSumHdr->addWidget(m_editProjSummaryBtn);
    projSumHdr->addWidget(m_continueProjBtn);
    projSumHdr->addWidget(m_rebuildProjBtn);

    m_projectSummaryView = new QTextEdit(projTop);
    m_projectSummaryView->setMinimumWidth(0);
    m_projectSummaryView->setReadOnly(true);
    m_projectSummaryView->setPlaceholderText("Synthesized project / subproject architecture summary...");
    setupMarkdownViewerStyle(m_projectSummaryView);

    projTopLay->addLayout(scopeRow);
    projTopLay->addWidget(m_projectMetaLabel);
    projTopLay->addLayout(projSumHdr);
    projTopLay->addWidget(m_projectSummaryView, 1);

    auto* projBot = new QWidget(projSplit);
    projBot->setMinimumWidth(0);
    auto* projBotLay = new QVBoxLayout(projBot);
    projBotLay->setContentsMargins(6, 6, 6, 6);
    auto* projPromptLbl = new QLabel("Project / Subproject System Prompt:", projBot);
    projPromptLbl->setMinimumWidth(0);
    projPromptLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_projectPromptEdit = new QPlainTextEdit(projBot);
    m_projectPromptEdit->setMinimumWidth(0);
    m_projectPromptEdit->setPlaceholderText("Instructions for the selected project or subproject (e.g. Use C++20, Qt6 style)...");
    projBotLay->addWidget(projPromptLbl);
    projBotLay->addWidget(m_projectPromptEdit, 1);

    projSplit->setStretchFactor(0, 2);
    projSplit->setStretchFactor(1, 1);
    projSplit->setSizes({420, 210});
    tabs->addTab(projSplit, "Project");

    // Вкладка 3: Effective Context
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

    connect(m_projectScopeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updatingUi || idx < 0) return;
        if (m_editProjSummaryBtn->isChecked()) {
            m_editProjSummaryBtn->setChecked(false);
        }
        m_selectedProjectScope = m_projectScopeCombo->itemData(idx).toString();
        refreshDisplay();
    });

    connect(m_filePromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi || m_activeFilePath.isEmpty()) return;
        m_projectMgr->updateFileUserPrompt(m_activeFilePath, m_filePromptEdit->toPlainText());
        updateEffectivePreview();
    });

    connect(m_projectPromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi) return;
        m_projectMgr->setProjectSystemPrompt(m_projectPromptEdit->toPlainText(), m_selectedProjectScope);
        updateEffectivePreview();
    });

    connect(m_editProjSummaryBtn, &QPushButton::toggled, this, [this](bool editMode) {
        m_editingProjectMarkdown = editMode;
        m_updatingUi = true;
        m_projectSummaryView->setReadOnly(!editMode);
        if (editMode) {
            m_editProjSummaryBtn->setText("Apply MD");
            m_projectSummaryView->setPlainText(m_projectMgr->projectSummary(m_selectedProjectScope));
        } else {
            m_editProjSummaryBtn->setText("Edit MD");
            QString updatedMd = m_projectSummaryView->toPlainText();
            m_projectMgr->setProjectSummary(updatedMd, {}, m_selectedProjectScope);
            m_cachedProjectSummaryMd.clear();
            renderMarkdownPreservingScroll(m_projectSummaryView, updatedMd, m_cachedProjectSummaryMd);
            updateEffectivePreview();
        }
        m_updatingUi = false;
    });

    connect(m_projectSummaryView, &QTextEdit::textChanged, this, [this]() {
        if (m_updatingUi || !m_editingProjectMarkdown) return;
        m_projectMgr->setProjectSummary(m_projectSummaryView->toPlainText(), {}, m_selectedProjectScope);
        updateEffectivePreview();
    });

    connect(m_rebuildFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) {
            QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
            m_liveFileMarkdownByPath.remove(rel);
            m_cachedFileSummaryMd.clear();
            m_fileSummaryView->clear();
            emit rebuildFileSummaryClicked(m_activeFilePath);
        }
    });

    connect(m_continueFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) {
            emit continueFileSummaryClicked(m_activeFilePath);
        }
    });

    connect(m_clearFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) {
            QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
            m_liveFileMarkdownByPath.remove(rel);
            m_cachedFileSummaryMd.clear();
            m_fileSummaryView->clear();
            emit clearFileSummaryClicked(m_activeFilePath);
        }
    });

    connect(m_removeFileBtn, &QPushButton::clicked, this, [this]() {
        if (!m_activeFilePath.isEmpty()) {
            QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
            m_liveFileMarkdownByPath.remove(rel);
            m_cachedFileSummaryMd.clear();
            m_fileSummaryView->clear();
            emit removeFileSummaryClicked(m_activeFilePath);
        }
    });

    connect(m_rebuildProjBtn, &QPushButton::clicked, this, [this]() {
        if (m_editProjSummaryBtn->isChecked()) m_editProjSummaryBtn->setChecked(false);
        m_liveProjectMarkdownByScope.remove(m_selectedProjectScope);
        m_cachedProjectSummaryMd.clear();
        m_projectSummaryView->clear();
        emit rebuildProjectSummaryClicked(m_selectedProjectScope);
    });

    connect(m_continueProjBtn, &QPushButton::clicked, this, [this]() {
        if (m_editProjSummaryBtn->isChecked()) m_editProjSummaryBtn->setChecked(false);
        emit continueProjectSummaryClicked(m_selectedProjectScope);
    });

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString& relPath) {
        if (!m_projectMgr->fileContext(relPath).isSummaryPending) {
            m_liveFileMarkdownByPath.remove(relPath);
        }
        scheduleRefreshDisplay();
    });

    connect(m_projectMgr, &ProjectManager::projectContextChanged, this, [this]() {
        scheduleRefreshDisplay();
    });

    connect(m_projectMgr, &ProjectManager::subprojectsChanged, this, [this]() {
        rebuildProjectScopeCombo();
        scheduleRefreshDisplay();
    });

    connect(&ConfigManager::instance(), &ConfigManager::aiBehaviorChanged, this, [this](const AiBehaviorConfig&) {
        scheduleRefreshDisplay();
    });

    rebuildProjectScopeCombo();
}

void ContextInspectorDock::scheduleRefreshDisplay() {
    if (!m_refreshDebounceTimer.isActive()) {
        m_refreshDebounceTimer.start();
    }
}

void ContextInspectorDock::setupMarkdownViewerStyle(QTextEdit* viewer) {
    MarkdownRenderer::configureViewer(viewer);
}

void ContextInspectorDock::renderMarkdownPreservingScroll(QTextEdit* viewer,
                                                          const QString& markdownText,
                                                          QString& cachedMarkdown,
                                                          bool scrollToBottom) {
    if (markdownText.trimmed().isEmpty()) {
        if (!cachedMarkdown.isEmpty() || !viewer->document()->isEmpty()) {
            cachedMarkdown.clear();
            viewer->clear();
        }
        return;
    }

    if (cachedMarkdown == markdownText) {
        return;
    }
    cachedMarkdown = markdownText;

    QScrollBar* vbar = viewer->verticalScrollBar();
    int oldScroll = vbar->value();
    int oldMax = vbar->maximum();
    bool wasAtBottom = (oldMax == 0) || (oldScroll >= oldMax - 6);

    viewer->setHtml(MarkdownRenderer::toHtml(markdownText));

    if (scrollToBottom && wasAtBottom) {
        vbar->setValue(vbar->maximum());
    } else {
        vbar->setValue(oldScroll);
    }
}

void ContextInspectorDock::rebuildProjectScopeCombo() {
    if (!m_projectScopeCombo) return;
    const bool prevUpdating = m_updatingUi;
    m_updatingUi = true;

    m_projectScopeCombo->clear();
    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) {
        m_projectScopeCombo->addItem("No Project Open", QString());
        m_projectScopeCombo->setEnabled(false);
        m_selectedProjectScope.clear();
        m_updatingUi = prevUpdating;
        return;
    }

    m_projectScopeCombo->setEnabled(true);
    QString rootDirName = QDir(m_projectMgr->projectRootPath()).dirName();
    if (rootDirName.isEmpty()) rootDirName = m_projectMgr->projectRootPath();
    m_projectScopeCombo->addItem(QString("Root Project (%1)").arg(rootDirName), QString());

    const QStringList subs = m_projectMgr->subprojectRelPaths();
    int targetIdx = 0;
    for (int i = 0; i < subs.size(); ++i) {
        const QString& subRel = subs[i];
        m_projectScopeCombo->addItem(QString("Subproject: %1").arg(subRel), subRel);
        if (subRel == m_selectedProjectScope) {
            targetIdx = i + 1;
        }
    }

    if (targetIdx == 0 && !m_selectedProjectScope.isEmpty()) {
        m_selectedProjectScope.clear();
    }
    m_projectScopeCombo->setCurrentIndex(targetIdx);
    m_updatingUi = prevUpdating;
}

void ContextInspectorDock::setSelectedProjectScope(const QString& subprojectRelPath) {
    m_selectedProjectScope = subprojectRelPath.trimmed();
    rebuildProjectScopeCombo();
    refreshDisplay();
}

QString ContextInspectorDock::selectedProjectScope() const {
    return m_selectedProjectScope;
}

void ContextInspectorDock::setActiveFile(const QString& filePath) {
    m_activeFilePath = filePath;
    if (!filePath.isEmpty() && m_projectMgr && m_projectMgr->hasOpenProject()) {
        const QString ownerSub = m_projectMgr->owningProjectRelPathForFile(filePath);
        if (m_selectedProjectScope != ownerSub) {
            m_selectedProjectScope = ownerSub;
        }
    }
    rebuildProjectScopeCombo();
    refreshDisplay();
}

void ContextInspectorDock::resetLiveSummary(const QString& targetRelPath, bool isProjectSummary) {
    if (isProjectSummary) {
        m_liveProjectMarkdownByScope.remove(targetRelPath);
        if (targetRelPath == m_selectedProjectScope) {
            m_cachedProjectSummaryMd.clear();
        }
    } else if (!targetRelPath.isEmpty()) {
        m_liveFileMarkdownByPath.remove(targetRelPath);
        if (!m_activeFilePath.isEmpty() && m_projectMgr->toRelativePath(m_activeFilePath) == targetRelPath) {
            m_cachedFileSummaryMd.clear();
        }
    }
    scheduleRefreshDisplay();
}

void ContextInspectorDock::appendLiveSummaryChunk(const QString& targetRelPath, bool isProjectSummary, const QString& chunk) {
    m_updatingUi = true;
    if (isProjectSummary) {
        QString& liveProjBuf = m_liveProjectMarkdownByScope[targetRelPath];
        liveProjBuf += chunk;
        if (!m_editingProjectMarkdown && targetRelPath == m_selectedProjectScope) {
            renderMarkdownPreservingScroll(m_projectSummaryView, liveProjBuf, m_cachedProjectSummaryMd, true);
        }
    } else if (!targetRelPath.isEmpty()) {
        QString& liveBuf = m_liveFileMarkdownByPath[targetRelPath];
        liveBuf += chunk;
        if (!m_activeFilePath.isEmpty() && m_projectMgr->toRelativePath(m_activeFilePath) == targetRelPath) {
            renderMarkdownPreservingScroll(m_fileSummaryView, liveBuf, m_cachedFileSummaryMd, true);
        }
    }
    m_updatingUi = false;
}

void ContextInspectorDock::refreshDisplay() {
    m_updatingUi = true;
    rebuildProjectScopeCombo();

    const bool hasProj = (m_projectMgr && m_projectMgr->hasOpenProject());
    m_projectPromptEdit->setEnabled(hasProj);
    m_rebuildProjBtn->setEnabled(hasProj);
    m_editProjSummaryBtn->setEnabled(hasProj);

    const QString scopeSysPrompt = hasProj ? m_projectMgr->projectSystemPrompt(m_selectedProjectScope) : QString();
    if (m_projectPromptEdit->toPlainText() != scopeSysPrompt) {
        m_projectPromptEdit->setPlainText(scopeSysPrompt);
    }

    const QString projSum = hasProj ? m_projectMgr->projectSummary(m_selectedProjectScope) : QString();
    const QString projLlm = hasProj ? m_projectMgr->projectSummaryLlm(m_selectedProjectScope) : QString();
    const int indexedInScope = hasProj ? m_projectMgr->indexedFilesCount(m_selectedProjectScope, true) : 0;
    const int directChildSubs = hasProj ? m_projectMgr->directChildSubprojects(m_selectedProjectScope).size() : 0;
    const bool isDirty = hasProj && m_projectMgr->isProjectSummaryDirty(m_selectedProjectScope);

    m_projectMetaLabel->setText(
        QString("LLM: %1 | Indexed Files: %2 | Subprojects: %3%4")
            .arg(projLlm.isEmpty() ? "-" : projLlm)
            .arg(indexedInScope)
            .arg(directChildSubs)
            .arg(isDirty ? " [Outdated / Dirty]" : ""));

    m_continueProjBtn->setEnabled(hasProj && !projSum.trimmed().isEmpty());

    if (!m_editingProjectMarkdown) {
        const QString liveProjMd = m_liveProjectMarkdownByScope.value(m_selectedProjectScope);
        if (!liveProjMd.isEmpty()) {
            renderMarkdownPreservingScroll(m_projectSummaryView, liveProjMd, m_cachedProjectSummaryMd, true);
        } else {
            renderMarkdownPreservingScroll(m_projectSummaryView, projSum, m_cachedProjectSummaryMd);
        }
    }

    bool hasFile = !m_activeFilePath.isEmpty();
    m_filePromptEdit->setEnabled(hasFile);
    m_rebuildFileBtn->setEnabled(hasFile);
    m_clearFileBtn->setEnabled(hasFile);

    if (hasFile) {
        auto fc = m_projectMgr->fileContext(m_activeFilePath);
        bool hasAnySummaryState = fc.summaryCreated || !fc.aiSummary.isEmpty() || fc.isSummaryPending;
        m_removeFileBtn->setEnabled(hasAnySummaryState);
        m_continueFileBtn->setEnabled(!fc.aiSummary.trimmed().isEmpty() && !fc.isSummaryPending);

        QString skipReasonMd;
        bool summaryAllowed = ConfigManager::instance().shouldGenerateSummaryForFile(m_activeFilePath, &skipReasonMd);
        bool isClearedEmpty = fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && !fc.isSummaryPending;

        QString statusSuffix = fc.isSummaryPending ? " [Indexing...]"
                               : isClearedEmpty ? " [Summary Empty]"
                                                   : (!summaryAllowed && fc.aiSummary.isEmpty() ? " [Summary Skipped]" : "");
        m_fileHeaderLabel->setText(QString("Document: %1%2").arg(fc.relativePath, statusSuffix));
        m_fileHeaderLabel->setToolTip(fc.relativePath);

        QString ownerInfo = fc.subprojectRelPath.isEmpty()
                                ? QStringLiteral("Root")
                                : QString("Subproject (%1)").arg(fc.subprojectRelPath);
        QString llmInfo = fc.isSummaryPending
                              ? (fc.pendingLlmModel.isEmpty() ? "-" : fc.pendingLlmModel + " (pending)")
                              : (fc.llmModel.isEmpty() ? (isClearedEmpty ? "cleared (empty)" : (summaryAllowed ? "-" : "Skipped by settings")) : fc.llmModel);
        QString shaInfo = fc.isSummaryPending
                              ? (fc.pendingSha256.isEmpty() ? "-" : fc.pendingSha256.left(12))
                              : (fc.sha256.isEmpty() ? "-" : fc.sha256.left(12));
        m_fileMetaLabel->setText(QString("Owner: %1 | LLM: %2 | SHA-256: %3").arg(ownerInfo, llmInfo, shaInfo));

        if (m_filePromptEdit->toPlainText() != fc.userPrompt) {
            m_filePromptEdit->setPlainText(fc.userPrompt);
        }

        if (!fc.isSummaryPending) {
            m_liveFileMarkdownByPath.remove(fc.relativePath);
            if (!fc.aiSummary.trimmed().isEmpty()) {
                m_fileSummaryView->setPlaceholderText("Click 'Regenerate' to build AI summary for this file...");
                renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary, m_cachedFileSummaryMd);
            } else if (isClearedEmpty) {
                m_cachedFileSummaryMd.clear();
                m_fileSummaryView->clear();
                m_fileSummaryView->setPlaceholderText("Summary is created and intentionally empty (cleared). Click 'Regenerate' to rebuild or 'Remove' to reset.");
            } else if (!summaryAllowed) {
                renderMarkdownPreservingScroll(m_fileSummaryView, skipReasonMd, m_cachedFileSummaryMd);
            } else {
                m_cachedFileSummaryMd.clear();
                m_fileSummaryView->clear();
                m_fileSummaryView->setPlaceholderText("No summary yet. Click 'Regenerate' to build AI summary for this file...");
            }
        } else {
            const QString liveMd = m_liveFileMarkdownByPath.value(fc.relativePath);
            if (!liveMd.isEmpty()) {
                renderMarkdownPreservingScroll(m_fileSummaryView, liveMd, m_cachedFileSummaryMd, true);
            } else {
                renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary, m_cachedFileSummaryMd);
            }
        }
    } else {
        m_removeFileBtn->setEnabled(false);
        m_continueFileBtn->setEnabled(false);
        m_fileHeaderLabel->setText("Document: None (Unsaved or Closed)");
        m_fileHeaderLabel->setToolTip({});
        m_fileMetaLabel->setText("Owner: - | LLM: - | SHA-256: -");
        m_filePromptEdit->clear();
        m_cachedFileSummaryMd.clear();
        m_fileSummaryView->clear();
    }

    updateEffectivePreview();
    m_updatingUi = false;
}

void ContextInspectorDock::updateEffectivePreview() {
    QString ctx = m_projectMgr->buildEffectiveSystemContext(m_activeFilePath);
    renderMarkdownPreservingScroll(m_effectiveContextPreview, ctx, m_cachedEffectiveMd);
    int chars = ctx.length(), tokens = (chars + 3) / 4;
    m_tokenEstimateLabel->setText(QString("Estimated size: %1 chars (~%2 tokens)").arg(chars).arg(tokens));
}
