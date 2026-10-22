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
                     QWidget* parent = nullptr);

private:
    void computeAndRenderDiff(const QString& oldText, const QString& newText);

    QPlainTextEdit* m_leftPane = nullptr;
    QPlainTextEdit* m_rightPane = nullptr;
};
