// src/ui/MarkdownRenderer.h
#pragma once
#include <QString>
#include <QTextEdit>

class MarkdownRenderer {
    MarkdownRenderer() = default;
public:
    static void configureViewer(QTextEdit* viewer);
    static QString toHtml(const QString& markdown);
private:
    static QString escapeHtml(QString s);
    static QStringList splitTableRow(const QString& row);
    static QString renderCodeBlock(const QString& code, const QString& lang);
    static QString formatInline(const QString& text);
};
