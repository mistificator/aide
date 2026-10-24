// src/ui/RecursiveSummaryDialog.cpp
#include "RecursiveSummaryDialog.h"
#include "../core/ProjectManager.h"
#include "../core/ConfigManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QStyle>

namespace {
constexpr int RoleFilePath = Qt::UserRole;
constexpr int RoleIsAllowed = Qt::UserRole + 1;
constexpr int RoleHasSummary = Qt::UserRole + 2;
}

RecursiveSummaryDialog::RecursiveSummaryDialog(ProjectManager* projMgr,
                                               const QString& rootDirPath,
                                               QWidget* parent)
    : QDialog(parent), m_projectMgr(projMgr), m_rootDirPath(QDir::cleanPath(rootDirPath)) {
    setWindowTitle("Generate AI Summaries — Select Project Files");
    resize(760, 580);

    auto* mainLay = new QVBoxLayout(this);

    auto* infoLabel = new QLabel(
        QString("<b>Target Directory:</b> %1<br>"
                "<span style='color:#888899;'>Files without an existing summary are pre-selected. "
                "Files that already have a summary are unchecked by default (check them to force regeneration).</span>")
            .arg(QDir::toNativeSeparators(m_rootDirPath)),
        this);
    infoLabel->setWordWrap(true);
    mainLay->addWidget(infoLabel);

    auto* topBar = new QHBoxLayout();
    auto* btnMissing = new QPushButton("Check Missing Only", this);
    auto* btnAllAllowed = new QPushButton("Check All Allowed (Regenerate)", this);
    auto* btnNone = new QPushButton("Uncheck All", this);
    m_showSkippedCheck = new QCheckBox("Show files skipped by extension rules", this);
    m_showSkippedCheck->setChecked(false);

    topBar->addWidget(btnMissing);
    topBar->addWidget(btnAllAllowed);
    topBar->addWidget(btnNone);
    topBar->addStretch();
    topBar->addWidget(m_showSkippedCheck);
    mainLay->addLayout(topBar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"File / Folder", "AI Summary Status"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setAlternatingRowColors(true);
    mainLay->addWidget(m_tree, 1);

    auto* bottomRow = new QHBoxLayout();
    m_countLabel = new QLabel("Selected: 0 file(s)", this);
    m_countLabel->setStyleSheet("font-weight: bold; color: #bd93f9;");
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* okBtn = btnBox->button(QDialogButtonBox::Ok)) {
        okBtn->setText("Generate Summaries");
    }
    bottomRow->addWidget(m_countLabel, 1);
    bottomRow->addWidget(btnBox);
    mainLay->addLayout(bottomRow);

    connect(btnMissing, &QPushButton::clicked, this, &RecursiveSummaryDialog::checkMissingOnly);
    connect(btnAllAllowed, &QPushButton::clicked, this, &RecursiveSummaryDialog::checkAllAllowed);
    connect(btnNone, &QPushButton::clicked, this, &RecursiveSummaryDialog::uncheckAll);
    connect(m_showSkippedCheck, &QCheckBox::toggled, this, &RecursiveSummaryDialog::rebuildTree);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) {
        if (!m_updatingChecks) updateSelectionCountLabel();
    });
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildTree();
}

void RecursiveSummaryDialog::rebuildTree() {
    m_updatingChecks = true;
    m_tree->clear();

    QFileInfo rootFi(m_rootDirPath);
    auto* rootItem = new QTreeWidgetItem(m_tree);
    rootItem->setText(0, rootFi.fileName().isEmpty() ? m_rootDirPath : rootFi.fileName());
    rootItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    rootItem->setFlags(rootItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    rootItem->setCheckState(0, Qt::Unchecked);

    populateDirectoryItem(m_rootDirPath, rootItem);
    m_tree->expandAll();

    m_updatingChecks = false;
    updateSelectionCountLabel();
}

bool RecursiveSummaryDialog::populateDirectoryItem(const QString& dirPath, QTreeWidgetItem* parentItem) {
    QDir dir(dirPath);
    if (!dir.exists()) return false;

    bool hasAnyVisibleChildren = false;
    const auto& cfgMgr = ConfigManager::instance();

    // 1. Подпапки (с учётом игнор-листа папок)
    const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& subFi : subDirs) {
        if (cfgMgr.isDirectoryIgnoredForSummary(subFi.fileName())) {
            continue;
        }
        auto* dirItem = new QTreeWidgetItem();
        dirItem->setText(0, subFi.fileName());
        dirItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        dirItem->setFlags(dirItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        dirItem->setCheckState(0, Qt::Unchecked);

        if (populateDirectoryItem(subFi.absoluteFilePath(), dirItem)) {
            parentItem->addChild(dirItem);
            hasAnyVisibleChildren = true;
        } else {
            delete dirItem;
        }
    }

    // 2. Файлы
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fileFi : files) {
        if (fileFi.suffix().compare("aide", Qt::CaseInsensitive) == 0) continue;

        QString absPath = fileFi.absoluteFilePath();
        bool allowed = cfgMgr.shouldGenerateSummaryForFile(absPath);
        if (!allowed && !m_showSkippedCheck->isChecked()) {
            continue;
        }

        auto fc = m_projectMgr->fileContext(absPath);
        bool hasSummary = fc.summaryCreated || !fc.aiSummary.trimmed().isEmpty();
        bool isPending = fc.isSummaryPending;

        auto* fileItem = new QTreeWidgetItem(parentItem);
        fileItem->setText(0, fileFi.fileName());
        fileItem->setToolTip(0, QDir::toNativeSeparators(absPath));
        fileItem->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        fileItem->setFlags(fileItem->flags() | Qt::ItemIsUserCheckable);

        fileItem->setData(0, RoleFilePath, absPath);
        fileItem->setData(0, RoleIsAllowed, allowed);
        fileItem->setData(0, RoleHasSummary, hasSummary);

        if (isPending) {
            fileItem->setCheckState(0, Qt::Unchecked);
            fileItem->setText(1, "Indexing in Queue...");
            fileItem->setForeground(1, QColor("#8be9fd"));
        } else if (hasSummary) {
            fileItem->setCheckState(0, Qt::Unchecked);
            if (fc.aiSummary.trimmed().isEmpty()) {
                fileItem->setText(1, "Summary Ready (Empty / Cleared)");
            } else {
                fileItem->setText(1, QString("Summary Ready (%1)").arg(fc.llmModel.isEmpty() ? "cached" : fc.llmModel));
            }
            fileItem->setForeground(1, QColor("#50fa7b"));
        } else if (allowed) {
            fileItem->setCheckState(0, Qt::Checked);
            fileItem->setText(1, "No Summary (Will Generate)");
            fileItem->setForeground(1, QColor("#ffb86c"));
        } else {
            fileItem->setCheckState(0, Qt::Unchecked);
            fileItem->setText(1, "Skipped by Rules");
            fileItem->setForeground(1, QColor("#888899"));
        }

        hasAnyVisibleChildren = true;
    }

    return hasAnyVisibleChildren;
}

void RecursiveSummaryDialog::collectCheckedFiles(QTreeWidgetItem* item, QStringList& outPaths) const {
    if (!item) return;
    QString filePath = item->data(0, RoleFilePath).toString();
    if (!filePath.isEmpty() && item->checkState(0) == Qt::Checked) {
        outPaths.append(filePath);
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectCheckedFiles(item->child(i), outPaths);
    }
}

QStringList RecursiveSummaryDialog::selectedFilePaths() const {
    QStringList out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        collectCheckedFiles(m_tree->topLevelItem(i), out);
    }
    return out;
}

void RecursiveSummaryDialog::updateSelectionCountLabel() {
    int count = selectedFilePaths().size();
    m_countLabel->setText(QString("Selected for summary generation: %1 file(s)").arg(count));
}

void RecursiveSummaryDialog::applyCheckFilter(QTreeWidgetItem* item, int mode) {
    if (!item) return;
    QString filePath = item->data(0, RoleFilePath).toString();
    if (!filePath.isEmpty()) {
        bool allowed = item->data(0, RoleIsAllowed).toBool();
        bool hasSummary = item->data(0, RoleHasSummary).toBool();
        if (mode == 0) {
            item->setCheckState(0, Qt::Unchecked);
        } else if (mode == 1) {
            item->setCheckState(0, (allowed && !hasSummary) ? Qt::Checked : Qt::Unchecked);
        } else if (mode == 2) {
            item->setCheckState(0, allowed ? Qt::Checked : Qt::Unchecked);
        }
    }
    for (int i = 0; i < item->childCount(); ++i) {
        applyCheckFilter(item->child(i), mode);
    }
}

void RecursiveSummaryDialog::checkMissingOnly() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 1);
    m_updatingChecks = false;
    updateSelectionCountLabel();
}

void RecursiveSummaryDialog::checkAllAllowed() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 2);
    m_updatingChecks = false;
    updateSelectionCountLabel();
}

void RecursiveSummaryDialog::uncheckAll() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 0);
    m_updatingChecks = false;
    updateSelectionCountLabel();
}
