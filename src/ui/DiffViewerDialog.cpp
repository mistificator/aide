// src/ui/DiffViewerDialog.cpp
#include "DiffViewerDialog.h"
#include "../syntax/LexicalHighlighter.h"
#include "../syntax/ColorScheme.h"
#include "../core/ConfigManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QFontDatabase>
#include <vector>

DiffViewerDialog::DiffViewerDialog(const QString& fileTitle,
                                   const QString& oldText,
                                   const QString& newText,
                                   const QString& langId,
                                   QWidget* parent,
                                   const QString& leftTitle,
                                   const QString& rightTitle)
    : QDialog(parent) {
    setWindowTitle("Two-Pane Diff — " + fileTitle);
    resize(1240, 760);

    auto* mainLay = new QVBoxLayout(this);

    auto* headerRow = new QHBoxLayout();
    auto* leftHdr = new QLabel(QString("<b>%1</b>").arg(leftTitle.toHtmlEscaped()), this);
    auto* rightHdr = new QLabel(QString("<b>%1</b>").arg(rightTitle.toHtmlEscaped()), this);
    headerRow->addWidget(leftHdr, 1);
    headerRow->addWidget(rightHdr, 1);
    mainLay->addLayout(headerRow);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_leftPane = new QPlainTextEdit(splitter);
    m_rightPane = new QPlainTextEdit(splitter);

    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto scheme = ColorScheme::loadByName(ConfigManager::instance().activeTheme());
    QString baseStyle = QString("QPlainTextEdit { background-color: %1; color: %2; }")
                            .arg(scheme.editorBg.name(), scheme.editorFg.name());

    for (auto* pane : {m_leftPane, m_rightPane}) {
        pane->setReadOnly(true);
        pane->setFont(monoFont);
        pane->setLineWrapMode(QPlainTextEdit::NoWrap);
        pane->setStyleSheet(baseStyle);
        auto* hl = new LexicalHighlighter(pane->document());
        hl->setColorScheme(scheme);
        hl->setLanguage(langId);
    }

    splitter->addWidget(m_leftPane);
    splitter->addWidget(m_rightPane);
    mainLay->addWidget(splitter, 1);

    connect(m_leftPane->verticalScrollBar(), &QScrollBar::valueChanged,
            m_rightPane->verticalScrollBar(), &QScrollBar::setValue);
    connect(m_rightPane->verticalScrollBar(), &QScrollBar::valueChanged,
            m_leftPane->verticalScrollBar(), &QScrollBar::setValue);
    connect(m_leftPane->horizontalScrollBar(), &QScrollBar::valueChanged,
            m_rightPane->horizontalScrollBar(), &QScrollBar::setValue);
    connect(m_rightPane->horizontalScrollBar(), &QScrollBar::valueChanged,
            m_leftPane->horizontalScrollBar(), &QScrollBar::setValue);

    computeAndRenderDiff(oldText, newText);
}

void DiffViewerDialog::computeAndRenderDiff(const QString& oldText, const QString& newText) {
    const QStringList oldLines = oldText.split('\n');
    const QStringList newLines = newText.split('\n');
    const int n = oldLines.size();
    const int m = newLines.size();

    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            if (oldLines[i] == newLines[j]) dp[i][j] = 1 + dp[i + 1][j + 1];
            else dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
        }
    }

    QStringList leftOut, rightOut;
    QList<int> removedRows, addedRows;
    int i = 0, j = 0, rowIdx = 0;
    int oldLineNo = 1, newLineNo = 1;

    while (i < n || j < m) {
        if (i < n && j < m && oldLines[i] == newLines[j]) {
            leftOut.append(QString("%1   %2").arg(oldLineNo++, 4).arg(oldLines[i++]));
            rightOut.append(QString("%1   %2").arg(newLineNo++, 4).arg(newLines[j++]));
        } else if (j < m && (i == n || dp[i][j + 1] >= dp[i + 1][j])) {
            leftOut.append(QString("       "));
            rightOut.append(QString("%1 + %2").arg(newLineNo++, 4).arg(newLines[j++]));
            addedRows.append(rowIdx);
        } else {
            leftOut.append(QString("%1 - %2").arg(oldLineNo++, 4).arg(oldLines[i++]));
            rightOut.append(QString("       "));
            removedRows.append(rowIdx);
        }
        ++rowIdx;
    }

    m_leftPane->setPlainText(leftOut.join('\n'));
    m_rightPane->setPlainText(rightOut.join('\n'));

    auto highlightRows = [](QPlainTextEdit* edit, const QList<int>& rows, const QColor& bgColor) {
        QList<QTextEdit::ExtraSelection> sels;
        for (int r : rows) {
            QTextBlock block = edit->document()->findBlockByNumber(r);
            if (!block.isValid()) continue;
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(bgColor);
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block);
            sels.append(sel);
        }
        edit->setExtraSelections(sels);
    };

    highlightRows(m_leftPane, removedRows, QColor("#4a2226"));
    highlightRows(m_rightPane, addedRows, QColor("#1e422b"));
}
