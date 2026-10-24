#include "MarkdownRenderer.h"
#include <QStringList>
#include <QRegularExpression>
#include <QFontDatabase>

void MarkdownRenderer::configureViewer(QTextEdit* viewer) {
    if (!viewer) return;
    viewer->document()->setDefaultStyleSheet(QString());
    QFont baseFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (baseFont.pointSize() <= 0) baseFont.setPointSize(10);
    viewer->setFont(baseFont);
    viewer->document()->setDefaultFont(baseFont);
    viewer->document()->setDocumentMargin(8);
}

QString MarkdownRenderer::toHtml(const QString& markdown) {
    if (markdown.trimmed().isEmpty()) return {};

    QString normalized = markdown;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    const QStringList lines = normalized.split('\n');

    QString html;
    html.reserve(markdown.size() * 2);
    html += "<div style='color:#f8f8f2; font-size:10pt; line-height:1.45;'>";

    bool inCodeBlock = false;
    QString codeLang;
    QStringList codeLines;

    bool inUl = false;
    bool inOl = false;
    bool inTable = false;
    bool tableHeaderDone = false;

    auto closeLists = [&]() {
        if (inUl) { html += "</ul>"; inUl = false; }
        if (inOl) { html += "</ol>"; inOl = false; }
    };

    auto closeTable = [&]() {
        if (inTable) {
            html += "</table>";
            inTable = false;
            tableHeaderDone = false;
        }
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString& rawLine = lines[i];
        const QString trimmed = rawLine.trimmed();

        // 1. Обработка ограждённых блоков кода ```lang ... ```
        if (trimmed.startsWith("```")) {
            closeLists();
            closeTable();
            if (!inCodeBlock) {
                inCodeBlock = true;
                codeLang = trimmed.mid(3).trimmed();
                codeLines.clear();
            } else {
                html += renderCodeBlock(codeLines.join('\n'), codeLang);
                inCodeBlock = false;
                codeLang.clear();
                codeLines.clear();
            }
            continue;
        }

        if (inCodeBlock) {
            codeLines.append(rawLine);
            continue;
        }

        // 2. Пустая строка
        if (trimmed.isEmpty()) {
            closeLists();
            closeTable();
            continue;
        }

        // 3. Горизонтальный разделитель (---, ***, ___)
        static const QRegularExpression rxHr(R"(^(?:-\s*){3,}$|^(?:\*\s*){3,}$|^(?:_\s*){3,}$)");
        if (rxHr.match(trimmed).hasMatch()) {
            closeLists();
            closeTable();
            html += "<hr style='border:none; border-top:1px solid #44475a; margin:10px 0;'/>";
            continue;
        }

        // 4. Таблицы Markdown (| col1 | col2 |)
        if (trimmed.startsWith('|') && trimmed.endsWith('|') && trimmed.count('|') >= 2) {
            closeLists();
            // Проверяем, не строка ли это разделителя |---|---|
            static const QRegularExpression rxSep(R"(^\|[\s:\-\|]+\|$)");
            if (rxSep.match(trimmed).hasMatch() && trimmed.contains('-')) {
                tableHeaderDone = true;
                continue;
            }

            if (!inTable) {
                inTable = true;
                tableHeaderDone = false;
                html += "<table cellspacing='0' cellpadding='6' "
                        "style='border-collapse:collapse; width:100%; margin:8px 0; border:1px solid #44475a;'>";
            }

            // Проверяем, является ли следующая строка разделителем заголовка таблицы
            bool isHeaderRow = !tableHeaderDone && (i + 1 < lines.size()) &&
                               rxSep.match(lines[i + 1].trimmed()).hasMatch() &&
                               lines[i + 1].contains('-');

            QString inner = trimmed.mid(1, trimmed.length() - 2);
            const QStringList cells = splitTableRow(inner);

            html += "<tr>";
            for (const QString& cell : cells) {
                if (isHeaderRow) {
                    html += QString("<th align='left' style='background-color:#282a36; color:#bd93f9; "
                                    "border:1px solid #44475a; padding:6px 8px; font-weight:bold;'>%1</th>")
                                .arg(formatInline(cell.trimmed()));
                } else {
                    html += QString("<td style='background-color:#1e1f29; color:#f8f8f2; "
                                    "border:1px solid #44475a; padding:5px 8px;'>%1</td>")
                                .arg(formatInline(cell.trimmed()));
                }
            }
            html += "</tr>";
            continue;
        } else {
            closeTable();
        }

        // 5. Заголовки (# .. ######)
        static const QRegularExpression rxHeading(R"(^(#{1,6})\s+(.+)$)");
        auto hm = rxHeading.match(trimmed);
        if (hm.hasMatch()) {
            closeLists();
            int level = hm.captured(1).length();
            QString title = formatInline(hm.captured(2).trimmed());
            int ptSize = (level == 1) ? 14 : (level == 2) ? 13 : (level == 3) ? 11 : 10;
            QString color = (level <= 2) ? "#bd93f9" : (level == 3 ? "#8be9fd" : "#ffb86c");
            html += QString("<div style='font-size:%1pt; font-weight:bold; color:%2; margin-top:10px; margin-bottom:4px;'>%3</div>")
                        .arg(ptSize)
                        .arg(color, title);
            continue;
        }

        // 6. Цитаты (> ...)
        if (trimmed.startsWith('>')) {
            closeLists();
            QString quoteBody = trimmed.mid(1).trimmed();
            html += QString("<div style='border-left:3px solid #bd93f9; background-color:#232530; "
                            "padding:6px 10px; margin:6px 0; color:#e2e2dc;'>%1</div>")
                        .arg(formatInline(quoteBody));
            continue;
        }

        // 7. Маркированные списки (-, *, +)
        static const QRegularExpression rxUl(R"(^[\-\*\+]\s+(.+)$)");
        auto ulm = rxUl.match(trimmed);
        if (ulm.hasMatch()) {
            if (inOl) { html += "</ol>"; inOl = false; }
            if (!inUl) {
                html += "<ul style='margin-top:4px; margin-bottom:6px; margin-left:18px;'>";
                inUl = true;
            }
            html += QString("<li style='margin-bottom:3px; color:#f8f8f2;'>%1</li>")
                        .arg(formatInline(ulm.captured(1).trimmed()));
            continue;
        }

        // 8. Нумерованные списки (1. ...)
        static const QRegularExpression rxOl(R"(^\d+[\.\)]\s+(.+)$)");
        auto olm = rxOl.match(trimmed);
        if (olm.hasMatch()) {
            if (inUl) { html += "</ul>"; inUl = false; }
            if (!inOl) {
                html += "<ol style='margin-top:4px; margin-bottom:6px; margin-left:18px;'>";
                inOl = true;
            }
            html += QString("<li style='margin-bottom:3px; color:#f8f8f2;'>%1</li>")
                        .arg(formatInline(olm.captured(1).trimmed()));
            continue;
        }

        // 9. Обычный абзац / строка
        closeLists();
        html += QString("<p style='margin-top:3px; margin-bottom:5px; color:#f8f8f2;'>%1</p>")
                    .arg(formatInline(trimmed));
    }

    if (inCodeBlock && !codeLines.isEmpty()) {
        html += renderCodeBlock(codeLines.join('\n'), codeLang);
    }
    closeLists();
    closeTable();

    html += "</div>";
    return html;
}

QString MarkdownRenderer::escapeHtml(QString s) {
    s.replace('&', "&amp;");
    s.replace('<', "&lt;");
    s.replace('>', "&gt;");
    s.replace('"', "&quot;");
    return s;
}

QStringList MarkdownRenderer::splitTableRow(const QString& row) {
    QStringList out;
    QString cur;
    bool inCode = false;
    for (int i = 0; i < row.length(); ++i) {
        QChar ch = row[i];
        if (ch == '`') {
            inCode = !inCode;
            cur += ch;
        } else if (ch == '|' && !inCode) {
            out.append(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.append(cur);
    return out;
}

QString MarkdownRenderer::renderCodeBlock(const QString& code, const QString& lang) {
    QString badge = lang.isEmpty() ? QString()
                                   : QString("<div style='background-color:#282a36; color:#8be9fd; font-size:9pt; "
                                             "font-weight:bold; padding:3px 8px; border:1px solid #44475a; "
                                             "border-bottom:none;'>%1</div>")
                                         .arg(escapeHtml(lang.toUpper()));
    return QString("%1<pre style='background-color:#191a22; color:#50fa7b; font-family:monospace; "
                   "font-size:9.5pt; padding:8px; margin-top:0; margin-bottom:8px; "
                   "border:1px solid #44475a; white-space:pre-wrap;'>%2</pre>")
        .arg(badge, escapeHtml(code));
}

QString MarkdownRenderer::formatInline(const QString& text) {
    // Сначала вырезаем инлайн-код `...` в плейсхолдеры, чтобы внутри него не срабатывали ** или HTML-теги
    QStringList codeSpans;
    QString work;
    work.reserve(text.length());

    int i = 0;
    while (i < text.length()) {
        if (text[i] == '`') {
            int end = text.indexOf('`', i + 1);
            if (end > i) {
                QString rawCode = text.mid(i + 1, end - i - 1);
                QString rendered = QString("<code style='font-family:monospace; background-color:#282a36; "
                                           "color:#8be9fd; padding:1px 4px;'>%1</code>")
                                       .arg(escapeHtml(rawCode));
                codeSpans.append(rendered);
                work += QString("\x01CODE%1\x01").arg(codeSpans.size() - 1);
                i = end + 1;
                continue;
            }
        }
        work += text[i];
        ++i;
    }

    work = escapeHtml(work);

    // Жирный + курсив (***...***)
    static const QRegularExpression rxBoldItalic(R"(\*\*\*(.+?)\*\*\*)");
    work.replace(rxBoldItalic, "<b style='color:#ffb86c;'><i>\\1</i></b>");

    // Жирный (**...** и __...__)
    static const QRegularExpression rxBold1(R"(\*\*(.+?)\*\*)");
    work.replace(rxBold1, "<b style='color:#ffffff;'>\\1</b>");
    static const QRegularExpression rxBold2(R"(__(.+?)__)");
    work.replace(rxBold2, "<b style='color:#ffffff;'>\\1</b>");

    // Курсив (*...* и _..._)
    static const QRegularExpression rxItalic1(R"((?<!\*)\*([^\*\n]+?)\*(?!\*))");
    work.replace(rxItalic1, "<i style='color:#e2e2dc;'>\\1</i>");

    // Ссылки [title](url)
    static const QRegularExpression rxLink(R"(\[([^\]]+)\]\(([^\)]+)\))");
    work.replace(rxLink, "<a href='\\2' style='color:#8be9fd;'>\\1</a>");

    // Возвращаем инлайн-код на место
    for (int idx = 0; idx < codeSpans.size(); ++idx) {
        work.replace(QString("\x01CODE%1\x01").arg(idx), codeSpans[idx]);
    }

    return work;
}
