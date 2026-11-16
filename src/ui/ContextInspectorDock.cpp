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
#include <QTextCharFormat>
#include <QScrollBar>
#include <QPushButton>
#include <QComboBox>
#include <QSplitter>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QFile>
#include <QDir>

namespace {
const QString kEditToggleBtnStyle = QStringLiteral(
    "QPushButton:checked { background-color: #50fa7b; color: #191a24; font-weight: bold; }");
} // namespace

ContextInspectorDock::ContextInspectorDock(ProjectManager* projMgr, QWidget* parent)
    : QDockWidget("AI Context Inspector", parent), m_projectMgr(projMgr) {
    setObjectName("ContextInspectorDock");
    setMinimumWidth(220);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    m_refreshDebounceTimer.setSingleShot(true);
    m_refreshDebounceTimer.setInterval(25);
    connect(&m_refreshDebounceTimer, &QTimer::timeout, this, &ContextInspectorDock::refreshDisplay);

    m_tabs = new QTabWidget(this);
    m_tabs->setMinimumWidth(0);
    m_tabs->setUsesScrollButtons(true);

    // =========================================================================
    // Вкладка 1: Document
    // =========================================================================
    auto* docSplit = new QSplitter(Qt::Vertical, m_tabs);
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
    auto* docSumTitle = new QLabel("Document Summary:", docTop);
    docSumTitle->setMinimumWidth(0);
    docSumTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_editFileSummaryBtn = new QPushButton("Edit", docTop);
    m_editFileSummaryBtn->setCheckable(true);
    m_editFileSummaryBtn->setStyleSheet(kEditToggleBtnStyle);
    m_editFileSummaryBtn->setToolTip("Edit document summary (Markdown)");

    m_continueFileBtn = new QPushButton("Continue", docTop);
    m_continueFileBtn->setToolTip("Continue generating truncated document summary");

    m_clearFileBtn = new QPushButton("Clear", docTop);
    m_clearFileBtn->setToolTip("Clear summary text while marking summary as created (empty)");

    m_removeFileBtn = new QPushButton("Remove", docTop);
    m_removeFileBtn->setToolTip("Completely remove summary record for this file");

    m_rebuildFileBtn = new QPushButton("Regenerate", docTop);
    m_rebuildFileBtn->setToolTip("Regenerate AI summary for this file");

    docSumHdr->addWidget(docSumTitle, 1);
    docSumHdr->addWidget(m_editFileSummaryBtn);
    docSumHdr->addWidget(m_continueFileBtn);
    docSumHdr->addWidget(m_clearFileBtn);
    docSumHdr->addWidget(m_removeFileBtn);
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
    m_tabs->addTab(docSplit, "Document");

    // =========================================================================
    // Вкладка 2: Project
    // =========================================================================
    auto* projSplit = new QSplitter(Qt::Vertical, m_tabs);
    projSplit->setMinimumWidth(0);
    projSplit->setChildrenCollapsible(false);

    auto* projTop = new QWidget(projSplit);
    projTop->setMinimumWidth(0);
    auto* projTopLay = new QVBoxLayout(projTop);
    projTopLay->setContentsMargins(6, 6, 6, 6);

    auto* scopeRow = new QHBoxLayout();
    scopeRow->setSpacing(4);
    auto* scopeLbl = new QLabel("Scope:", projTop);
    scopeLbl->setStyleSheet("font-weight: bold;");
    m_projectScopeCombo = new QComboBox(projTop);
    m_projectScopeCombo->setMinimumWidth(0);
    m_projectScopeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_projectScopeCombo->setToolTip("Select Root Project or a Subproject to inspect/edit its summary and system prompt");
    scopeRow->addWidget(scopeLbl);
    scopeRow->addWidget(m_projectScopeCombo, 1);

    m_projectMetaLabel = new QLabel("LLM: - | Indexed files: 0", projTop);
    m_projectMetaLabel->setStyleSheet("color: #888899; font-size: 11px;");
    m_projectMetaLabel->setWordWrap(true);
    m_projectMetaLabel->setMinimumWidth(0);
    m_projectMetaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* projSumHdr = new QHBoxLayout();
    projSumHdr->setSpacing(4);
    auto* projSumTitle = new QLabel("Project Summary:", projTop);
    projSumTitle->setMinimumWidth(0);
    projSumTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_editProjSummaryBtn = new QPushButton("Edit", projTop);
    m_editProjSummaryBtn->setCheckable(true);
    m_editProjSummaryBtn->setStyleSheet(kEditToggleBtnStyle);
    m_editProjSummaryBtn->setToolTip("Edit project / subproject summary (Markdown)");

    m_continueProjBtn = new QPushButton("Continue", projTop);
    m_continueProjBtn->setToolTip("Continue generating truncated project / subproject summary");

    m_rebuildProjBtn = new QPushButton("Rebuild", projTop);
    m_rebuildProjBtn->setToolTip("Rebuild Project / Subproject Architecture Summary");

    projSumHdr->addWidget(projSumTitle, 1);
    projSumHdr->addWidget(m_editProjSummaryBtn);
    projSumHdr->addWidget(m_continueProjBtn);
    projSumHdr->addWidget(m_rebuildProjBtn);

    m_projectSummaryView = new QTextEdit(projTop);
    m_projectSummaryView->setMinimumWidth(0);
    m_projectSummaryView->setReadOnly(true);
    m_projectSummaryView->setPlaceholderText("Synthesized project architecture summary...");
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
    m_projectPromptEdit->setPlaceholderText("Global or subproject-specific instructions (e.g. Use C++20, Qt6 style)...");
    projBotLay->addWidget(projPromptLbl);
    projBotLay->addWidget(m_projectPromptEdit, 1);

    projSplit->setStretchFactor(0, 2);
    projSplit->setStretchFactor(1, 1);
    projSplit->setSizes({420, 210});
    m_tabs->addTab(projSplit, "Project");

    // =========================================================================
    // Вкладка 3: Effective Context
    // =========================================================================
    auto* effPage = new QWidget(m_tabs);
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
    m_tabs->addTab(effPage, "Effective Context");

    setWidget(m_tabs);

    // =========================================================================
    // Сигналы и обработчики
    // =========================================================================
    connect(m_projectScopeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updatingUi || idx < 0) return;
        if (m_editingProjectMarkdown) {
            finishEditingProjectSummary(true);
        }
        m_selectedProjectScope = m_projectScopeCombo->itemData(idx).toString();
        m_cachedProjectSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        refreshDisplay();
    });

    connect(m_filePromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi || m_activeFilePath.isEmpty()) return;
        m_projectMgr->updateFileUserPrompt(m_activeFilePath, m_filePromptEdit->toPlainText());
        m_cachedEffectiveMd.clear();
        updateEffectivePreview();
    });

    connect(m_projectPromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updatingUi || !m_projectMgr || !m_projectMgr->hasOpenProject()) return;
        m_projectMgr->setProjectSystemPrompt(m_projectPromptEdit->toPlainText(), m_selectedProjectScope);
        m_cachedEffectiveMd.clear();
        updateEffectivePreview();
    });

    // Переключатель редактирования саммари документа (Edit <-> Save)
    connect(m_editFileSummaryBtn, &QPushButton::toggled, this, [this](bool editMode) {
        if (editMode) {
            if (m_activeFilePath.isEmpty() || !m_projectMgr) {
                QSignalBlocker blocker(m_editFileSummaryBtn);
                m_editFileSummaryBtn->setChecked(false);
                return;
            }
            m_editingFileMarkdown = true;
            m_updatingUi = true;
            m_editFileSummaryBtn->setText("Save");
            m_editFileSummaryBtn->setToolTip("Save edited summary and return to rendered view");

            const QString relPath = m_projectMgr->toRelativePath(m_activeFilePath);
            m_liveFileMarkdownByPath.remove(relPath);
            m_cachedFileSummaryMd.clear();

            const auto fc = m_projectMgr->fileContext(m_activeFilePath);
            m_fileSummaryView->setReadOnly(false);
            m_fileSummaryView->clear();
            m_fileSummaryView->setCurrentCharFormat(QTextCharFormat());
            m_fileSummaryView->setPlainText(fc.aiSummary);
            m_fileSummaryView->setFocus();
            m_updatingUi = false;
        } else {
            finishEditingFileSummary(true);
            refreshDisplay();
        }
    });

    // Переключатель редактирования саммари проекта/подпроекта (Edit <-> Save)
    connect(m_editProjSummaryBtn, &QPushButton::toggled, this, [this](bool editMode) {
        if (editMode) {
            if (!m_projectMgr || !m_projectMgr->hasOpenProject()) {
                QSignalBlocker blocker(m_editProjSummaryBtn);
                m_editProjSummaryBtn->setChecked(false);
                return;
            }
            m_editingProjectMarkdown = true;
            m_updatingUi = true;
            m_editProjSummaryBtn->setText("Save");
            m_editProjSummaryBtn->setToolTip("Save edited summary and return to rendered view");

            m_liveProjectMarkdownByScope.remove(m_selectedProjectScope);
            m_cachedProjectSummaryMd.clear();

            const QString rawMd = m_projectMgr->projectSummary(m_selectedProjectScope);
            m_projectSummaryView->setReadOnly(false);
            m_projectSummaryView->clear();
            m_projectSummaryView->setCurrentCharFormat(QTextCharFormat());
            m_projectSummaryView->setPlainText(rawMd);
            m_projectSummaryView->setFocus();
            m_updatingUi = false;
        } else {
            finishEditingProjectSummary(true);
            refreshDisplay();
        }
    });

    connect(m_rebuildFileBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeFilePath.isEmpty()) return;
        finishEditingFileSummary(false);
        const QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
        m_liveFileMarkdownByPath.remove(rel);
        m_cachedFileSummaryMd.clear();
        m_fileSummaryView->clear();
        emit rebuildFileSummaryClicked(m_activeFilePath);
    });

    connect(m_continueFileBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeFilePath.isEmpty()) return;
        finishEditingFileSummary(true);
        m_cachedFileSummaryMd.clear();
        emit continueFileSummaryClicked(m_activeFilePath);
    });

    connect(m_clearFileBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeFilePath.isEmpty()) return;
        finishEditingFileSummary(false);
        const QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
        m_liveFileMarkdownByPath.remove(rel);
        m_cachedFileSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        m_fileSummaryView->clear();
        emit clearFileSummaryClicked(m_activeFilePath);
    });

    connect(m_removeFileBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeFilePath.isEmpty()) return;
        finishEditingFileSummary(false);
        const QString rel = m_projectMgr->toRelativePath(m_activeFilePath);
        m_liveFileMarkdownByPath.remove(rel);
        m_cachedFileSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        m_fileSummaryView->clear();
        emit removeFileSummaryClicked(m_activeFilePath);
    });

    connect(m_rebuildProjBtn, &QPushButton::clicked, this, [this]() {
        finishEditingProjectSummary(false);
        m_liveProjectMarkdownByScope.remove(m_selectedProjectScope);
        m_cachedProjectSummaryMd.clear();
        m_projectSummaryView->clear();
        emit rebuildProjectSummaryClicked(m_selectedProjectScope);
    });

    connect(m_continueProjBtn, &QPushButton::clicked, this, [this]() {
        finishEditingProjectSummary(true);
        m_cachedProjectSummaryMd.clear();
        emit continueProjectSummaryClicked(m_selectedProjectScope);
    });

    connect(m_projectMgr, &ProjectManager::fileContextChanged, this, [this](const QString& relPath) {
        if (!m_projectMgr->fileContext(relPath).isSummaryPending) {
            m_liveFileMarkdownByPath.remove(relPath);
        }
        if (!m_activeFilePath.isEmpty() && m_projectMgr->toRelativePath(m_activeFilePath) == relPath) {
            m_cachedFileSummaryMd.clear();
        }
        m_cachedEffectiveMd.clear();
        scheduleRefreshDisplay();
    });

    connect(m_projectMgr, &ProjectManager::projectContextChanged, this, [this]() {
        m_liveProjectMarkdownByScope.clear();
        m_cachedProjectSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        scheduleRefreshDisplay();
    });

    connect(m_projectMgr, &ProjectManager::subprojectsChanged, this, [this]() {
        m_liveProjectMarkdownByScope.clear();
        m_cachedProjectSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        rebuildProjectScopeCombo();
        scheduleRefreshDisplay();
    });

    connect(m_projectMgr, &ProjectManager::projectLoaded, this, [this](const QString&) {
        finishEditingFileSummary(false);
        finishEditingProjectSummary(false);
        m_selectedProjectScope.clear();
        m_liveFileMarkdownByPath.clear();
        m_liveProjectMarkdownByScope.clear();
        m_cachedFileSummaryMd.clear();
        m_cachedProjectSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        rebuildProjectScopeCombo();
        scheduleRefreshDisplay();
    });

    connect(m_projectMgr, &ProjectManager::projectClosed, this, [this]() {
        finishEditingFileSummary(false);
        finishEditingProjectSummary(false);
        m_selectedProjectScope.clear();
        m_liveFileMarkdownByPath.clear();
        m_liveProjectMarkdownByScope.clear();
        m_cachedFileSummaryMd.clear();
        m_cachedProjectSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        rebuildProjectScopeCombo();
        scheduleRefreshDisplay();
    });

    connect(&ConfigManager::instance(), &ConfigManager::aiBehaviorChanged, this, [this](const AiBehaviorConfig&) {
        m_cachedFileSummaryMd.clear();
        m_cachedProjectSummaryMd.clear();
        m_cachedEffectiveMd.clear();
        scheduleRefreshDisplay();
    });
}

void ContextInspectorDock::finishEditingFileSummary(bool saveChanges) {
    if (!m_editingFileMarkdown && (!m_editFileSummaryBtn || !m_editFileSummaryBtn->isChecked())) {
        return;
    }

    const bool wasEditing = m_editingFileMarkdown;
    m_editingFileMarkdown = false;
    m_updatingUi = true;

    if (m_editFileSummaryBtn) {
        QSignalBlocker blocker(m_editFileSummaryBtn);
        m_editFileSummaryBtn->setChecked(false);
        m_editFileSummaryBtn->setText("Edit");
        m_editFileSummaryBtn->setToolTip("Edit document summary (Markdown)");
    }

    m_fileSummaryView->setReadOnly(true);
    m_cachedFileSummaryMd.clear();
    m_cachedEffectiveMd.clear();

    if (saveChanges && wasEditing && !m_activeFilePath.isEmpty() && m_projectMgr && m_projectMgr->hasOpenProject()) {
        const QString updatedMd = m_fileSummaryView->toPlainText().trimmed();
        const QString relPath = m_projectMgr->toRelativePath(m_activeFilePath);
        m_liveFileMarkdownByPath.remove(relPath);

        auto fc = m_projectMgr->fileContext(m_activeFilePath);
        QString sha = fc.sha256;
        if (sha.isEmpty()) {
            QFile f(m_activeFilePath);
            if (f.open(QIODevice::ReadOnly)) {
                sha = ProjectManager::computeSha256(f.readAll());
            }
        }
        const QString llm = fc.llmModel.isEmpty() ? QStringLiteral("manual") : fc.llmModel;

        if (updatedMd.isEmpty()) {
            m_projectMgr->clearFileSummary(m_activeFilePath, sha);
            m_fileSummaryView->clear();
        } else {
            m_projectMgr->updateFileSummary(relPath, sha, llm, updatedMd);
            renderMarkdownPreservingScroll(m_fileSummaryView, updatedMd, m_cachedFileSummaryMd);
        }
        updateEffectivePreview();
    }

    m_updatingUi = false;
}

void ContextInspectorDock::finishEditingProjectSummary(bool saveChanges) {
    if (!m_editingProjectMarkdown && (!m_editProjSummaryBtn || !m_editProjSummaryBtn->isChecked())) {
        return;
    }

    const bool wasEditing = m_editingProjectMarkdown;
    m_editingProjectMarkdown = false;
    m_updatingUi = true;

    if (m_editProjSummaryBtn) {
        QSignalBlocker blocker(m_editProjSummaryBtn);
        m_editProjSummaryBtn->setChecked(false);
        m_editProjSummaryBtn->setText("Edit");
        m_editProjSummaryBtn->setToolTip("Edit project / subproject summary (Markdown)");
    }

    m_projectSummaryView->setReadOnly(true);
    m_liveProjectMarkdownByScope.remove(m_selectedProjectScope);
    m_cachedProjectSummaryMd.clear();
    m_cachedEffectiveMd.clear();

    if (saveChanges && wasEditing && m_projectMgr && m_projectMgr->hasOpenProject()) {
        const QString updatedMd = m_projectSummaryView->toPlainText().trimmed();
        QString llm = m_projectMgr->projectSummaryLlm(m_selectedProjectScope);
        if (llm.isEmpty()) {
            llm = QStringLiteral("manual");
        }

        m_projectMgr->setProjectSummary(updatedMd, llm, m_selectedProjectScope);
        renderMarkdownPreservingScroll(m_projectSummaryView, updatedMd, m_cachedProjectSummaryMd);
        updateEffectivePreview();
    }

    m_updatingUi = false;
}

void ContextInspectorDock::setupMarkdownViewerStyle(QTextEdit* viewer) {
    MarkdownRenderer::configureViewer(viewer);
}

void ContextInspectorDock::renderMarkdownPreservingScroll(QTextEdit* viewer,
                                                          const QString& markdownText,
                                                          QString& cachedMarkdown,
                                                          bool scrollToBottom) {
    if (!viewer) return;

    if (markdownText.trimmed().isEmpty()) {
        cachedMarkdown.clear();
        viewer->clear();
        return;
    }

    if (markdownText == cachedMarkdown && !viewer->document()->isEmpty()) {
        if (scrollToBottom) {
            QScrollBar* vbar = viewer->verticalScrollBar();
            vbar->setValue(vbar->maximum());
        }
        return;
    }
    cachedMarkdown = markdownText;

    QScrollBar* vbar = viewer->verticalScrollBar();
    int oldScroll = vbar->value();
    int oldMax = vbar->maximum();
    bool wasAtBottom = (oldMax == 0) || (oldScroll >= oldMax - 6);

    viewer->setUpdatesEnabled(false);
    viewer->setHtml(MarkdownRenderer::toHtml(markdownText));
    if (scrollToBottom && wasAtBottom) {
        vbar->setValue(vbar->maximum());
    } else {
        vbar->setValue(oldScroll);
    }
    viewer->setUpdatesEnabled(true);
}

void ContextInspectorDock::setActiveFile(const QString& filePath) {
    if (m_activeFilePath == filePath) {
        refreshDisplay();
        return;
    }
    if (m_editingFileMarkdown) {
        finishEditingFileSummary(true);
    }
    m_activeFilePath = filePath;
    m_cachedFileSummaryMd.clear();
    m_cachedEffectiveMd.clear();
    refreshDisplay();
}

void ContextInspectorDock::setSelectedProjectScope(const QString& subprojectRelPath) {
    QString clean = m_projectMgr ? m_projectMgr->toRelativePath(subprojectRelPath) : subprojectRelPath.trimmed();
    if (clean == ".") clean.clear();

    if (m_editingProjectMarkdown && m_selectedProjectScope != clean) {
        finishEditingProjectSummary(true);
    }
    m_selectedProjectScope = clean;
    m_cachedProjectSummaryMd.clear();
    m_cachedEffectiveMd.clear();
    rebuildProjectScopeCombo();
    refreshDisplay();
}

void ContextInspectorDock::inspectProjectScope(const QString& subprojectRelPath) {
    setSelectedProjectScope(subprojectRelPath);
    if (m_tabs && m_tabs->count() > 1) {
        m_tabs->setCurrentIndex(1);
    }
    show();
    raise();
}

QString ContextInspectorDock::selectedProjectScope() const {
    return m_selectedProjectScope;
}

void ContextInspectorDock::rebuildProjectScopeCombo() {
    if (!m_projectScopeCombo) return;
    QSignalBlocker blocker(m_projectScopeCombo);
    m_projectScopeCombo->clear();

    if (!m_projectMgr || !m_projectMgr->hasOpenProject()) {
        m_projectScopeCombo->addItem("(No Project Open)", QString());
        m_projectScopeCombo->setEnabled(false);
        return;
    }

    m_projectScopeCombo->setEnabled(true);
    const QString rootName = QFileInfo(m_projectMgr->projectRootPath()).fileName();
    m_projectScopeCombo->addItem(
        QString("Root Project (%1)").arg(rootName.isEmpty() ? m_projectMgr->projectRootPath() : rootName),
        QString());

    const QStringList subs = m_projectMgr->subprojectRelPaths();
    int targetIdx = 0;
    for (int i = 0; i < subs.size(); ++i) {
        const QString& subRel = subs[i];
        const bool standalone = m_projectMgr->hasStandaloneAideFile(subRel);
        const QString badge = standalone ? QStringLiteral(" [.aide]") : QStringLiteral(" [virtual]");
        m_projectScopeCombo->addItem(subRel + badge, subRel);
        if (subRel.compare(m_selectedProjectScope, Qt::CaseInsensitive) == 0) {
            targetIdx = i + 1;
        }
    }

    if (targetIdx == 0 && !m_selectedProjectScope.isEmpty()) {
        m_projectScopeCombo->addItem(m_selectedProjectScope + " [folder]", m_selectedProjectScope);
        targetIdx = m_projectScopeCombo->count() - 1;
    }

    m_projectScopeCombo->setCurrentIndex(targetIdx);
}

void ContextInspectorDock::scheduleRefreshDisplay() {
    if (!m_refreshDebounceTimer.isActive()) {
        m_refreshDebounceTimer.start();
    }
}

void ContextInspectorDock::resetLiveSummary(const QString& targetRelPath, bool isProjectSummary) {
    if (isProjectSummary) {
        QString clean = targetRelPath.trimmed();
        if (clean == ".") clean.clear();
        m_liveProjectMarkdownByScope.remove(clean);
        if (clean == m_selectedProjectScope) {
            m_cachedProjectSummaryMd.clear();
        }
    } else if (!targetRelPath.isEmpty()) {
        m_liveFileMarkdownByPath.remove(targetRelPath);
        if (!m_activeFilePath.isEmpty() && m_projectMgr && m_projectMgr->toRelativePath(m_activeFilePath) == targetRelPath) {
            m_cachedFileSummaryMd.clear();
        }
    }
    m_cachedEffectiveMd.clear();
    scheduleRefreshDisplay();
}

void ContextInspectorDock::appendLiveSummaryChunk(const QString& targetRelPath, bool isProjectSummary, const QString& chunk) {
    m_updatingUi = true;
    if (isProjectSummary) {
        QString clean = targetRelPath.trimmed();
        if (clean == ".") clean.clear();
        QString& liveBuf = m_liveProjectMarkdownByScope[clean];
        liveBuf += chunk;
        if (!m_editingProjectMarkdown && clean == m_selectedProjectScope) {
            renderMarkdownPreservingScroll(m_projectSummaryView, liveBuf, m_cachedProjectSummaryMd, true);
        }
    } else if (!targetRelPath.isEmpty()) {
        QString& liveBuf = m_liveFileMarkdownByPath[targetRelPath];
        liveBuf += chunk;
        if (!m_editingFileMarkdown && !m_activeFilePath.isEmpty() &&
            m_projectMgr->toRelativePath(m_activeFilePath) == targetRelPath) {
            renderMarkdownPreservingScroll(m_fileSummaryView, liveBuf, m_cachedFileSummaryMd, true);
        }
    }
    m_updatingUi = false;
}

void ContextInspectorDock::refreshDisplay() {
    if (m_updatingUi) return;
    m_updatingUi = true;

    const bool hasProj = m_projectMgr && m_projectMgr->hasOpenProject();
    if (m_projectScopeCombo && m_projectScopeCombo->count() <= 1 && hasProj) {
        rebuildProjectScopeCombo();
    }

    m_projectPromptEdit->setEnabled(hasProj);
    m_rebuildProjBtn->setEnabled(hasProj);
    m_editProjSummaryBtn->setEnabled(hasProj);

    if (hasProj) {
        const QString sysPrompt = m_projectMgr->projectSystemPrompt(m_selectedProjectScope);
        if (m_projectPromptEdit->toPlainText() != sysPrompt) {
            m_projectPromptEdit->setPlainText(sysPrompt);
        }

        const QString projSum = m_projectMgr->projectSummary(m_selectedProjectScope);
        const QString projLlm = m_projectMgr->projectSummaryLlm(m_selectedProjectScope);
        const int indexedCount = m_projectMgr->indexedFilesCount(m_selectedProjectScope, true);
        const bool dirty = m_projectMgr->isProjectSummaryDirty(m_selectedProjectScope);

        m_continueProjBtn->setEnabled(!projSum.trimmed().isEmpty());
        m_projectMetaLabel->setText(
            QString("LLM: %1 | Indexed files: %2%3")
                .arg(projLlm.isEmpty() ? "-" : projLlm)
                .arg(indexedCount)
                .arg(dirty ? " | [Outdated]" : ""));

        if (!m_editingProjectMarkdown) {
            const QString liveProj = m_liveProjectMarkdownByScope.value(m_selectedProjectScope);
            if (!liveProj.isEmpty()) {
                renderMarkdownPreservingScroll(m_projectSummaryView, liveProj, m_cachedProjectSummaryMd, true);
            } else {
                renderMarkdownPreservingScroll(m_projectSummaryView, projSum, m_cachedProjectSummaryMd);
            }
        }
    } else {
        m_continueProjBtn->setEnabled(false);
        m_projectMetaLabel->setText("LLM: - | Indexed files: 0");
        m_projectPromptEdit->clear();
        if (!m_editingProjectMarkdown) {
            m_cachedProjectSummaryMd.clear();
            m_projectSummaryView->clear();
        }
    }

    const bool hasFile = !m_activeFilePath.isEmpty() && hasProj;
    m_filePromptEdit->setEnabled(hasFile);
    m_rebuildFileBtn->setEnabled(hasFile);
    m_clearFileBtn->setEnabled(hasFile);
    m_removeFileBtn->setEnabled(hasFile);

    if (hasFile) {
        const auto fc = m_projectMgr->fileContext(m_activeFilePath);
        QString skipReasonMd;
        const bool summaryAllowed = ConfigManager::instance().shouldGenerateSummaryForFile(m_activeFilePath, &skipReasonMd);
        const bool isClearedEmpty = fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && !fc.isSummaryPending;

        m_editFileSummaryBtn->setEnabled(!fc.isSummaryPending || m_editingFileMarkdown);
        m_continueFileBtn->setEnabled(!fc.isSummaryPending && !fc.aiSummary.trimmed().isEmpty());

        const QString statusSuffix = fc.isSummaryPending ? " [Indexing...]"
                                     : isClearedEmpty ? " [Summary Empty]"
                                                         : (!summaryAllowed && fc.aiSummary.isEmpty() ? " [Summary Skipped]" : "");
        m_fileHeaderLabel->setText(QString("Document: %1%2").arg(fc.relativePath, statusSuffix));
        m_fileHeaderLabel->setToolTip(fc.relativePath);

        const QString llmInfo = fc.isSummaryPending
                                    ? (fc.pendingLlmModel.isEmpty() ? "-" : fc.pendingLlmModel + " (pending)")
                                    : (fc.llmModel.isEmpty()
                                           ? (isClearedEmpty ? "cleared (empty)" : (summaryAllowed ? "-" : "Skipped by settings"))
                                           : fc.llmModel);
        const QString shaInfo = fc.isSummaryPending
                                    ? (fc.pendingSha256.isEmpty() ? "-" : fc.pendingSha256.left(12))
                                    : (fc.sha256.isEmpty() ? "-" : fc.sha256.left(12));
        m_fileMetaLabel->setText(QString("LLM: %1 | SHA-256: %2").arg(llmInfo, shaInfo));

        if (m_filePromptEdit->toPlainText() != fc.userPrompt) {
            m_filePromptEdit->setPlainText(fc.userPrompt);
        }

        if (!m_editingFileMarkdown) {
            if (!fc.isSummaryPending) {
                m_liveFileMarkdownByPath.remove(fc.relativePath);
                if (!fc.aiSummary.trimmed().isEmpty()) {
                    m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on first open...");
                    renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary, m_cachedFileSummaryMd);
                } else if (isClearedEmpty) {
                    m_cachedFileSummaryMd.clear();
                    m_fileSummaryView->clear();
                    m_fileSummaryView->setPlaceholderText("Summary is created and intentionally empty (cleared). Click 'Regenerate' to rebuild.");
                } else if (!summaryAllowed) {
                    renderMarkdownPreservingScroll(m_fileSummaryView, skipReasonMd, m_cachedFileSummaryMd);
                } else {
                    m_cachedFileSummaryMd.clear();
                    m_fileSummaryView->clear();
                    m_fileSummaryView->setPlaceholderText("AI summary will be generated automatically on first open...");
                }
            } else {
                const QString liveMd = m_liveFileMarkdownByPath.value(fc.relativePath);
                if (!liveMd.isEmpty()) {
                    renderMarkdownPreservingScroll(m_fileSummaryView, liveMd, m_cachedFileSummaryMd, true);
                } else {
                    renderMarkdownPreservingScroll(m_fileSummaryView, fc.aiSummary, m_cachedFileSummaryMd);
                }
            }
        }
    } else {
        m_editFileSummaryBtn->setEnabled(false);
        m_continueFileBtn->setEnabled(false);
        m_fileHeaderLabel->setText("Document: None (Unsaved or Closed)");
        m_fileHeaderLabel->setToolTip({});
        m_fileMetaLabel->setText("LLM: - | SHA-256: -");
        m_filePromptEdit->clear();
        if (!m_editingFileMarkdown) {
            m_cachedFileSummaryMd.clear();
            m_fileSummaryView->clear();
        }
    }

    updateEffectivePreview();
    m_updatingUi = false;
}

void ContextInspectorDock::updateEffectivePreview() {
    if (!m_projectMgr) return;
    const QString ctx = m_projectMgr->buildEffectiveSystemContext(m_activeFilePath);
    renderMarkdownPreservingScroll(m_effectiveContextPreview, ctx, m_cachedEffectiveMd);
    const int chars = ctx.length();
    const int tokens = (chars + 3) / 4;
    m_tokenEstimateLabel->setText(QString("Estimated size: %1 chars (~%2 tokens)").arg(chars).arg(tokens));
}
