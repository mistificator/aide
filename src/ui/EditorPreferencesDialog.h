// src/ui/EditorPreferencesDialog.h
#pragma once
#include <QDialog>
#include "../core/ConfigManager.h"

class QLineEdit;
class QTableWidget;
class QComboBox;
class QPushButton;
class QLabel;
class ThemeAndSyntaxDownloader;

class EditorPreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditorPreferencesDialog(ThemeAndSyntaxDownloader* downloader, QWidget* parent = nullptr);

private slots:
    void onIndexKdeClicked();
    void onDownloadSelectedKdeSyntax();
    void onImportLocalSyntaxFile();
    void onAddCustomRow();
    void onRemoveSelectedRow();
    void onAcceptChanges();

private:
    void populateTable(const QVector<FileTypeRule>& rules);
    QVector<FileTypeRule> collectRulesFromTable() const;

    ThemeAndSyntaxDownloader* m_downloader = nullptr;
    QLineEdit* m_baseUrlEdit = nullptr;
    QComboBox* m_kdeRemoteCombo = nullptr;
    QPushButton* m_indexKdeBtn = nullptr;
    QPushButton* m_downloadSelectedBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableWidget* m_table = nullptr;
};
