// src/syntax/LexicalHighlighter.cpp
#include "LexicalHighlighter.h"
#include "../core/ConfigManager.h"
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QXmlStreamReader>
#include <ranges>

LexicalHighlighter::LexicalHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent), m_scheme(ColorScheme::loadByName(ConfigManager::instance().activeTheme())) {
    rebuildRules();
}

void LexicalHighlighter::setLanguageByFilePath(const QString& filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();
    QString fileName = QFileInfo(filePath).fileName().toLower();
    if (ext == "cpp" || ext == "cxx" || ext == "cc" || ext == "hpp" || ext == "hxx" || ext == "h") setLanguage("cpp");
    else if (ext == "c") setLanguage("c");
    else if (ext == "py" || ext == "pyw") setLanguage("python");
    else if (ext == "sh" || ext == "bash" || fileName == ".bashrc") setLanguage("bash");
    else if (ext == "sql") setLanguage("sql");
    else if (ext == "js" || ext == "mjs" || ext == "cjs" || ext == "json") setLanguage("javascript");
    else setLanguage("cpp");
}

void LexicalHighlighter::setLanguage(const QString& langId) {
    if (m_langId == langId && !m_rules.isEmpty()) return;
    m_langId = langId.toLower();
    rebuildRules();
    rehighlight();
}

void LexicalHighlighter::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    rehighlight();
}

bool LexicalHighlighter::loadRulesFromSyntaxFile(const QString& langId, QStringList& keywords, QStringList& types, QStringList& builtins) {
    QString baseDir = ConfigManager::instance().syntaxDirPath();
    QFile jsonFile(baseDir + "/" + langId + ".json");
    if (jsonFile.open(QIODevice::ReadOnly)) {
        auto obj = QJsonDocument::fromJson(jsonFile.readAll()).object();
        for (const auto& v : obj["keywords"].toArray()) keywords << v.toString();
        for (const auto& v : obj["types"].toArray())    types << v.toString();
        for (const auto& v : obj["builtins"].toArray()) builtins << v.toString();
        return !keywords.isEmpty();
    }

    QFile xmlFile(baseDir + "/" + langId + ".xml");
    if (xmlFile.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&xmlFile);
        QString currentList;
        while (!xml.atEnd() && !xml.hasError()) {
            auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                if (xml.name() == QStringLiteral("list"))
                    currentList = xml.attributes().value("name").toString().toLower();
                else if (xml.name() == QStringLiteral("item") && !currentList.isEmpty()) {
                    QString word = xml.readElementText().trimmed();
                    if (currentList.contains("type")) types << word;
                    else if (currentList.contains("builtin") || currentList.contains("func")) builtins << word;
                    else keywords << word;
                }
            } else if (token == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("list")) {
                currentList.clear();
            }
        }
        return !keywords.isEmpty();
    }
    return false;
}

void LexicalHighlighter::populateFallbackLists(const QString& langId, QStringList& keywords, QStringList& types, QStringList& builtins) {
    if (langId == "cpp" || langId == "c") {
        keywords = {"alignas", "alignof", "and", "asm", "Break", "case", "catch", "class", "co_await", "co_return",
                    "co_yield", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue",
                    "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum", "explicit", "export",
                    "extern", "false", "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace",
                    "new", "noexcept", "not", "nullptr", "operator", "or", "override", "private", "protected",
                    "public", "register", "reinterpret_cast", "requires", "return", "sizeof", "static", "static_assert",
                    "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try",
                    "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while", "signals", "slots", "emit"};
        types = {"auto", "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short",
                 "signed", "unsigned", "void", "wchar_t", "size_t", "int8_t", "int16_t", "int32_t", "int64_t",
                 "uint8_t", "uint16_t", "uint32_t", "uint64_t", "QString", "QObject", "QWidget", "QByteArray",
                 "QList", "QVector", "QMap", "QHash", "QVariant", "QJsonObject", "QTextCursor", "std", "string", "vector"};
    } else if (langId == "python") {
        keywords = {"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif",
                    "else", "except", "False", "finally", "for", "from", "global", "if", "import", "in", "is",
                    "lambda", "None", "nonlocal", "not", "or", "pass", "raise", "return", "True", "try", "while", "with", "yield", "self"};
        types = {"int", "float", "str", "bool", "list", "dict", "set", "tuple", "bytes", "object", "Callable", "Optional", "Any"};
        builtins = {"print", "len", "range", "enumerate", "zip", "map", "filter", "open", "super", "isinstance", "getattr", "setattr"};
    } else if (langId == "bash") {
        keywords = {"if", "then", "else", "elif", "fi", "case", "esac", "for", "select", "while", "until", "do",
                    "done", "in", "function", "time", "coproc", "return", "exit", "break", "continue", "local", "export", "readonly", "declare"};
        builtins = {"echo", "printf", "read", "cd", "pwd", "pushd", "popd", "dirs", "let", "eval", "set", "unset",
                    "shift", "source", "grep", "awk", "sed", "find", "xargs", "tar", "curl", "wget", "chmod", "chown"};
    } else if (langId == "sql") {
        keywords = {"SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE", "TABLE",
                    "DROP", "ALTER", "INDEX", "VIEW", "JOIN", "INNER", "LEFT", "RIGHT", "FULL", "OUTER", "ON", "AS",
                    "DISTINCT", "GROUP", "BY", "ORDER", "ASC", "DESC", "HAVING", "LIMIT", "OFFSET", "UNION", "ALL",
                    "EXISTS", "IN", "BETWEEN", "LIKE", "IS", "NULL", "NOT", "AND", "OR", "PRIMARY", "KEY", "FOREIGN",
                    "REFERENCES", "DEFAULT", "CONSTRAINT", "UNIQUE", "CHECK", "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION"};
        types = {"INT", "INTEGER", "BIGINT", "SMALLINT", "DECIMAL", "NUMERIC", "FLOAT", "REAL", "DOUBLE", "VARCHAR",
                 "CHAR", "TEXT", "DATE", "TIME", "TIMESTAMP", "BOOLEAN", "BLOB", "JSON", "UUID", "SERIAL"};
        builtins = {"COUNT", "SUM", "AVG", "MIN", "MAX", "COALESCE", "NULLIF", "CAST", "NOW", "UPPER", "LOWER", "SUBSTRING"};
    } else if (langId == "javascript") {
        keywords = {"async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default",
                    "delete", "do", "else", "export", "extends", "false", "finally", "for", "from", "function", "if",
                    "import", "in", "instanceof", "let", "new", "null", "of", "return", "static", "super", "switch",
                    "this", "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "with", "yield"};
        types = {"Array", "Boolean", "Date", "Error", "Function", "JSON", "Map", "Math", "Number", "Object", "Promise",
                 "Proxy", "RegExp", "Set", "String", "Symbol", "WeakMap", "WeakSet", "console", "window", "document"};
    }
}

void LexicalHighlighter::rebuildRules() {
    m_rules.clear();
    QStringList keywords, types, builtins;
    if (!loadRulesFromSyntaxFile(m_langId, keywords, types, builtins))
        populateFallbackLists(m_langId, keywords, types, builtins);

    auto opt = (m_langId == "sql") ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption;
    auto addWordList = [&](const QStringList& words, TokenRole role) {
        for (const auto& w : words | std::views::filter([](const QString& s) { return !s.isEmpty(); }))
            m_rules.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(w)), opt), role});
    };

    addWordList(keywords, TokenRole::Keyword);
    addWordList(types, TokenRole::DataType);
    addWordList(builtins, TokenRole::BuiltinFunction);

    // Вызовы функций, числа, директивы препроцессора, строки, однострочные комментарии
    m_rules.append({QRegularExpression(R"(\b[A-Za-z_][A-Za-z0-9_]*(?=\s*\())"), TokenRole::BuiltinFunction});
    m_rules.append({QRegularExpression(R"(\b(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?[uUlLfF]*)\b)"), TokenRole::Number});

    if (m_langId == "cpp" || m_langId == "c")
        m_rules.append({QRegularExpression(R"(^\s*#\s*[a-zA-Z_]+.*)"), TokenRole::Preprocessor});
    else if (m_langId == "bash")
        m_rules.append({QRegularExpression(R"(\$[A-Za-z_][A-Za-z0-9_]*|\$\{[^}]+\})"), TokenRole::Preprocessor});

    m_rules.append({QRegularExpression(R"("([^"\\]|\\.)*"|'([^'\\]|\\.)*'|`([^`\\]|\\.)*`)"), TokenRole::String});

    if (m_langId == "python" || m_langId == "bash")
        m_rules.append({QRegularExpression(R"(#[^\n]*)"), TokenRole::Comment});
    else if (m_langId == "sql")
        m_rules.append({QRegularExpression(R"(--[^\n]*)"), TokenRole::Comment});
    else
        m_rules.append({QRegularExpression(R"(//[^\n]*)"), TokenRole::Comment});

    m_hasMultiLineComments = (m_langId == "cpp" || m_langId == "c" || m_langId == "javascript" || m_langId == "sql");
    if (m_hasMultiLineComments) {
        m_multiLineCommentStart = QRegularExpression(R"(/\*)");
        m_multiLineCommentEnd   = QRegularExpression(R"(\*/)");
    }
}

void LexicalHighlighter::highlightBlock(const QString& text) {
    for (const auto& rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), m_scheme.formats.value(rule.role));
        }
    }

    if (!m_hasMultiLineComments) return;
    setCurrentBlockState(0);

    int startIndex = (previousBlockState() != 1) ? text.indexOf(m_multiLineCommentStart) : 0;
    while (startIndex >= 0) {
        auto endMatch = m_multiLineCommentEnd.match(text, startIndex);
        int endIndex = endMatch.capturedStart(), commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }
        setFormat(startIndex, commentLength, m_scheme.formats.value(TokenRole::Comment));
        startIndex = text.indexOf(m_multiLineCommentStart, startIndex + commentLength);
    }
}