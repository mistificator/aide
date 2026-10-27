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

        // 1b. Блочные математические формулы $$ ... $$
        if (trimmed.startsWith("$$")) {
            closeLists();
            closeTable();
            if (!inMathBlock) {
                if (trimmed.length() > 4 && trimmed.endsWith("$$")) {
                    QString innerMath = trimmed.mid(2, trimmed.length() - 4).trimmed();
                    html += renderLatexMath(innerMath, true);
                } else {
                    inMathBlock = true;
                    mathLines.clear();
                    QString firstPart = trimmed.mid(2).trimmed();
                    if (!firstPart.isEmpty()) mathLines.append(firstPart);
                }
            } else {
                QString lastPart = trimmed;
                if (lastPart.endsWith("$$")) lastPart.chop(2);
                if (!lastPart.trimmed().isEmpty()) mathLines.append(lastPart.trimmed());
                html += renderLatexMath(mathLines.join(' '), true);
                inMathBlock = false;
                mathLines.clear();
            }
            continue;
        }

        if (inMathBlock) {
            if (trimmed.endsWith("$$")) {
                QString lastPart = trimmed.left(trimmed.length() - 2).trimmed();
                if (!lastPart.isEmpty()) mathLines.append(lastPart);
                html += renderLatexMath(mathLines.join(' '), true);
                inMathBlock = false;
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

    // Удаляем размерные модификаторы скобок и пробельные макросы LaTeX
    static const QRegularExpression rxLeftRight(R"(\\left\b|\\right\b|\\big\b|\\Big\b|\\bigl\b|\\bigr\b)");
    s.remove(rxLeftRight);
    s.replace("\\,", " ");
    s.replace("\\;", " ");
    s.replace("\\:", " ");
    s.replace("\\!", "");
    s.replace("\\quad", "  ");
    s.replace("\\qquad", "    ");
    s.replace("\\ ", " ");

    // Замена таблицы символов LaTeX на Unicode
    struct SymMap { const char* cmd; const char* uni; };
    static constexpr SymMap kSymbols[] = {
        {"\\Longleftrightarrow", "⟺"}, {"\\iff", "⇔"},
        {"\\Longrightarrow", "⟹"},     {"\\implies", "⇒"},
        {"\\Longleftarrow", "⟸"},      {"\\longleftrightarrow", "⟷"},
        {"\\longrightarrow", "⟶"},     {"\\longleftarrow", "⟵"},
        {"\\leftrightarrow", "↔"},     {"\\Leftrightarrow", "⇔"},
        {"\\rightarrow", "→"},         {"\\to", "→"},
        {"\\leftarrow", "←"},          {"\\gets", "←"},
        {"\\Rightarrow", "⇒"},         {"\\Leftarrow", "⇐"},
        {"\\uparrow", "↑"},            {"\\downarrow", "↓"},
        {"\\updownarrow", "↕"},        {"\\mapsto", "↦"},
        {"\\hookleftarrow", "↩"},      {"\\hookrightarrow", "↪"},
        {"\\nearrow", "↗"},            {"\\searrow", "↘"},
        {"\\leqslant", "≤"},           {"\\geqslant", "≥"},
        {"\\leq", "≤"},                {"\\le", "≤"},
        {"\\geq", "≥"},                {"\\ge", "≥"},
        {"\\neq", "≠"},                {"\\ne", "≠"},
        {"\\approx", "≈"},             {"\\equiv", "≡"},
        {"\\cong", "≅"},               {"\\sim", "∼"},
        {"\\simeq", "≃"},              {"\\propto", "∝"},
        {"\\ll", "≪"},                 {"\\gg", "≫"},
        {"\\times", "×"},              {"\\div", "÷"},
        {"\\cdot", "·"},               {"\\ast", "∗"},
        {"\\star", "⋆"},               {"\\circ", "∘"},
        {"\\bullet", "∙"},             {"\\pm", "±"},
        {"\\mp", "∓"},                 {"\\oplus", "⊕"},
        {"\\ominus", "⊖"},             {"\\otimes", "⊗"},
        {"\\infty", "∞"},              {"\\partial", "∂"},
        {"\\nabla", "∇"},              {"\\forall", "∀"},
        {"\\exists", "∃"},             {"\\nexists", "∄"},
        {"\\emptyset", "∅"},           {"\\varnothing", "∅"},
        {"\\in", "∈"},                 {"\\notin", "∉"},
        {"\\ni", "∋"},                 {"\\subseteq", "⊆"},
        {"\\supseteq", "⊇"},           {"\\subset", "⊂"},
        {"\\supset", "⊃"},             {"\\cup", "∪"},
        {"\\cap", "∩"},                {"\\setminus", "∖"},
        {"\\land", "∧"},               {"\\lor", "∨"},
        {"\\lnot", "¬"},               {"\\neg", "¬"},
        {"\\top", "⊤"},                {"\\bot", "⊥"},
        {"\\perp", "⊥"},               {"\\parallel", "∥"},
        {"\\angle", "∠"},              {"\\triangle", "△"},
        {"\\sum", "∑"},                {"\\prod", "∏"},
        {"\\int", "∫"},                {"\\oint", "∮"},
        {"\\ldots", "…"},              {"\\cdots", "⋯"},
        {"\\dots", "…"},               {"\\vdots", "⋮"},
        {"\\ddots", "⋱"},              {"\\checkmark", "✓"},
        // Греческий алфавит
        {"\\alpha", "α"},   {"\\beta", "β"},    {"\\gamma", "γ"},   {"\\delta", "δ"},
        {"\\varepsilon", "ε"}, {"\\epsilon", "ε"}, {"\\zeta", "ζ"}, {"\\eta", "η"},
        {"\\vartheta", "ϑ"}, {"\\theta", "θ"},  {"\\iota", "ι"},    {"\\kappa", "κ"},
        {"\\lambda", "λ"},  {"\\mu", "μ"},      {"\\nu", "ν"},      {"\\xi", "ξ"},
        {"\\pi", "π"},      {"\\varpi", "ϖ"},   {"\\varrho", "ϱ"},  {"\\rho", "ρ"},
        {"\\varsigma", "ς"}, {"\\sigma", "σ"},  {"\\tau", "τ"},     {"\\upsilon", "υ"},
        {"\\varphi", "φ"},  {"\\phi", "φ"},     {"\\chi", "χ"},     {"\\psi", "psi"},
        {"\\omega", "ω"},
        {"\\Gamma", "Γ"},   {"\\Delta", "Δ"},   {"\\Theta", "Θ"},   {"\\Lambda", "Λ"},
        {"\\Xi", "Ξ"},      {"\\Pi", "Π"},      {"\\Sigma", "∑"},   {"\\Upsilon", "Υ"},
        {"\\Phi", "Φ"},     {"\\Psi", "Ψ"},     {"\\Omega", "Ω"},
        // Стандартные математические функции
        {"\\log", "log"},   {"\\ln", "ln"},     {"\\exp", "exp"},
        {"\\sin", "sin"},   {"\\cos", "cos"},   {"\\tan", "tan"},
        {"\\min", "min"},   {"\\max", "max"},   {"\\sup", "sup"},
        {"\\inf", "inf"},   {"\\lim", "lim"},   {"\\mod", "mod"},
        {"\\det", "det"},   {"\\dim", "dim"},   {"\\gcd", "gcd"}
    };

    for (const auto& m : kSymbols) {
        QRegularExpression rx(QString::fromLatin1(m.cmd) + R"((?![A-Za-z]))");
        s.replace(rx, QString::fromUtf8(m.uni));
    }

    s = escapeHtml(s);

    // Дроби \frac{a}{b} -> (a)/(b)
    static const QRegularExpression rxFrac(R"(\\frac\s*\{([^{}]*)\}\s*\{([^{}]*)\})");
    while (rxFrac.match(s).hasMatch()) {
        s.replace(rxFrac, "<span style='white-space:nowrap;'>(\\1)/(\\2)</span>");
    }

    // Корни \sqrt{x} -> √(x)
    static const QRegularExpression rxSqrtBrace(R"(\\sqrt\s*\{([^{}]*)\})");
    s.replace(rxSqrtBrace, "√(\\1)");
    s.replace("\\sqrt", "√");

    // Текстовые и шрифтовые обёртки внутри формул
    static const QRegularExpression rxText(R"(\\(?:text|mathrm|operatorname|mbox)\s*\{([^{}]*)\})");
    s.replace(rxText, "\\1");
    static const QRegularExpression rxBoldMath(R"(\\(?:mathbf|boldsymbol|textbf)\s*\{([^{}]*)\})");
    s.replace(rxBoldMath, "<b>\\1</b>");
    static const QRegularExpression rxItalicMath(R"(\\(?:mathit|textit)\s*\{([^{}]*)\})");
    s.replace(rxItalicMath, "<i>\\1</i>");
    static const QRegularExpression rxMonoMath(R"(\\(?:mathtt|texttt)\s*\{([^{}]*)\})");
    s.replace(rxMonoMath, "<code style='font-family:monospace; color:#8be9fd;'>\\1</code>");
    static const QRegularExpression rxCalMath(R"(\\(?:mathcal|mathbb)\s*\{([^{}]*)\})");
    s.replace(rxCalMath, "<b>\\1</b>");

    // Верхние и нижние индексы: ^{...}, ^x, _{...}, _x
    static const QRegularExpression rxSupBrace(R"(\^\{([^{}]+)\})");
    s.replace(rxSupBrace, "<sup>\\1</sup>");
    static const QRegularExpression rxSupChar(R"(\^([A-Za-z0-9\+\-\*]))");
    s.replace(rxSupChar, "<sup>\\1</sup>");

    static const QRegularExpression rxSubBrace(R"(_\{([^{}]+)\})");
    s.replace(rxSubBrace, "<sub>\\1</sub>");
    static const QRegularExpression rxSubChar(R"(_([A-Za-z0-9\+\-]))");
    s.replace(rxSubChar, "<sub>\\1</sub>");

    // Очистка оставшихся экранированных фигурных скобок \{ и \} и одиночных группирующих { }
    s.replace("\\{", "{");
    s.replace("\\}", "}");

    if (displayMode) {
        return QString("<div style='background-color:#232530; border-left:3px solid #f1fa8c; "
                       "padding:6px 12px; margin:6px 0; color:#f1fa8c; font-size:10.5pt;'>%1</div>")
            .arg(s);
    }

    // Если внутри $...$ была только одиночная стрелка или символ (например, $\rightarrow$),
    // возвращаем её напрямую без лишнего изменения шрифта
    QString plainCheck = s.trimmed();
    if (plainCheck.length() <= 3 && !plainCheck.contains('<')) {
        return plainCheck;
    }

    return QString("<span style='color:#f1fa8c;'>%1</span>").arg(s);
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

    // 2. Вырезаем математические выражения $$...$$ и $...$ в плейсхолдеры до экранирования HTML и курсива
    QStringList mathSpans;
    static const QRegularExpression rxInlineDisplayMath(R"(\$\$([^\$\n]+?)\$\$)");
    auto dmIt = rxInlineDisplayMath.globalMatch(work);
    while (dmIt.hasNext()) {
        auto m = dmIt.next();
        QString rendered = renderLatexMath(m.captured(1), false);
        int idx = mathSpans.size();
        mathSpans.append(rendered);
        work.replace(m.captured(0), QString("\x01MATH%1\x01").arg(idx));
        dmIt = rxInlineDisplayMath.globalMatch(work);
    }

    // Одиночные $...$: без пробела сразу после открывающего $ и перед закрывающим $,
    // чтобы не задевать обычные суммы в долларах вроде "$10 and $20"
    static const QRegularExpression rxInlineMath(R"((?<!\\)\$(?!\s)([^\$\n]+?)(?<!\s)\$)");
    int searchPos = 0;
    while (true) {
        auto m = rxInlineMath.match(work, searchPos);
        if (!m.hasMatch()) break;
        QString inner = m.captured(1);
        // Если выражение начинается с цифры и не содержит ни команд LaTeX (\), ни индексов (^, _), ни операторов,
        // пропускаем его (защита от "$100 - $200")
        bool looksLikeCurrencyRange = inner[0].isDigit() &&
                                      !inner.contains('\\') && !inner.contains('^') &&
                                      !inner.contains('_') && !inner.contains('=');
        if (looksLikeCurrencyRange) {
            searchPos = m.capturedEnd();
            continue;
        }
        QString rendered = renderLatexMath(inner, false);
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
