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

        // 1. Ограждённые блоки кода ```lang ... ```
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

        // 1b. Блочные математические формулы $$ ... $$ и \[ ... \]
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
                html += renderLatexMath(mathLines.join(' '), true);
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
        html += renderLatexMath(mathLines.join(' '), true);
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
    bool inMath = false;
    for (int i = 0; i < row.length(); ++i) {
        QChar ch = row[i];
        if (ch == '`') {
            inCode = !inCode;
            cur += ch;
        } else if (ch == '$' && !inCode) {
            inMath = !inMath;
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

    // 1. Извлекаем текстовые блоки \text{...}, \mathrm{...}, \textrm{...}, \operatorname{...}, \mbox{...}
    // в плейсхолдеры, чтобы кириллица, пробелы и единицы измерения (" км", " м") не искажались
    QStringList textSpans;
    static const QRegularExpression rxTextMacro(
        R"(\\(?:text|textrm|mathrm|operatorname|mbox)\s*\{([^{}]*)\})");
    while (true) {
        auto m = rxTextMacro.match(s);
        if (!m.hasMatch()) break;
        QString rawInner = m.captured(1);
        QString escapedInner = escapeHtml(rawInner);
        // Сохраняем ведущий/завершающий пробел внутри \text{ км} как неразрывный,
        // чтобы HTML-рендерер гарантированно не склеивал число с единицей измерения
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

    // 2. Специальная обработка градусов и угловых штрихов до общей замены символов:
    // 2^\circ, 0.15^\circ, 0.2^{\circ}, \degree -> ° (\u00B0)
    static const QRegularExpression rxDegree(
        R"(\^\s*\{\s*\\circ\s*\}|\^\s*\\circ(?![A-Za-z])|\\degree(?![A-Za-z]))");
    s.replace(rxDegree, QStringLiteral("\u00B0"));

    static const QRegularExpression rxDoublePrime(
        R"(\^\s*\{\s*\\prime\s*\\prime\s*\})");
    s.replace(rxDoublePrime, QStringLiteral("\u2033"));

    static const QRegularExpression rxSinglePrime(
        R"(\^\s*\{\s*\\prime\s*\}|\^\s*\\prime(?![A-Za-z])|\\prime(?![A-Za-z]))");
    s.replace(rxSinglePrime, QStringLiteral("\u2032"));

    // 3. Удаляем размерные модификаторы скобок и обрабатываем пробельные макросы LaTeX (включая ~)
    static const QRegularExpression rxLeftRight(
        R"(\\left\b|\\right\b|\\big\b|\\Big\b|\\bigl\b|\\bigr\b|\\Bigl\b|\\Bigr\b|\\middle\b)");
    s.remove(rxLeftRight);
    s.replace("\\,", " ");
    s.replace("\\;", " ");
    s.replace("\\:", " ");
    s.replace("\\!", "");
    s.replace("\\quad", "  ");
    s.replace("\\qquad", "    ");
    s.replace("\\ ", " ");
    // Неэкранированная тильда ~ в LaTeX — неразрывный пробел (например, $12~\text{м}$)
    static const QRegularExpression rxUnescapedTilde(R"((?<!\\)~)");
    s.replace(rxUnescapedTilde, " ");

    // 4. Таблица символов LaTeX -> Unicode (записана через \uXXXX для защиты от проблем с кодировкой)
    struct SymMap { const char* cmd; const char16_t* uni; };
    static constexpr SymMap kSymbols[] = {
        {"\\Longleftrightarrow", u"\u27FA"}, {"\\iff", u"\u21D4"},
        {"\\Longrightarrow", u"\u27F9"},     {"\\implies", u"\u21D2"},
        {"\\Longleftarrow", u"\u27F8"},      {"\\longleftrightarrow", u"\u27F7"},
        {"\\longrightarrow", u"\u27F6"},     {"\\longleftarrow", u"\u27F5"},
        {"\\leftrightarrow", u"\u2194"},     {"\\Leftrightarrow", u"\u21D4"},
        {"\\rightarrow", u"\u2192"},         {"\\to", u"\u2192"},
        {"\\leftarrow", u"\u2190"},          {"\\gets", u"\u2190"},
        {"\\Rightarrow", u"\u21D2"},         {"\\Leftarrow", u"\u21D0"},
        {"\\uparrow", u"\u2191"},            {"\\downarrow", u"\u2193"},
        {"\\updownarrow", u"\u2195"},        {"\\mapsto", u"\u21A6"},
        {"\\hookleftarrow", u"\u21A9"},      {"\\hookrightarrow", u"\u21AA"},
        {"\\nearrow", u"\u2197"},            {"\\searrow", u"\u2198"},
        {"\\leqslant", u"\u2264"},           {"\\geqslant", u"\u2265"},
        {"\\leq", u"\u2264"},                {"\\le", u"\u2264"},
        {"\\geq", u"\u2265"},                {"\\ge", u"\u2265"},
        {"\\neq", u"\u2260"},                {"\\ne", u"\u2260"},
        {"\\approx", u"\u2248"},             {"\\equiv", u"\u2261"},
        {"\\cong", u"\u2245"},               {"\\sim", u"\u223C"},
        {"\\simeq", u"\u2243"},              {"\\propto", u"\u221D"},
        {"\\ll", u"\u226A"},                 {"\\gg", u"\u226B"},
        {"\\times", u"\u00D7"},              {"\\div", u"\u00F7"},
        {"\\cdot", u"\u00B7"},               {"\\ast", u"\u2217"},
        {"\\star", u"\u22C6"},               {"\\circ", u"\u2218"},
        {"\\bullet", u"\u2219"},             {"\\pm", u"\u00B1"},
        {"\\mp", u"\u2213"},                 {"\\oplus", u"\u2295"},
        {"\\ominus", u"\u2296"},             {"\\otimes", u"\u2297"},
        {"\\infty", u"\u221E"},              {"\\partial", u"\u2202"},
        {"\\nabla", u"\u2207"},              {"\\forall", u"\u2200"},
        {"\\exists", u"\u2203"},             {"\\nexists", u"\u2204"},
        {"\\emptyset", u"\u2205"},           {"\\varnothing", u"\u2205"},
        {"\\in", u"\u2208"},                 {"\\notin", u"\u2209"},
        {"\\ni", u"\u220B"},                 {"\\subseteq", u"\u2286"},
        {"\\supseteq", u"\u2287"},           {"\\subset", u"\u2282"},
        {"\\supset", u"\u2283"},             {"\\cup", u"\u222A"},
        {"\\cap", u"\u2229"},                {"\\setminus", u"\u2216"},
        {"\\land", u"\u2227"},               {"\\lor", u"\u2228"},
        {"\\lnot", u"\u00AC"},               {"\\neg", u"\u00AC"},
        {"\\top", u"\u22A4"},                {"\\bot", u"\u22A5"},
        {"\\perp", u"\u22A5"},               {"\\parallel", u"\u2225"},
        {"\\angle", u"\u2220"},              {"\\measuredangle", u"\u2221"},
        {"\\triangle", u"\u25B3"},           {"\\sum", u"\u2211"},
        {"\\prod", u"\u220F"},               {"\\int", u"\u222B"},
        {"\\oint", u"\u222E"},               {"\\ldots", u"\u2026"},
        {"\\cdots", u"\u22EF"},              {"\\dots", u"\u2026"},
        {"\\vdots", u"\u22EE"},              {"\\ddots", u"\u22F1"},
        {"\\checkmark", u"\u2713"},
        // Греческий алфавит (строчные и прописные)
        {"\\alpha", u"\u03B1"},   {"\\beta", u"\u03B2"},    {"\\gamma", u"\u03B3"},   {"\\delta", u"\u03B4"},
        {"\\varepsilon", u"\u03B5"}, {"\\epsilon", u"\u03B5"}, {"\\zeta", u"\u03B6"}, {"\\eta", u"\u03B7"},
        {"\\vartheta", u"\u03D1"}, {"\\theta", u"\u03B8"},  {"\\iota", u"\u03B9"},    {"\\kappa", u"\u03BA"},
        {"\\lambda", u"\u03BB"},  {"\\mu", u"\u03BC"},      {"\\nu", u"\u03BD"},      {"\\xi", u"\u03BE"},
        {"\\pi", u"\u03C0"},      {"\\varpi", u"\u03D6"},   {"\\varrho", u"\u03F1"},  {"\\rho", u"\u03C1"},
        {"\\varsigma", u"\u03C2"}, {"\\sigma", u"\u03C3"},  {"\\tau", u"\u03C4"},     {"\\upsilon", u"\u03C5"},
        {"\\varphi", u"\u03C6"},  {"\\phi", u"\u03D5"},     {"\\chi", u"\u03C7"},     {"\\psi", u"\u03C8"},
        {"\\omega", u"\u03C9"},
        {"\\Gamma", u"\u0393"},   {"\\Delta", u"\u0394"},   {"\\Theta", u"\u0398"},   {"\\Lambda", u"\u039B"},
        {"\\Xi", u"\u039E"},      {"\\Pi", u"\u03A0"},      {"\\Sigma", u"\u03A3"},   {"\\Upsilon", u"\u03A5"},
        {"\\Phi", u"\u03A6"},     {"\\Psi", u"\u03A8"},     {"\\Omega", u"\u03A9"},
        // Стандартные математические функции
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
        QRegularExpression rx(QString::fromLatin1(m.cmd) + R"((?![A-Za-z]))");
        s.replace(rx, QString::fromUtf16(m.uni));
    }

    s = escapeHtml(s);

    // 5. Шрифтовые обёртки внутри формул (\mathbf, \mathit, \mathtt, \mathcal)
    static const QRegularExpression rxBoldMath(R"(\\(?:mathbf|boldsymbol|textbf)\s*\{([^{}]*)\})");
    s.replace(rxBoldMath, "<b>\\1</b>");
    static const QRegularExpression rxItalicMath(R"(\\(?:mathit|textit)\s*\{([^{}]*)\})");
    s.replace(rxItalicMath, "<i>\\1</i>");
    static const QRegularExpression rxMonoMath(R"(\\(?:mathtt|texttt)\s*\{([^{}]*)\})");
    s.replace(rxMonoMath, "<code style='font-family:monospace; color:#8be9fd;'>\\1</code>");
    static const QRegularExpression rxCalMath(R"(\\(?:mathcal|mathbb)\s*\{([^{}]*)\})");
    s.replace(rxCalMath, "<b>\\1</b>");

    // 6. Многопроходное разрешение степеней/индексов (^{...}, ^x, _{...}, _x), дробей \frac и корней \sqrt
    static const QRegularExpression rxSupBrace(R"(\^\s*\{([^{}]+)\})");
    static const QRegularExpression rxSupChar(R"(\^([^\s<>\{\}\^_]))");
    static const QRegularExpression rxSubBrace(R"(_\s*\{([^{}]+)\})");
    static const QRegularExpression rxSubChar(R"(_([^\s<>\{\}\^_]))");
    static const QRegularExpression rxFrac(R"(\\frac\s*\{([^{}]*)\}\s*\{([^{}]*)\})");
    static const QRegularExpression rxSqrtBrace(R"(\\sqrt\s*\{([^{}]*)\})");

    for (int pass = 0; pass < 4; ++pass) {
        QString before = s;
        s.replace(rxSupBrace, "<sup>\\1</sup>");
        s.replace(rxSupChar, "<sup>\\1</sup>");
        s.replace(rxSubBrace, "<sub>\\1</sub>");
        s.replace(rxSubChar, "<sub>\\1</sub>");
        s.replace(rxFrac, "<span style='white-space:nowrap;'>(\\1)/(\\2)</span>");
        s.replace(rxSqrtBrace, QStringLiteral("\u221A(\\1)"));
        if (s == before) break;
    }
    s.replace("\\sqrt", QStringLiteral("\u221A"));

    // Очистка экранированных фигурных скобок \{ и \}
    s.replace("\\{", "{");
    s.replace("\\}", "}");

    // 7. Возвращаем защищённые блоки \text{...} на место
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

    // 2. Вырезаем математические выражения $$...$$, \[...\], \(...\) и $...$ в плейсхолдеры
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

    // Одиночные $...$: поддерживаем стандартные формулы, а также формулы с LaTeX-командами,
    // не затрагивая диапазоны цен вида "$100 - $200" или "$100-$200"
    static const QRegularExpression rxInlineMath(R"((?<!\\)\$([^\$\n]+?)(?<!\\)\$(?!\d))");
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

        // Если внутри есть пробелы по краям и нет никаких признаков LaTeX — это обычные знаки доллара в тексте
        if (trimmedInner.isEmpty() || (hasLeadingOrTrailingSpace && !hasLatexMarkers)) {
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
