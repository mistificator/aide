// src/ui/DiffViewerDialog.h
#pragma once
#include <QDialog>
#include <QString>

class QPlainTextEdit;

class DiffViewerDialog : public QDialog {
    Q_OBJECT
public:
    DiffViewerDialog(const QString& fileTitle,
                     const QString& oldText,
                     const QString& newText,
                     const QString& langId,
                     QWidget* parent = nullptr,
                     const QString& leftTitle = "BEFORE (Saved on Disk)",
                     const QString& rightTitle = "AFTER (Current Unsaved Buffer)");

private:
    void computeAndRenderDiff(const QString& oldText, const QString& newText);

    QPlainTextEdit* m_leftPane = nullptr;
    QPlainTextEdit* m_rightPane = nullptr;
};
