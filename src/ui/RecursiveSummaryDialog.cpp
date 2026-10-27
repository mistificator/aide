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
} // namespace

ProjectFileSelectorWidget::ProjectFileSelectorWidget(ProjectManager* projMgr,
                                                     const QString& rootDirPath,
                                                     FileSelectorPreset preset,
                                                     QWidget* parent)
    : QWidget(parent),
    m_projectMgr(projMgr),
    m_rootDirPath(QDir::cleanPath(rootDirPath)),
    m_preset(preset) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    if (m_preset == FileSelectorPreset::IndexedFilesDefault) {
        auto* btnIndexed = new QPushButton("Check Indexed Only", this);
        auto* btnAllAllowed = new QPushButton("Check All Allowed", this);
        auto* btnNone = new QPushButton("Uncheck All", this);
        topBar->addWidget(btnIndexed);
        topBar->addWidget(btnAllAllowed);
        topBar->addWidget(btnNone);

        connect(btnIndexed, &QPushButton::clicked, this, &ProjectFileSelectorWidget::checkIndexedOnly);
        connect(btnAllAllowed, &QPushButton::clicked, this, &ProjectFileSelectorWidget::checkAllAllowed);
        connect(btnNone, &QPushButton::clicked, this, &ProjectFileSelectorWidget::uncheckAll);
    } else {
        auto* btnMissing = new QPushButton("Check Missing Only", this);
        auto* btnAllAllowed = new QPushButton("Check All Allowed (Regenerate)", this);
        auto* btnNone = new QPushButton("Uncheck All", this);
        topBar->addWidget(btnMissing);
        topBar->addWidget(btnAllAllowed);
        topBar->addWidget(btnNone);

        connect(btnMissing, &QPushButton::clicked, this, &ProjectFileSelectorWidget::checkMissingOnly);
        connect(btnAllAllowed, &QPushButton::clicked, this, &ProjectFileSelectorWidget::checkAllAllowed);
        connect(btnNone, &QPushButton::clicked, this, &ProjectFileSelectorWidget::uncheckAll);
    }

    m_showSkippedCheck = new QCheckBox("Show files skipped by extension rules", this);
    m_showSkippedCheck->setChecked(false);
    topBar->addStretch();
    topBar->addWidget(m_showSkippedCheck);
    lay->addLayout(topBar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"File / Folder", "AI Summary Status"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setAlternatingRowColors(true);
    lay->addWidget(m_tree, 1);

    connect(m_showSkippedCheck, &QCheckBox::toggled, this, &ProjectFileSelectorWidget::rebuildTree);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) {
        if (!m_updatingChecks) notifySelectionChanged();
    });

    rebuildTree();
}

void ProjectFileSelectorWidget::rebuildTree() {
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
    notifySelectionChanged();
}

bool ProjectFileSelectorWidget::populateDirectoryItem(const QString& dirPath, QTreeWidgetItem* parentItem) {
    QDir dir(dirPath);
    if (!dir.exists()) return false;

    bool hasAnyVisibleChildren = false;
    const auto& cfgMgr = ConfigManager::instance();

    // 1. Подпапки
    const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                    QDir::Name | QDir::IgnoreCase);
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
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fileFi : files) {
        if (fileFi.suffix().compare("aide", Qt::CaseInsensitive) == 0) continue;

        QString absPath = fileFi.absoluteFilePath();
        bool allowed = cfgMgr.shouldGenerateSummaryForFile(absPath);
        auto fc = m_projectMgr->fileContext(absPath);
        bool hasSummary = fc.summaryCreated || !fc.aiSummary.trimmed().isEmpty();
        bool isPending = fc.isSummaryPending;

        // В режиме выбора для промпта всегда показываем проиндексированные файлы, даже если их расширение не в правилах
        bool visibleByRules = (m_preset == FileSelectorPreset::IndexedFilesDefault)
                                  ? (allowed || hasSummary)
                                  : allowed;
        if (!visibleByRules && !m_showSkippedCheck->isChecked()) {
            continue;
        }

        auto* fileItem = new QTreeWidgetItem(parentItem);
        fileItem->setText(0, fileFi.fileName());
        fileItem->setToolTip(0, QDir::toNativeSeparators(absPath));
        fileItem->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        fileItem->setFlags(fileItem->flags() | Qt::ItemIsUserCheckable);
        fileItem->setData(0, RoleFilePath, absPath);
        fileItem->setData(0, RoleIsAllowed, allowed);
        fileItem->setData(0, RoleHasSummary, hasSummary);

        bool checkByDefault = false;
        if (m_preset == FileSelectorPreset::IndexedFilesDefault) {
            checkByDefault = hasSummary;
        } else {
            checkByDefault = (!isPending && !hasSummary && allowed);
        }
        fileItem->setCheckState(0, checkByDefault ? Qt::Checked : Qt::Unchecked);

        if (isPending) {
            fileItem->setText(1, "Indexing in Queue...");
            fileItem->setForeground(1, QColor("#8be9fd"));
        } else if (hasSummary) {
            if (fc.aiSummary.trimmed().isEmpty()) {
                fileItem->setText(1, "Summary Ready (Empty / Cleared)");
            } else {
                fileItem->setText(1, QString("Summary Ready (%1)").arg(fc.llmModel.isEmpty() ? "cached" : fc.llmModel));
            }
            fileItem->setForeground(1, QColor("#50fa7b"));
        } else if (allowed) {
            fileItem->setText(1, m_preset == FileSelectorPreset::IndexedFilesDefault
                                     ? "Not Indexed"
                                     : "No Summary (Will Generate)");
            fileItem->setForeground(1, QColor("#ffb86c"));
        } else {
            fileItem->setText(1, "Skipped by Rules");
            fileItem->setForeground(1, QColor("#888899"));
        }

        hasAnyVisibleChildren = true;
    }

    return hasAnyVisibleChildren;
}

void ProjectFileSelectorWidget::collectCheckedFiles(QTreeWidgetItem* item, QStringList& outPaths) const {
    if (!item) return;
    QString filePath = item->data(0, RoleFilePath).toString();
    if (!filePath.isEmpty() && item->checkState(0) == Qt::Checked) {
        outPaths.append(filePath);
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectCheckedFiles(item->child(i), outPaths);
    }
}

QStringList ProjectFileSelectorWidget::selectedFilePaths() const {
    QStringList out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        collectCheckedFiles(m_tree->topLevelItem(i), out);
    }
    return out;
}

int ProjectFileSelectorWidget::selectedFilesCount() const {
    return selectedFilePaths().size();
}

void ProjectFileSelectorWidget::notifySelectionChanged() {
    emit selectionChanged(selectedFilesCount());
}

void ProjectFileSelectorWidget::applyCheckFilter(QTreeWidgetItem* item, int mode) {
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
            bool shouldCheck = (m_preset == FileSelectorPreset::IndexedFilesDefault) ? (allowed || hasSummary) : allowed;
            item->setCheckState(0, shouldCheck ? Qt::Checked : Qt::Unchecked);
        } else if (mode == 3) {
            item->setCheckState(0, hasSummary ? Qt::Checked : Qt::Unchecked);
        }
    }
    for (int i = 0; i < item->childCount(); ++i) {
        applyCheckFilter(item->child(i), mode);
    }
}

void ProjectFileSelectorWidget::checkIndexedOnly() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 3);
    m_updatingChecks = false;
    notifySelectionChanged();
}

void ProjectFileSelectorWidget::checkMissingOnly() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 1);
    m_updatingChecks = false;
    notifySelectionChanged();
}

void ProjectFileSelectorWidget::checkAllAllowed() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 2);
    m_updatingChecks = false;
    notifySelectionChanged();
}

void ProjectFileSelectorWidget::uncheckAll() {
    m_updatingChecks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 0);
    m_updatingChecks = false;
    notifySelectionChanged();
}

// ============================================================================
// RecursiveSummaryDialog
// ============================================================================

RecursiveSummaryDialog::RecursiveSummaryDialog(ProjectManager* projMgr,
                                               const QString& rootDirPath,
                                               QWidget* parent)
    : QDialog(parent) {
    QString cleanRoot = QDir::cleanPath(rootDirPath);
    setWindowTitle("Generate AI Summaries — Select Project Files");
    resize(760, 580);

    auto* mainLay = new QVBoxLayout(this);

    auto* infoLabel = new QLabel(
        QString("<b>Target Directory:</b> %1<br>"
                "<span style='color:#888899;'>Files without an existing summary are pre-selected. "
                "Files that already have a summary are unchecked by default (check them to force regeneration).</span>")
            .arg(QDir::toNativeSeparators(cleanRoot)),
        this);
    infoLabel->setWordWrap(true);
    mainLay->addWidget(infoLabel);

    m_selector = new ProjectFileSelectorWidget(projMgr, cleanRoot, FileSelectorPreset::MissingSummariesDefault, this);
    mainLay->addWidget(m_selector, 1);

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

    connect(m_selector, &ProjectFileSelectorWidget::selectionChanged,
            this, &RecursiveSummaryDialog::updateSelectionCountLabel);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateSelectionCountLabel(m_selector->selectedFilesCount());
}

QStringList RecursiveSummaryDialog::selectedFilePaths() const {
    return m_selector ? m_selector->selectedFilePaths() : QStringList{};
}

void RecursiveSummaryDialog::updateSelectionCountLabel(int count) {
    if (m_countLabel) {
        m_countLabel->setText(QString("Selected for summary generation: %1 file(s)").arg(count));
    }
}
