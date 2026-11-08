// src/ui/MarkdownRenderer.cpp
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

    bool inMathBlock = false;
    QString mathCloseToken;
    QStringList mathLines;

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

        // 1. Блоки кода ```lang ... ```
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

        // 1b. Блочные формулы $$ ... $$ или \[ ... \]
        if (!inMathBlock && (trimmed.startsWith("$$") || trimmed.startsWith("\\["))) {
            closeLists();
            closeTable();
            const QString openTok = trimmed.startsWith("$$") ? QStringLiteral("$$") : QStringLiteral("\\[");
            const QString closeTok = trimmed.startsWith("$$") ? QStringLiteral("$$") : QStringLiteral("\\]");

            if (trimmed.length() > openTok.length() && trimmed.endsWith(closeTok)) {
                QString innerMath = trimmed.mid(openTok.length(),
                                                trimmed.length() - openTok.length() - closeTok.length()).trimmed();
                html += renderLatexMath(innerMath, true);
            } else {
                inMathBlock = true;
                mathCloseToken = closeTok;
                mathLines.clear();
                QString firstPart = trimmed.mid(openTok.length()).trimmed();
                if (!firstPart.isEmpty()) mathLines.append(firstPart);
            }
            continue;
        }

        if (inMathBlock) {
            if (trimmed.endsWith(mathCloseToken)) {
                QString lastPart = trimmed.left(trimmed.length() - mathCloseToken.length()).trimmed();
                if (!lastPart.isEmpty()) mathLines.append(lastPart);
                html += renderLatexMath(mathLines.join('\n'), true);
                inMathBlock = false;
                mathCloseToken.clear();
                mathLines.clear();
            } else {
                mathLines.append(trimmed);
            }
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

        // 9. Обычный абзац
        closeLists();
        html += QString("<p style='margin-top:3px; margin-bottom:5px; color:#f8f8f2;'>%1</p>")
                    .arg(formatInline(trimmed));
    }

    if (inCodeBlock && !codeLines.isEmpty()) {
        html += renderCodeBlock(codeLines.join('\n'), codeLang);
    }
    if (inMathBlock && !mathLines.isEmpty()) {
        html += renderLatexMath(mathLines.join('\n'), true);
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

QString MarkdownRenderer::decodeCommonHtmlEntities(QString s) {
    if (!s.contains('&')) return s;
    struct EntityMap { const char* ent; const char16_t* uni; };
    static constexpr EntityMap kEntities[] = {
        {"&rarr;", u"\u2192"},   {"&larr;", u"\u2190"},   {"&harr;", u"\u2194"},
        {"&rArr;", u"\u21D2"},   {"&lArr;", u"\u21D0"},   {"&hArr;", u"\u21D4"},
        {"&uarr;", u"\u2191"},   {"&darr;", u"\u2193"},   {"&mdash;", u"\u2014"},
        {"&ndash;", u"\u2013"},  {"&hellip;", u"\u2026"}, {"&bull;", u"\u2022"},
        {"&middot;", u"\u00B7"}, {"&times;", u"\u00D7"},  {"&divide;", u"\u00F7"},
        {"&plusmn;", u"\u00B1"}, {"&le;", u"\u2264"},     {"&ge;", u"\u2265"},
        {"&ne;", u"\u2260"},     {"&asymp;", u"\u2248"},  {"&equiv;", u"\u2261"},
        {"&infin;", u"\u221E"},  {"&deg;", u"\u00B0"},    {"&nbsp;", u"\u00A0"}
    };
    for (const auto& e : kEntities) {
        s.replace(QLatin1String(e.ent), QString::fromUtf16(e.uni), Qt::CaseInsensitive);
    }
    return s;
}

QString MarkdownRenderer::replaceBareLatexArrowsAndSymbols(QString s) {
    if (!s.contains('\\')) return s;

    struct BareSym { const char* cmd; const char16_t* uni; };
    static constexpr BareSym kBareSymbols[] = {
        {"\\Longleftrightarrow", u"\u27FA"}, {"\\Longrightarrow", u"\u27F9"},
        {"\\Longleftarrow", u"\u27F8"},      {"\\longleftrightarrow", u"\u27F7"},
        {"\\longrightarrow", u"\u27F6"},     {"\\longleftarrow", u"\u27F5"},
        {"\\Leftrightarrow", u"\u21D4"},     {"\\leftrightarrow", u"\u2194"},
        {"\\hookrightarrow", u"\u21AA"},     {"\\hookleftarrow", u"\u21A9"},
        {"\\Rightarrow", u"\u21D2"},         {"\\Leftarrow", u"\u21D0"},
        {"\\rightarrow", u"\u2192"},         {"\\leftarrow", u"\u2190"},
        {"\\updownarrow", u"\u2195"},        {"\\uparrow", u"\u2191"},
        {"\\downarrow", u"\u2193"},          {"\\mapsto", u"\u21A6"},
        {"\\implies", u"\u21D2"},            {"\\iff", u"\u21D4"},
        {"\\leqslant", u"\u2264"},           {"\\geqslant", u"\u2265"},
        {"\\leq", u"\u2264"},                {"\\geq", u"\u2265"},
        {"\\neq", u"\u2260"},                {"\\approx", u"\u2248"},
        {"\\equiv", u"\u2261"},              {"\\times", u"\u00D7"},
        {"\\div", u"\u00F7"},                {"\\pm", u"\u00B1"},
        {"\\infty", u"\u221E"},              {"\\checkmark", u"\u2713"}
    };

    for (const auto& b : kBareSymbols) {
        // Не заменяем внутри путей Windows (после буквы диска/слэша/цифры)
        QString pattern = QStringLiteral("(?<![A-Za-z0-9_:/\\\\])")
                          + QRegularExpression::escape(QString::fromLatin1(b.cmd))
                          + QStringLiteral("(?![A-Za-z0-9_/\\\\])");
        QRegularExpression rx(pattern);
        s.replace(rx, QString::fromUtf16(b.uni));
    }
    return s;
}

QStringList MarkdownRenderer::splitTableRow(const QString& row) {
    QStringList out;
    QString cur;
    bool inCode = false;
    bool inMath = false;
    for (int i = 0; i < row.length(); ++i) {
        QChar ch = row[i];
        if (ch == '`') {
            inCode = !inCode;
            cur += ch;
        } else if (ch == '$' && !inCode) {
            if (!inMath) {
                // Включаем режим формулы только если в строке есть закрывающий '$'
                int nextDollar = row.indexOf('$', i + 1);
                if (nextDollar > i) {
                    inMath = true;
                }
            } else {
                inMath = false;
            }
            cur += ch;
        } else if (ch == '|' && !inCode && !inMath) {
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

QString MarkdownRenderer::renderLatexMath(const QString& rawLatex, bool displayMode) {
    QString s = rawLatex.trimmed();
    if (s.isEmpty()) return {};

    // 0. Сохраняем экранированные фигурные скобки \{ и \} в плейсхолдеры
    s.replace("\\{", "\x02LBRACE\x02");
    s.replace("\\}", "\x02RBRACE\x02");

    // 1. Извлекаем текстовые макросы \text{...}, \mathrm{...}, \textrm{...}, \operatorname{...}, \mbox{...}
    QStringList textSpans;
    static const QRegularExpression rxTextMacro(
        R"(\\(?:text|textrm|textsf|mathrm|operatorname\*?|mbox|hbox)\s*\{([^{}]*)\})");
    while (true) {
        auto m = rxTextMacro.match(s);
        if (!m.hasMatch()) break;
        QString rawInner = m.captured(1);
        QString escapedInner = escapeHtml(rawInner);
        if (escapedInner.startsWith(' ')) {
            escapedInner.replace(0, 1, "&nbsp;");
        }
        if (escapedInner.endsWith(' ') && escapedInner.length() > 1) {
            escapedInner.chop(1);
            escapedInner += "&nbsp;";
        }
        int idx = textSpans.size();
        textSpans.append(escapedInner);
        s.replace(m.capturedStart(), m.capturedLength(), QString("\x02TXT%1\x02").arg(idx));
    }

    // 1b. Стрелки с надписями \xrightarrow[sub]{sup} и \xleftarrow[sub]{sup}
    static const QRegularExpression rxXRightArrowOpt(R"(\\xrightarrow\s*\[([^\[\]]*)\]\s*\{([^{}]*)\})");
    s.replace(rxXRightArrowOpt, QStringLiteral(" \u2014[\\2 / \\1]\u2192 "));
    static const QRegularExpression rxXRightArrow(R"(\\xrightarrow\s*\{([^{}]*)\})");
    while (true) {
        auto m = rxXRightArrow.match(s);
        if (!m.hasMatch()) break;
        QString lbl = m.captured(1).trimmed();
        QString repl = lbl.isEmpty() ? QStringLiteral("\u2192") : QStringLiteral(" \u2014(%1)\u2192 ").arg(lbl);
        s.replace(m.capturedStart(), m.capturedLength(), repl);
    }

    static const QRegularExpression rxXLeftArrowOpt(R"(\\xleftarrow\s*\[([^\[\]]*)\]\s*\{([^{}]*)\})");
    s.replace(rxXLeftArrowOpt, QStringLiteral(" \u2190[\\2 / \\1]\u2014 "));
    static const QRegularExpression rxXLeftArrow(R"(\\xleftarrow\s*\{([^{}]*)\})");
    while (true) {
        auto m = rxXLeftArrow.match(s);
        if (!m.hasMatch()) break;
        QString lbl = m.captured(1).trimmed();
        QString repl = lbl.isEmpty() ? QStringLiteral("\u2190") : QStringLiteral(" \u2190(%1)\u2014 ").arg(lbl);
        s.replace(m.capturedStart(), m.capturedLength(), repl);
    }

    // 2. Градусы, штрихи и отрицания с \not
    static const QRegularExpression rxDegree(
        R"(\^\s*\{\s*\\circ\s*\}|\^\s*\\circ(?![A-Za-z])|\\degree(?![A-Za-z]))");
    s.replace(rxDegree, QStringLiteral("\u00B0"));

    static const QRegularExpression rxDoublePrime(
        R"(\^\s*\{\s*\\prime\s*\\prime\s*\})");
    s.replace(rxDoublePrime, QStringLiteral("\u2033"));

    static const QRegularExpression rxSinglePrime(
        R"(\^\s*\{\s*\\prime\s*\}|\^\s*\\prime(?![A-Za-z])|\\prime(?![A-Za-z]))");
    s.replace(rxSinglePrime, QStringLiteral("\u2032"));

    // Комбинации \not=, \not\in, \not\equiv, \not\subset, \not\subseteq, \not\approx, \not\sim
    s.replace(QRegularExpression(R"(\\not\s*=)"), QStringLiteral("\u2260"));
    s.replace(QRegularExpression(R"(\\not\s*\\in(?![A-Za-z]))"), QStringLiteral("\u2209"));
    s.replace(QRegularExpression(R"(\\not\s*\\equiv(?![A-Za-z]))"), QStringLiteral("\u2262"));
    s.replace(QRegularExpression(R"(\\not\s*\\subseteq(?![A-Za-z]))"), QStringLiteral("\u2288"));
    s.replace(QRegularExpression(R"(\\not\s*\\subset(?![A-Za-z]))"), QStringLiteral("\u2284"));
    s.replace(QRegularExpression(R"(\\not\s*\\supseteq(?![A-Za-z]))"), QStringLiteral("\u2289"));
    s.replace(QRegularExpression(R"(\\not\s*\\supset(?![A-Za-z]))"), QStringLiteral("\u2285"));
    s.replace(QRegularExpression(R"(\\not\s*\\approx(?![A-Za-z]))"), QStringLiteral("\u2249"));
    s.replace(QRegularExpression(R"(\\not\s*\\sim(?![A-Za-z]))"), QStringLiteral("\u2241"));
    s.replace(QRegularExpression(R"(\\not\s*\\parallel(?![A-Za-z]))"), QStringLiteral("\u2226"));

    // 3. Окружения \begin{...} / \end{...} и размерные модификаторы LaTeX
    s.replace(QRegularExpression(R"(\\begin\s*\{pmatrix\})"), "(");
    s.replace(QRegularExpression(R"(\\end\s*\{pmatrix\})"), ")");
    s.replace(QRegularExpression(R"(\\begin\s*\{bmatrix\})"), "[");
    s.replace(QRegularExpression(R"(\\end\s*\{bmatrix\})"), "]");
    s.replace(QRegularExpression(R"(\\begin\s*\{Bmatrix\})"), "\x02LBRACE\x02");
    s.replace(QRegularExpression(R"(\\end\s*\{Bmatrix\})"), "\x02RBRACE\x02");
    s.replace(QRegularExpression(R"(\\begin\s*\{vmatrix\})"), "|");
    s.replace(QRegularExpression(R"(\\end\s*\{vmatrix\})"), "|");
    s.replace(QRegularExpression(R"(\\begin\s*\{Vmatrix\})"), QStringLiteral("\u2016"));
    s.replace(QRegularExpression(R"(\\end\s*\{Vmatrix\})"), QStringLiteral("\u2016"));
    s.remove(QRegularExpression(R"(\\(?:begin|end)\s*\{[A-Za-z0-9\*]+\}(?:\s*\{[^{}]*\})?)"));

    // Невидимые ограничители \left. и \right.
    s.remove(QRegularExpression(R"(\\(?:left|right|middle)\s*\.)"));

    static const QRegularExpression rxLeftRight(
        R"(\\(?:left|right|middle|big|Big|bigg|Bigg|bigl|bigr|Bigl|Bigr|biggl|biggr|Biggl|Biggr|limits|nolimits|displaystyle|textstyle|scriptstyle|scriptscriptstyle|ensuremath)(?![A-Za-z]))");
    s.remove(rxLeftRight);

    // Двойная вертикальная черта \| -> ‖ (до обработки \\)
    s.replace("\\|", QStringLiteral("\u2016"));

    // Перенос строки \\ в блочных/строчных формулах (до замены "\ ")
    s.replace("\\\\", "\x02MATHBR\x02");

    s.replace("\\,", " ");
    s.replace("\\;", " ");
    s.replace("\\:", " ");
    s.replace("\\>", " ");
    s.replace("\\!", "");
    s.replace("\\enspace", " ");
    s.replace("\\quad", "  ");
    s.replace("\\qquad", "    ");
    s.replace("\\ ", " ");
    s.replace("\\%", "%");
    s.replace("\\$", "$");
    s.replace("\\&", "&");
    s.replace("\\#", "#");
    s.replace("\\_", "_");

    static const QRegularExpression rxUnescapedTilde(R"((?<!\\)~)");
    s.replace(rxUnescapedTilde, " ");

    // Разделитель колонок & в окружениях aligned / cases / matrix
    s.replace(QRegularExpression(R"((?<!\\)&)"), "  ");

    // 4. Таблица символов LaTeX -> Unicode (с обязательным QRegularExpression::escape!)
    struct SymMap { const char* cmd; const char16_t* uni; };
    static constexpr SymMap kSymbols[] = {
        {"\\Longleftrightarrow", u"\u27FA"}, {"\\iff", u"\u21D4"},
        {"\\Longrightarrow", u"\u27F9"},     {"\\implies", u"\u21D2"},
        {"\\Longleftarrow", u"\u27F8"},      {"\\impliedby", u"\u21D0"},
        {"\\longleftrightarrow", u"\u27F7"}, {"\\longrightarrow", u"\u27F6"},
        {"\\longleftarrow", u"\u27F5"},      {"\\longmapsto", u"\u27FC"},
        {"\\leftrightarrow", u"\u2194"},     {"\\Leftrightarrow", u"\u21D4"},
        {"\\rightleftharpoons", u"\u21CC"},  {"\\hookrightarrow", u"\u21AA"},
        {"\\hookleftarrow", u"\u21A9"},      {"\\rightharpoonup", u"\u21C0"},
        {"\\rightharpoondown", u"\u21C1"},   {"\\leftharpoonup", u"\u21BC"},
        {"\\leftharpoondown", u"\u21BD"},    {"\\rightarrow", u"\u2192"},
        {"\\to", u"\u2192"},                 {"\\leftarrow", u"\u2190"},
        {"\\gets", u"\u2190"},               {"\\Rightarrow", u"\u21D2"},
        {"\\Leftarrow", u"\u21D0"},          {"\\updownarrow", u"\u2195"},
        {"\\Updownarrow", u"\u21D5"},        {"\\uparrow", u"\u2191"},
        {"\\Uparrow", u"\u21D1"},            {"\\downarrow", u"\u2193"},
        {"\\Downarrow", u"\u21D3"},          {"\\mapsto", u"\u21A6"},
        {"\\nearrow", u"\u2197"},            {"\\searrow", u"\u2198"},
        {"\\swarrow", u"\u2199"},            {"\\nwarrow", u"\u2196"},
        {"\\leqslant", u"\u2264"},           {"\\geqslant", u"\u2265"},
        {"\\leq", u"\u2264"},                {"\\le", u"\u2264"},
        {"\\geq", u"\u2265"},                {"\\ge", u"\u2265"},
        {"\\neq", u"\u2260"},                {"\\ne", u"\u2260"},
        {"\\approxeq", u"\u224A"},           {"\\approx", u"\u2248"},
        {"\\equiv", u"\u2261"},              {"\\cong", u"\u2245"},
        {"\\simeq", u"\u2243"},              {"\\sim", u"\u223C"},
        {"\\propto", u"\u221D"},             {"\\asymp", u"\u224D"},
        {"\\doteq", u"\u2250"},              {"\\triangleq", u"\u225C"},
        {"\\coloneqq", u":="},               {"\\eqqcolon", u"=:"},
        {"\\ll", u"\u226A"},                 {"\\gg", u"\u226B"},
        {"\\preceq", u"\u2AAF"},             {"\\succeq", u"\u2AB0"},
        {"\\prec", u"\u227A"},               {"\\succ", u"\u227B"},
        {"\\times", u"\u00D7"},              {"\\div", u"\u00F7"},
        {"\\cdot", u"\u00B7"},               {"\\ast", u"\u2217"},
        {"\\star", u"\u22C6"},               {"\\circ", u"\u2218"},
        {"\\bullet", u"\u2219"},             {"\\pm", u"\u00B1"},
        {"\\mp", u"\u2213"},                 {"\\oplus", u"\u2295"},
        {"\\ominus", u"\u2296"},             {"\\otimes", u"\u2297"},
        {"\\oslash", u"\u2298"},             {"\\odot", u"\u2299"},
        {"\\dagger", u"\u2020"},             {"\\ddagger", u"\u2021"},
        {"\\amalg", u"\u2A3F"},              {"\\infty", u"\u221E"},
        {"\\partial", u"\u2202"},            {"\\nabla", u"\u2207"},
        {"\\forall", u"\u2200"},             {"\\nexists", u"\u2204"},
        {"\\exists", u"\u2203"},             {"\\emptyset", u"\u2205"},
        {"\\varnothing", u"\u2205"},         {"\\notin", u"\u2209"},
        {"\\in", u"\u2208"},                 {"\\ni", u"\u220B"},
        {"\\nsubseteq", u"\u2288"},          {"\\subsetneq", u"\u228A"},
        {"\\subseteq", u"\u2286"},           {"\\supseteq", u"\u2287"},
        {"\\subset", u"\u2282"},             {"\\supset", u"\u2283"},
        {"\\bigcup", u"\u22C3"},             {"\\bigcap", u"\u22C2"},
        {"\\bigsqcup", u"\u2A06"},           {"\\bigoplus", u"\u2A01"},
        {"\\bigotimes", u"\u2A02"},          {"\\bigodot", u"\u2A00"},
        {"\\bigvee", u"\u22C1"},             {"\\bigwedge", u"\u22C0"},
        {"\\cup", u"\u222A"},                {"\\cap", u"\u2229"},
        {"\\smallsetminus", u"\u2216"},      {"\\setminus", u"\u2216"},
        {"\\land", u"\u2227"},               {"\\wedge", u"\u2227"},
        {"\\lor", u"\u2228"},                {"\\vee", u"\u2228"},
        {"\\lnot", u"\u00AC"},               {"\\neg", u"\u00AC"},
        {"\\top", u"\u22A4"},                {"\\bot", u"\u22A5"},
        {"\\perp", u"\u22A5"},               {"\\nparallel", u"\u2226"},
        {"\\parallel", u"\u2225"},           {"\\nmid", u"\u2224"},
        {"\\mid", u"|"},                     {"\\vdash", u"\u22A2"},
        {"\\models", u"\u22A8"},             {"\\measuredangle", u"\u2221"},
        {"\\angle", u"\u2220"},              {"\\triangle", u"\u25B3"},
        {"\\square", u"\u25A1"},             {"\\Box", u"\u25A1"},
        {"\\Diamond", u"\u25C7"},            {"\\diamond", u"\u22C4"},
        {"\\coprod", u"\u2210"},             {"\\sum", u"\u2211"},
        {"\\prod", u"\u220F"},               {"\\iiint", u"\u222D"},
        {"\\iint", u"\u222C"},               {"\\oint", u"\u222E"},
        {"\\int", u"\u222B"},                {"\\ldots", u"\u2026"},
        {"\\cdots", u"\u22EF"},              {"\\dots", u"\u2026"},
        {"\\vdots", u"\u22EE"},              {"\\ddots", u"\u22F1"},
        {"\\checkmark", u"\u2713"},          {"\\langle", u"\u27E8"},
        {"\\rangle", u"\u27E9"},             {"\\lfloor", u"\u230A"},
        {"\\rfloor", u"\u230B"},             {"\\lceil", u"\u2308"},
        {"\\rceil", u"\u2309"},              {"\\Vert", u"\u2016"},
        {"\\vert", u"|"},                    {"\\backslash", u"\\"},
        {"\\colon", u":"},                   {"\\hbar", u"\u210F"},
        {"\\ell", u"\u2113"},                {"\\Re", u"\u211C"},
        {"\\Im", u"\u2111"},                {"\\wp", u"\u2118"},
        {"\\aleph", u"\u2135"},
        // Греческий алфавит
        {"\\alpha", u"\u03B1"},   {"\\beta", u"\u03B2"},    {"\\gamma", u"\u03B3"},   {"\\delta", u"\u03B4"},
        {"\\varepsilon", u"\u03B5"}, {"\\epsilon", u"\u03B5"}, {"\\zeta", u"\u03B6"}, {"\\eta", u"\u03B7"},
        {"\\vartheta", u"\u03D1"}, {"\\theta", u"\u03B8"},  {"\\iota", u"\u03B9"},    {"\\kappa", u"\u03BA"},
        {"\\lambda", u"\u03BB"},  {"\\mu", u"\u03BC"},      {"\\nu", u"\u03BD"},      {"\\xi", u"\u03BE"},
        {"\\varpi", u"\u03D6"},   {"\\pi", u"\u03C0"},      {"\\varrho", u"\u03F1"},  {"\\rho", u"\u03C1"},
        {"\\varsigma", u"\u03C2"}, {"\\sigma", u"\u03C3"},  {"\\tau", u"\u03C4"},     {"\\upsilon", u"\u03C5"},
        {"\\varphi", u"\u03C6"},  {"\\phi", u"\u03D5"},     {"\\chi", u"\u03C7"},     {"\\psi", u"\u03C8"},
        {"\\omega", u"\u03C9"},
        {"\\Gamma", u"\u0393"},   {"\\Delta", u"\u0394"},   {"\\Theta", u"\u0398"},   {"\\Lambda", u"\u039B"},
        {"\\Xi", u"\u039E"},      {"\\Pi", u"\u03A0"},      {"\\Sigma", u"\u03A3"},   {"\\Upsilon", u"\u03A5"},
        {"\\Phi", u"\u03A6"},     {"\\Psi", u"\u03A8"},     {"\\Omega", u"\u03A9"},
        // Стандартные функции
        {"\\arcsin", u"arcsin"},  {"\\arccos", u"arccos"},  {"\\arctan", u"arctan"},
        {"\\sinh", u"sinh"},      {"\\cosh", u"cosh"},      {"\\tanh", u"tanh"},
        {"\\sin", u"sin"},        {"\\cos", u"cos"},        {"\\tan", u"tan"},
        {"\\cot", u"cot"},        {"\\sec", u"sec"},        {"\\csc", u"csc"},
        {"\\log", u"log"},        {"\\ln", u"ln"},          {"\\exp", u"exp"},
        {"\\min", u"min"},        {"\\max", u"max"},        {"\\sup", u"sup"},
        {"\\inf", u"inf"},        {"\\lim", u"lim"},        {"\\mod", u"mod"},
        {"\\det", u"det"},        {"\\dim", u"dim"},        {"\\gcd", u"gcd"},
        {"\\deg", u"deg"}
    };

    for (const auto& m : kSymbols) {
        QRegularExpression rx(QRegularExpression::escape(QString::fromLatin1(m.cmd)) + R"((?![A-Za-z]))");
        s.replace(rx, QString::fromUtf16(m.uni));
    }

    // ASCII-стрелки внутри формул ($A -> B$, $A <-> B$, $A => B$)
    s.replace("<->", QStringLiteral("\u2194"));
    s.replace("<=>", QStringLiteral("\u21D4"));
    s.replace("-->", QStringLiteral("\u27F6"));
    s.replace("<--", QStringLiteral("\u27F5"));
    s.replace("->", QStringLiteral("\u2192"));
    s.replace("<-", QStringLiteral("\u2190"));
    s.replace("=>", QStringLiteral("\u21D2"));

    s = escapeHtml(s);

    // 5. Шрифтовые и декоративные макросы (\mathbf, \mathit, \mathtt, \mathcal, \overline, \underline, \vec, \hat...)
    static const QRegularExpression rxBoldMath(R"(\\(?:mathbf|boldsymbol|textbf|bm)\s*\{([^{}]*)\})");
    s.replace(rxBoldMath, "<b>\\1</b>");
    static const QRegularExpression rxItalicMath(R"(\\(?:mathit|textit)\s*\{([^{}]*)\})");
    s.replace(rxItalicMath, "<i>\\1</i>");
    static const QRegularExpression rxMonoMath(R"(\\(?:mathtt|texttt)\s*\{([^{}]*)\})");
    s.replace(rxMonoMath, "<code style='font-family:monospace; color:#8be9fd;'>\\1</code>");
    static const QRegularExpression rxCalMath(R"(\\(?:mathcal|mathbb|mathfrak|mathscr)\s*\{([^{}]*)\})");
    s.replace(rxCalMath, "<b>\\1</b>");

    static const QRegularExpression rxOverline(R"(\\(?:overline|bar)\s*\{([^{}]*)\})");
    s.replace(rxOverline, "<span style='text-decoration:overline;'>\\1</span>");
    static const QRegularExpression rxUnderline(R"(\\underline\s*\{([^{}]*)\})");
    s.replace(rxUnderline, "<u>\\1</u>");
    static const QRegularExpression rxCancel(R"(\\(?:cancel|bcancel|xcancel)\s*\{([^{}]*)\})");
    s.replace(rxCancel, "<s>\\1</s>");
    static const QRegularExpression rxBoxed(R"(\\boxed\s*\{([^{}]*)\})");
    s.replace(rxBoxed, "<span style='border:1px solid #f1fa8c; padding:1px 4px;'>\\1</span>");
    static const QRegularExpression rxVec(R"(\\(?:vec|overrightarrow)\s*\{([^{}]*)\})");
    s.replace(rxVec, QStringLiteral("\\1\u20D7"));
    static const QRegularExpression rxHat(R"(\\(?:hat|widehat)\s*\{([^{}]*)\})");
    s.replace(rxHat, QStringLiteral("\\1\u0302"));
    static const QRegularExpression rxTilde(R"(\\(?:tilde|widetilde)\s*\{([^{}]*)\})");
    s.replace(rxTilde, QStringLiteral("\\1\u0303"));
    static const QRegularExpression rxDdot(R"(\\ddot\s*\{([^{}]*)\})");
    s.replace(rxDdot, QStringLiteral("\\1\u0308"));
    static const QRegularExpression rxDot(R"(\\dot\s*\{([^{}]*)\})");
    s.replace(rxDot, QStringLiteral("\\1\u0307"));

    // 6. Индексы (^{...}, ^x, _{...}, _x), дроби \frac/\dfrac/\tfrac, биномы \binom и корни \sqrt
    static const QRegularExpression rxSupBrace(R"(\^\s*\{([^{}]+)\})");
    static const QRegularExpression rxSupChar(R"(\^([^\s<>\{\}\^_]))");
    static const QRegularExpression rxSubBrace(R"(_\s*\{([^{}]+)\})");
    static const QRegularExpression rxSubChar(R"(_([^\s<>\{\}\^_]))");
    static const QRegularExpression rxFrac(R"(\\(?:d|t|c)?frac\s*\{([^{}]*)\}\s*\{([^{}]*)\})");
    static const QRegularExpression rxBinom(R"(\\(?:d|t)?binom\s*\{([^{}]*)\}\s*\{([^{}]*)\})");
    static const QRegularExpression rxSqrtNth(R"(\\sqrt\s*\[([^\[\]]+)\]\s*\{([^{}]*)\})");
    static const QRegularExpression rxSqrtBrace(R"(\\sqrt\s*\{([^{}]*)\})");

    for (int pass = 0; pass < 4; ++pass) {
        QString before = s;
        s.replace(rxSupBrace, "<sup>\\1</sup>");
        s.replace(rxSupChar, "<sup>\\1</sup>");
        s.replace(rxSubBrace, "<sub>\\1</sub>");
        s.replace(rxSubChar, "<sub>\\1</sub>");
        s.replace(rxFrac, "<span style='white-space:nowrap;'>(\\1)/(\\2)</span>");
        s.replace(rxBinom, "<span style='white-space:nowrap;'>C(\\1, \\2)</span>");
        s.replace(rxSqrtNth, QStringLiteral("<sup>\\1</sup>\u221A(\\2)"));
        s.replace(rxSqrtBrace, QStringLiteral("\u221A(\\1)"));
        if (s == before) break;
    }
    s.replace("\\sqrt", QStringLiteral("\u221A"));

    // Очистка оставшихся группирующих фигурных скобок и восстановление экранированных \{ и \}
    s.remove('{');
    s.remove('}');
    s.replace("\x02LBRACE\x02", "{");
    s.replace("\x02RBRACE\x02", "}");
    s.replace("\x02MATHBR\x02", displayMode ? "<br/>" : "; ");

    // 7. Возвращаем сохранённые \text{...} спаны
    for (int idx = 0; idx < textSpans.size(); ++idx) {
        s.replace(QString("\x02TXT%1\x02").arg(idx), textSpans[idx]);
    }

    if (displayMode) {
        return QString("<div style='background-color:#232530; border-left:3px solid #f1fa8c; "
                       "padding:6px 12px; margin:6px 0; color:#f1fa8c; font-size:10.5pt;'>%1</div>")
            .arg(s);
    }

    return QString("<span style='color:#f1fa8c; white-space:nowrap;'>%1</span>").arg(s);
}

QString MarkdownRenderer::formatInline(const QString& text) {
    // 1. Вырезаем инлайн-код `...` в плейсхолдеры
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

    // 2. Вырезаем математические выражения $$...$$, \[...\], \(...\) и $...$
    QStringList mathSpans;
    auto extractMathPattern = [&](const QRegularExpression& rx, bool displayMode) {
        while (true) {
            auto m = rx.match(work);
            if (!m.hasMatch()) break;
            QString rendered = renderLatexMath(m.captured(1), displayMode);
            int idx = mathSpans.size();
            mathSpans.append(rendered);
            work.replace(m.capturedStart(), m.capturedLength(), QString("\x01MATH%1\x01").arg(idx));
        }
    };

    static const QRegularExpression rxInlineDisplayMath(R"(\$\$([^\$\n]+?)\$\$)");
    static const QRegularExpression rxBracketMath(R"(\\\[(.+?)\\\])");
    static const QRegularExpression rxParenMath(R"(\\\((.+?)\\\))");
    extractMathPattern(rxInlineDisplayMath, false);
    extractMathPattern(rxBracketMath, false);
    extractMathPattern(rxParenMath, false);

    // Одиночные $...$: поддерживаем любые LaTeX-конструкции (включая $\rightarrow$2D),
    // отсекая только валютные диапазоны вроде "$100 - $200"
    static const QRegularExpression rxInlineMath(R"((?<!\\)\$([^\$\n]+?)(?<!\\)\$)");
    int searchPos = 0;
    while (true) {
        auto m = rxInlineMath.match(work, searchPos);
        if (!m.hasMatch()) break;

        QString rawInner = m.captured(1);
        QString trimmedInner = rawInner.trimmed();
        bool hasLatexMarkers = rawInner.contains('\\') || rawInner.contains('^') ||
                               rawInner.contains('_') || rawInner.contains('{') ||
                               rawInner.contains('}');
        bool hasLeadingOrTrailingSpace = !rawInner.isEmpty() &&
                                         (rawInner.front().isSpace() || rawInner.back().isSpace());
        bool followedByDigit = (m.capturedEnd() < work.length()) && work[m.capturedEnd()].isDigit();
        bool looksLikeCurrencyRange = !trimmedInner.isEmpty() &&
                                      trimmedInner.front().isDigit() &&
                                      !hasLatexMarkers &&
                                      !trimmedInner.contains('=') &&
                                      !trimmedInner.contains('<') &&
                                      !trimmedInner.contains('>');

        if (trimmedInner.isEmpty() ||
            (!hasLatexMarkers && (hasLeadingOrTrailingSpace || followedByDigit || looksLikeCurrencyRange))) {
            searchPos = m.capturedEnd() - 1;
            continue;
        }

        QString rendered = renderLatexMath(trimmedInner, false);
        int idx = mathSpans.size();
        mathSpans.append(rendered);
        QString placeholder = QString("\x01MATH%1\x01").arg(idx);
        work.replace(m.capturedStart(), m.capturedLength(), placeholder);
        searchPos = m.capturedStart() + placeholder.length();
    }

    // 3. Обрабатываем HTML-сущности (&rarr; и др.) и одиночные LaTeX-стрелки без $...$
    work = decodeCommonHtmlEntities(work);
    work = replaceBareLatexArrowsAndSymbols(work);

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

    // Возвращаем математические выражения и инлайн-код на место
    for (int idx = 0; idx < mathSpans.size(); ++idx) {
        work.replace(QString("\x01MATH%1\x01").arg(idx), mathSpans[idx]);
    }
    for (int idx = 0; idx < codeSpans.size(); ++idx) {
        work.replace(QString("\x01CODE%1\x01").arg(idx), codeSpans[idx]);
    }

    return work;
}
