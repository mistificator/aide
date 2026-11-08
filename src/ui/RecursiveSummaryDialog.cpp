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
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QStyle>
#include <QTimer>
#include <QApplication>

namespace {
constexpr int RoleFilePath   = Qt::UserRole;
constexpr int RoleIsAllowed  = Qt::UserRole + 1;
constexpr int RoleHasSummary = Qt::UserRole + 2;
constexpr int RoleFileSize   = Qt::UserRole + 3;
} // namespace

bool ProjectFileSelectorWidget::FastFilterRules::isDirIgnored(const QString& dirName) const {
    if (dirName.isEmpty()) return true;
    const QString lower = dirName.toLower();
    if (exactIgnoredDirs.contains(lower)) return true;
    for (const auto& rx : wildcardIgnoredDirs) {
        if (rx.match(dirName).hasMatch()) return true;
    }
    return false;
}

bool ProjectFileSelectorWidget::FastFilterRules::isFileAllowed(const QString& fileName, const QString& suffix) const {
    const QString lowerName = fileName.toLower();
    if (exactAllowedNames.contains(lowerName)) return true;
    for (const auto& rx : wildcardAllowedNames) {
        if (rx.match(fileName).hasMatch()) return true;
    }
    if (!suffix.isEmpty() && allowedExtensions.contains(suffix.toLower())) {
        return true;
    }
    return false;
}

ProjectFileSelectorWidget::FastFilterRules ProjectFileSelectorWidget::buildFastFilterRules() const {
    FastFilterRules rules;
    const auto& cfgMgr = ConfigManager::instance();
    const auto beh = cfgMgr.aiBehavior();

    for (const QString& ext : beh.summaryAllowedExtensions) {
        QString clean = ext.trimmed().toLower();
        if (clean.startsWith("*.")) clean = clean.mid(2);
        else if (clean.startsWith('.')) clean = clean.mid(1);
        if (!clean.isEmpty()) rules.allowedExtensions.insert(clean);
    }
    if (beh.summaryUseSyntaxExtensions) {
        for (const QString& ext : cfgMgr.extensionsFromLoadedSyntaxes()) {
            QString clean = ext.trimmed().toLower();
            if (!clean.isEmpty()) rules.allowedExtensions.insert(clean);
        }
    }

    for (const QString& pat : beh.summaryAllowedFilenames) {
        QString p = pat.trimmed();
        if (p.isEmpty()) continue;
        if (p.contains('*') || p.contains('?')) {
            rules.wildcardAllowedNames.append(QRegularExpression(
                QRegularExpression::wildcardToRegularExpression(p),
                QRegularExpression::CaseInsensitiveOption));
        } else {
            rules.exactAllowedNames.insert(p.toLower());
        }
    }

    for (const QString& pat : beh.summaryIgnoredDirs) {
        QString p = pat.trimmed();
        if (p.isEmpty()) continue;
        if (p.contains('*') || p.contains('?')) {
            rules.wildcardIgnoredDirs.append(QRegularExpression(
                QRegularExpression::wildcardToRegularExpression(p),
                QRegularExpression::CaseInsensitiveOption));
        } else {
            rules.exactIgnoredDirs.insert(p.toLower());
        }
    }
    return rules;
}

ProjectFileSelectorWidget::ProjectFileSelectorWidget(ProjectManager* projMgr,
                                                     const QString& rootDirPath,
                                                     FileSelectorPreset preset,
                                                     QWidget* parent)
    : QWidget(parent),
    m_projectMgr(projMgr),
    m_rootDirPath(QDir::cleanPath(rootDirPath)),
    m_preset(preset) {
    m_dirIcon = style()->standardIcon(QStyle::SP_DirIcon);
    m_fileIcon = style()->standardIcon(QStyle::SP_FileIcon);

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

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimumHeight(18);
    m_progressBar->setMaximumHeight(18);
    m_progressBar->setTextVisible(true);
    m_progressBar->setRange(0, 0);
    m_progressBar->setFormat("Preparing project file tree...");
    m_progressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #44475a; border-radius: 4px; background: #191a22; text-align: center; color: #f8f8f2; font-size: 11px; }"
        "QProgressBar::chunk { background-color: #bd93f9; border-radius: 3px; }");
    lay->addWidget(m_progressBar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"File / Folder", "AI Summary Status"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->setColumnWidth(1, 220);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    lay->addWidget(m_tree, 1);

    connect(m_showSkippedCheck, &QCheckBox::toggled, this, &ProjectFileSelectorWidget::rebuildTree);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) {
        if (!m_updatingChecks) notifySelectionChanged();
    });

    // Отложенный запуск позволяет мгновенно отрисовать окно диалога и прогресс-бар
    QTimer::singleShot(0, this, &ProjectFileSelectorWidget::rebuildTree);
}

int ProjectFileSelectorWidget::countDirectoriesQuick(const QString& dirPath,
                                                     const FastFilterRules& rules,
                                                     int& abortCounter) const {
    QDir dir(dirPath);
    if (!dir.exists()) return 0;
    int count = 1;
    const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo& subFi : subDirs) {
        if (rules.isDirIgnored(subFi.fileName())) continue;
        if ((++abortCounter % 40) == 0) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        count += countDirectoriesQuick(subFi.absoluteFilePath(), rules, abortCounter);
    }
    return count;
}

void ProjectFileSelectorWidget::rebuildTree() {
    const int myGen = ++m_buildGeneration;
    m_isLoading = true;
    m_updatingChecks = true;

    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_progressBar->setFormat("Scanning project folders...");
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if (myGen != m_buildGeneration) return;

    const FastFilterRules rules = buildFastFilterRules();
    int stepCounter = 0;
    const int totalDirs = qMax(1, countDirectoriesQuick(m_rootDirPath, rules, stepCounter));
    if (myGen != m_buildGeneration) return;

    m_progressBar->setRange(0, totalDirs);
    m_progressBar->setValue(0);
    m_progressBar->setFormat("Loading project tree: %p%");

    m_tree->setUpdatesEnabled(false);
    m_tree->blockSignals(true);
    m_tree->clear();

    QFileInfo rootFi(m_rootDirPath);
    auto* rootItem = new QTreeWidgetItem();
    rootItem->setText(0, rootFi.fileName().isEmpty() ? m_rootDirPath : rootFi.fileName());
    rootItem->setIcon(0, m_dirIcon);

    QString rootRelPrefix;
    if (m_projectMgr && !m_projectMgr->projectRootPath().isEmpty()) {
        QString rel = QDir(m_projectMgr->projectRootPath()).relativeFilePath(m_rootDirPath);
        rel.replace('\\', '/');
        if (rel != ".") rootRelPrefix = rel;
    }

    int processedDirs = 0;
    int totalFilesFound = 0;
    populateDirectoryItem(m_rootDirPath, rootRelPrefix, rootItem, rules, processedDirs, totalDirs, totalFilesFound);

    if (myGen != m_buildGeneration) {
        delete rootItem;
        m_tree->blockSignals(false);
        m_tree->setUpdatesEnabled(true);
        return;
    }

    m_totalFilesFound = totalFilesFound;

    // Включаем AutoTristate после наполнения детей, чтобы избежать лишних пересчётов при вставке
    rootItem->setFlags(rootItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    m_tree->addTopLevelItem(rootItem);

    // Если файлов слишком много, раскрываем только верхние уровни для мгновенной вёрстки
    if (totalFilesFound <= 600) {
        m_tree->expandAll();
    } else {
        rootItem->setExpanded(true);
        for (int i = 0; i < rootItem->childCount(); ++i) {
            rootItem->child(i)->setExpanded(true);
        }
    }

    m_tree->blockSignals(false);
    m_tree->setUpdatesEnabled(true);

    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    m_progressBar->setFormat(QString("Loaded %1 file(s)").arg(totalFilesFound));
    m_progressBar->hide();

    m_updatingChecks = false;
    m_isLoading = false;
    notifySelectionChanged();
    emit loadingFinished();
}

bool ProjectFileSelectorWidget::populateDirectoryItem(const QString& dirPath,
                                                      const QString& relDirPrefix,
                                                      QTreeWidgetItem* parentItem,
                                                      const FastFilterRules& rules,
                                                      int& processedDirs,
                                                      int totalDirs,
                                                      int& totalFilesFound) {
    QDir dir(dirPath);
    if (!dir.exists()) return false;

    ++processedDirs;
    if ((processedDirs % 8) == 0 || processedDirs == totalDirs) {
        m_progressBar->setValue(qMin(processedDirs, totalDirs));
        m_progressBar->setFormat(QString("Loading project tree: %1 files (%p%)").arg(totalFilesFound));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    bool hasAnyVisibleChildren = false;
    const bool showSkipped = m_showSkippedCheck->isChecked();

    // 1. Подпапки
    const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                    QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& subFi : subDirs) {
        const QString subName = subFi.fileName();
        if (rules.isDirIgnored(subName)) continue;

        auto* dirItem = new QTreeWidgetItem();
        dirItem->setText(0, subName);
        dirItem->setIcon(0, m_dirIcon);

        const QString childRelPrefix = relDirPrefix.isEmpty() ? subName : (relDirPrefix + "/" + subName);
        if (populateDirectoryItem(subFi.absoluteFilePath(), childRelPrefix, dirItem,
                                  rules, processedDirs, totalDirs, totalFilesFound)) {
            dirItem->setFlags(dirItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
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
        const QString suffix = fileFi.suffix();
        if (suffix.compare("aide", Qt::CaseInsensitive) == 0) continue;

        const QString fileName = fileFi.fileName();
        const QString absPath = fileFi.absoluteFilePath();
        const QString relPath = relDirPrefix.isEmpty() ? fileName : (relDirPrefix + "/" + fileName);

        const bool allowed = rules.isFileAllowed(fileName, suffix);
        const auto fc = m_projectMgr ? m_projectMgr->fileContext(relPath) : FileContextData{};
        const bool hasSummary = fc.summaryCreated || !fc.aiSummary.trimmed().isEmpty();
        const bool isPending = fc.isSummaryPending;

        const bool visibleByRules = (m_preset == FileSelectorPreset::IndexedFilesDefault)
                                        ? (allowed || hasSummary)
                                        : allowed;
        if (!visibleByRules && !showSkipped) {
            continue;
        }

        auto* fileItem = new QTreeWidgetItem();
        fileItem->setText(0, fileName);
        fileItem->setToolTip(0, QDir::toNativeSeparators(absPath));
        fileItem->setIcon(0, m_fileIcon);
        fileItem->setFlags(fileItem->flags() | Qt::ItemIsUserCheckable);
        fileItem->setData(0, RoleFilePath, absPath);
        fileItem->setData(0, RoleIsAllowed, allowed);
        fileItem->setData(0, RoleHasSummary, hasSummary);
        fileItem->setData(0, RoleFileSize, fileFi.size());

        const bool checkByDefault = (m_preset == FileSelectorPreset::IndexedFilesDefault)
                                        ? hasSummary
                                        : (!isPending && !hasSummary && allowed);
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

        parentItem->addChild(fileItem);
        hasAnyVisibleChildren = true;
        ++totalFilesFound;
    }

    return hasAnyVisibleChildren;
}

void ProjectFileSelectorWidget::collectCheckedFiles(QTreeWidgetItem* item, QStringList& outPaths) const {
    if (!item || item->checkState(0) == Qt::Unchecked) return;
    QString filePath = item->data(0, RoleFilePath).toString();
    if (!filePath.isEmpty() && item->checkState(0) == Qt::Checked) {
        outPaths.append(filePath);
        return;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectCheckedFiles(item->child(i), outPaths);
    }
}

void ProjectFileSelectorWidget::collectCheckedBytes(QTreeWidgetItem* item, qint64& outBytes) const {
    if (!item || item->checkState(0) == Qt::Unchecked) return;
    QString filePath = item->data(0, RoleFilePath).toString();
    if (!filePath.isEmpty() && item->checkState(0) == Qt::Checked) {
        outBytes += item->data(0, RoleFileSize).toLongLong();
        return;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectCheckedBytes(item->child(i), outBytes);
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

qint64 ProjectFileSelectorWidget::selectedFilesTotalBytes() const {
    qint64 total = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        collectCheckedBytes(m_tree->topLevelItem(i), total);
    }
    return total;
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
    m_tree->setUpdatesEnabled(false);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 3);
    m_tree->setUpdatesEnabled(true);
    m_updatingChecks = false;
    notifySelectionChanged();
}

void ProjectFileSelectorWidget::checkMissingOnly() {
    m_updatingChecks = true;
    m_tree->setUpdatesEnabled(false);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 1);
    m_tree->setUpdatesEnabled(true);
    m_updatingChecks = false;
    notifySelectionChanged();
}

void ProjectFileSelectorWidget::checkAllAllowed() {
    m_updatingChecks = true;
    m_tree->setUpdatesEnabled(false);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 2);
    m_tree->setUpdatesEnabled(true);
    m_updatingChecks = false;
    notifySelectionChanged();
}

void ProjectFileSelectorWidget::uncheckAll() {
    m_updatingChecks = true;
    m_tree->setUpdatesEnabled(false);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) applyCheckFilter(m_tree->topLevelItem(i), 0);
    m_tree->setUpdatesEnabled(true);
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
    m_countLabel = new QLabel("Loading files...", this);
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
}

QStringList RecursiveSummaryDialog::selectedFilePaths() const {
    return m_selector ? m_selector->selectedFilePaths() : QStringList{};
}

void RecursiveSummaryDialog::updateSelectionCountLabel(int count) {
    if (m_countLabel) {
        m_countLabel->setText(QString("Selected for summary generation: %1 file(s)").arg(count));
    }
}
