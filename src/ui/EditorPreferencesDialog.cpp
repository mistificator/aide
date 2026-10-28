// src/ui/EditorPreferencesDialog.cpp
#include "EditorPreferencesDialog.h"
#include "../syntax/ThemeAndSyntaxDownloader.h"
#include "../syntax/LexicalHighlighter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <algorithm>

EditorPreferencesDialog::EditorPreferencesDialog(ThemeAndSyntaxDownloader* downloader, QWidget* parent)
    : QDialog(parent), m_downloader(downloader) {
    setWindowTitle("Editor Preferences — File Types & Syntax Highlighting");
    resize(820, 560);

    auto* mainLay = new QVBoxLayout(this);

    // 1. Источник определений синтаксиса
    auto* srcGroup = new QGroupBox("Syntax Definitions Source (KDE Kate XML Format)", this);
    auto* srcLay = new QVBoxLayout(srcGroup);

    auto* urlRow = new QHBoxLayout();
    urlRow->addWidget(new QLabel("Remote Syntax Base URL:", srcGroup));
    m_baseUrlEdit = new QLineEdit(ConfigManager::instance().syntaxBaseUrl(), srcGroup);
    auto* resetUrlBtn = new QPushButton("Reset Default", srcGroup);
    urlRow->addWidget(m_baseUrlEdit, 1);
    urlRow->addWidget(resetUrlBtn);
    srcLay->addLayout(urlRow);

    auto* kdeRow = new QHBoxLayout();
    m_indexKdeBtn = new QPushButton("1. Index All KDE Syntaxes from Web", srcGroup);
    m_kdeRemoteCombo = new QComboBox(srcGroup);
    m_kdeRemoteCombo->setMinimumWidth(220);
    m_kdeRemoteCombo->setPlaceholderText("Click 'Index All KDE Syntaxes'...");
    m_downloadSelectedBtn = new QPushButton("2. Download && Register Selected", srcGroup);
    m_downloadSelectedBtn->setEnabled(false);
    auto* importLocalBtn = new QPushButton("Import Syntax XML from Disk...", srcGroup);

    kdeRow->addWidget(m_indexKdeBtn);
    kdeRow->addWidget(m_kdeRemoteCombo, 1);
    kdeRow->addWidget(m_downloadSelectedBtn);
    kdeRow->addWidget(importLocalBtn);
    srcLay->addLayout(kdeRow);

    m_statusLabel = new QLabel("Ready. Local syntax storage: " + ConfigManager::instance().syntaxDirPath(), srcGroup);
    srcLay->addWidget(m_statusLabel);
    mainLay->addWidget(srcGroup);

    // 2. Таблица поддерживаемых типов файлов (с сортировкой по Language ID)
    auto* tableGroup = new QGroupBox("Supported File Types & Extensions Mapping", this);
    auto* tableLay = new QVBoxLayout(tableGroup);

    m_table = new QTableWidget(0, 4, tableGroup);
    m_table->setHorizontalHeaderLabels({"Language ID", "Display Name", "Syntax File (.xml)", "Extensions (comma-separated)"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableLay->addWidget(m_table);

    auto* tblBtnRow = new QHBoxLayout();
    auto* addRowBtn = new QPushButton("+ Add File Type", tableGroup);
    auto* removeRowBtn = new QPushButton("- Remove Selected", tableGroup);
    auto* sortByLangBtn = new QPushButton("Sort by Language ID", tableGroup);
    auto* syncAllBtn = new QPushButton("Download All Missing XMLs", tableGroup);
    tblBtnRow->addWidget(addRowBtn);
    tblBtnRow->addWidget(removeRowBtn);
    tblBtnRow->addWidget(sortByLangBtn);
    tblBtnRow->addStretch();
    tblBtnRow->addWidget(syncAllBtn);
    tableLay->addLayout(tblBtnRow);
    mainLay->addWidget(tableGroup, 1);

    // 3. Кнопки OK / Cancel
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLay->addWidget(btnBox);

    ConfigManager::instance().repairFileTypeExtensions();
    populateTable(ConfigManager::instance().fileTypeRules());

    // Автозаполнение .xml и расширений при изменении Language ID
    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (col != 0) return;
        auto* langItem = m_table->item(row, 0);
        if (!langItem) return;
        QString langId = langItem->text().trimmed().toLower();
        if (langId.isEmpty() || langId == "custom") return;

        m_table->blockSignals(true);
        m_table->setSortingEnabled(false);

        int currentRow = m_table->row(langItem);
        auto* nameItem = m_table->item(currentRow, 1);
        auto* xmlItem = m_table->item(currentRow, 2);
        auto* extItem = m_table->item(currentRow, 3);

        QString xmlFile = langId + ".xml";
        if (xmlItem && (xmlItem->text().trimmed().isEmpty() || xmlItem->text() == "custom.xml")) {
            xmlItem->setText(xmlFile);
        }

        QString localXmlPath = ConfigManager::instance().syntaxDirPath() + "/" + (xmlItem ? xmlItem->text().trimmed() : xmlFile);
        QStringList knownExts;
        QString dispName;
        if (QFile::exists(localXmlPath)) {
            auto meta = LexicalHighlighter::inspectSyntaxXml(localXmlPath);
            if (meta.valid) {
                dispName = meta.name;
                knownExts = meta.extensions;
            }
        }
        if (knownExts.isEmpty()) {
            knownExts = ConfigManager::knownExtensionsForLanguage(langId);
        }

        if (nameItem && (nameItem->text().trimmed().isEmpty() || nameItem->text() == "Custom Language")) {
            nameItem->setText(dispName.isEmpty() ? langId.toUpper() : dispName);
        }
        if (extItem && !knownExts.isEmpty()) {
            QString curExt = extItem->text().trimmed().toLower();
            if (curExt.isEmpty() || curExt == "ext" || curExt == langId) {
                extItem->setText(knownExts.join(", "));
            }
        }

        m_table->setSortingEnabled(true);
        m_table->sortItems(0, Qt::AscendingOrder);
        m_table->blockSignals(false);
    });

    connect(resetUrlBtn, &QPushButton::clicked, this, [this]() {
        m_baseUrlEdit->setText("https://raw.githubusercontent.com/KDE/syntax-highlighting/master/data/syntax/");
    });
    connect(m_indexKdeBtn, &QPushButton::clicked, this, &EditorPreferencesDialog::onIndexKdeClicked);
    connect(m_downloadSelectedBtn, &QPushButton::clicked, this, &EditorPreferencesDialog::onDownloadSelectedKdeSyntax);
    connect(importLocalBtn, &QPushButton::clicked, this, &EditorPreferencesDialog::onImportLocalSyntaxFile);
    connect(addRowBtn, &QPushButton::clicked, this, &EditorPreferencesDialog::onAddCustomRow);
    connect(removeRowBtn, &QPushButton::clicked, this, &EditorPreferencesDialog::onRemoveSelectedRow);
    connect(sortByLangBtn, &QPushButton::clicked, this, [this]() {
        m_table->sortItems(0, Qt::AscendingOrder);
    });

    connect(syncAllBtn, &QPushButton::clicked, this, [this]() {
        ConfigManager::instance().setSyntaxBaseUrl(m_baseUrlEdit->text());
        ConfigManager::instance().setFileTypeRules(collectRulesFromTable());
        m_statusLabel->setText("Downloading syntax files from remote repository...");
        m_downloader->forceDownloadAll();
    });

    connect(m_downloader, &ThemeAndSyntaxDownloader::kdeSyntaxIndexFetched, this,
            [this](const QStringList& files, const QString& err) {
                m_indexKdeBtn->setEnabled(true);
                if (!err.isEmpty()) {
                    m_statusLabel->setText("Failed to index KDE repository: " + err);
                    return;
                }
                m_kdeRemoteCombo->clear();
                m_kdeRemoteCombo->addItems(files);
                m_downloadSelectedBtn->setEnabled(!files.isEmpty());
                m_statusLabel->setText(QString("Indexed %1 KDE syntax definitions from repository.").arg(files.size()));
            });

    connect(m_downloader, &ThemeAndSyntaxDownloader::allDownloadsFinished, this, [this]() {
        populateTable(ConfigManager::instance().fileTypeRules());
        m_statusLabel->setText("Syntax download complete and file extensions updated.");
    });

    connect(btnBox, &QDialogButtonBox::accepted, this, &EditorPreferencesDialog::onAcceptChanges);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void EditorPreferencesDialog::populateTable(const QVector<FileTypeRule>& rules) {
    QVector<FileTypeRule> sortedRules = rules;
    std::sort(sortedRules.begin(), sortedRules.end(), [](const FileTypeRule& a, const FileTypeRule& b) {
        return a.langId.compare(b.langId, Qt::CaseInsensitive) < 0;
    });

    m_table->blockSignals(true);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    for (const auto& r : sortedRules) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(r.langId));
        m_table->setItem(row, 1, new QTableWidgetItem(r.displayName));
        m_table->setItem(row, 2, new QTableWidgetItem(r.syntaxFile));
        m_table->setItem(row, 3, new QTableWidgetItem(r.extensions.join(", ")));
    }

    m_table->setSortingEnabled(true);
    m_table->sortItems(0, Qt::AscendingOrder);
    m_table->blockSignals(false);
}

QVector<FileTypeRule> EditorPreferencesDialog::collectRulesFromTable() const {
    QVector<FileTypeRule> rules;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto getCell = [&](int col) {
            auto* it = m_table->item(row, col);
            return it ? it->text().trimmed() : QString();
        };
        FileTypeRule r;
        r.langId = getCell(0).toLower();
        r.displayName = getCell(1);
        r.syntaxFile = getCell(2);
        QString extRaw = getCell(3);
        extRaw.replace(';', ',');
        for (QString e : extRaw.split(',', Qt::SkipEmptyParts)) {
            e = e.trimmed().toLower();
            if (e.startsWith("*.")) e = e.mid(2);
            else if (e.startsWith('.')) e = e.mid(1);
            if (!e.isEmpty() && !r.extensions.contains(e)) r.extensions.append(e);
        }
        if (!r.langId.isEmpty()) rules.append(r);
    }

    std::sort(rules.begin(), rules.end(), [](const FileTypeRule& a, const FileTypeRule& b) {
        return a.langId.compare(b.langId, Qt::CaseInsensitive) < 0;
    });
    return rules;
}

void EditorPreferencesDialog::onIndexKdeClicked() {
    m_indexKdeBtn->setEnabled(false);
    m_statusLabel->setText("Fetching KDE syntax list from GitHub API...");
    m_downloader->fetchKdeSyntaxRepositoryIndex();
}

void EditorPreferencesDialog::onDownloadSelectedKdeSyntax() {
    QString xmlFile = m_kdeRemoteCombo->currentText().trimmed();
    if (xmlFile.isEmpty()) return;
    ConfigManager::instance().setSyntaxBaseUrl(m_baseUrlEdit->text());
    ConfigManager::instance().setFileTypeRules(collectRulesFromTable());
    m_statusLabel->setText("Downloading " + xmlFile + "...");
    m_downloader->downloadSpecificSyntaxFiles({xmlFile}, true);
}

void EditorPreferencesDialog::onImportLocalSyntaxFile() {
    QString startDir = ConfigManager::instance().resolveInitialDialogDir();
    QString path = QFileDialog::getOpenFileName(this, "Import Syntax Highlighting XML File",
                                                startDir, "Syntax Files (*.xml *.json);;All Files (*)");
    if (path.isEmpty()) return;

    ConfigManager::instance().setLastDialogDir(path);
    QFileInfo fi(path);
    QString destPath = ConfigManager::instance().syntaxDirPath() + "/" + fi.fileName();
    if (QFile::exists(destPath) && QFileInfo(destPath).canonicalFilePath() != fi.canonicalFilePath()) {
        QFile::remove(destPath);
    }
    QFile::copy(path, destPath);

    auto meta = LexicalHighlighter::inspectSyntaxXml(destPath);

    FileTypeRule r;
    r.langId = fi.baseName().toLower();
    r.displayName = meta.valid ? meta.name : fi.baseName().toUpper();
    r.syntaxFile = fi.fileName();
    r.extensions = (meta.valid && !meta.extensions.isEmpty())
                       ? meta.extensions
                       : ConfigManager::knownExtensionsForLanguage(r.langId);
    if (r.extensions.isEmpty()) {
        r.extensions = {r.langId};
    }

    auto current = collectRulesFromTable();
    ConfigManager::instance().setFileTypeRules(current);
    ConfigManager::instance().upsertFileTypeRule(r);
    populateTable(ConfigManager::instance().fileTypeRules());

    m_statusLabel->setText(QString("Imported '%1' (%2) with extensions: %3")
                               .arg(r.displayName, r.syntaxFile, r.extensions.join(", ")));
}

void EditorPreferencesDialog::onAddCustomRow() {
    m_table->blockSignals(true);
    m_table->setSortingEnabled(false);

    int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* langItem = new QTableWidgetItem("custom");
    m_table->setItem(row, 0, langItem);
    m_table->setItem(row, 1, new QTableWidgetItem("Custom Language"));
    m_table->setItem(row, 2, new QTableWidgetItem("custom.xml"));
    m_table->setItem(row, 3, new QTableWidgetItem("ext"));

    m_table->setSortingEnabled(true);
    m_table->blockSignals(false);

    int sortedRow = m_table->row(langItem);
    m_table->selectRow(sortedRow);
    m_table->scrollToItem(langItem);
}

void EditorPreferencesDialog::onRemoveSelectedRow() {
    int row = m_table->currentRow();
    if (row >= 0) m_table->removeRow(row);
}

void EditorPreferencesDialog::onAcceptChanges() {
    ConfigManager::instance().setSyntaxBaseUrl(m_baseUrlEdit->text());
    ConfigManager::instance().setFileTypeRules(collectRulesFromTable());
    accept();
}
