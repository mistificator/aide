// src/syntax/LexicalHighlighter.cpp
#include "LexicalHighlighter.h"
#include "../core/ConfigManager.h"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QDir>

LexicalHighlighter::LexicalHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent),
    m_scheme(ColorScheme::loadByName(ConfigManager::instance().activeTheme())) {
    rebuildRules();
}

KdeSyntaxMetadata LexicalHighlighter::inspectSyntaxXml(const QString& xmlFilePath) {
    KdeSyntaxMetadata meta;
    QFile f(xmlFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return meta;

    QXmlStreamReader xml(&f);
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("language")) {
            meta.name = xml.attributes().value("name").toString().trimmed();
            meta.section = xml.attributes().value("section").toString().trimmed();
            QString extStr = xml.attributes().value("extensions").toString();
            const auto parts = extStr.split(';', Qt::SkipEmptyParts);
            for (QString p : parts) {
                p = p.trimmed();
                if (p.startsWith("*.")) p = p.mid(2);
                else if (p.startsWith('*')) p = p.mid(1);
                if (!p.isEmpty() && !meta.extensions.contains(p.toLower()))
                    meta.extensions.append(p.toLower());
            }
            meta.valid = !meta.name.isEmpty();
            break;
        }
    }
    return meta;
}

void LexicalHighlighter::setLanguageByFilePath(const QString& filePath) {
    setLanguage(ConfigManager::instance().languageIdForFile(filePath));
}

void LexicalHighlighter::setLanguage(const QString& langId) {
    QString clean = langId.toLower().trimmed();
    if (clean.isEmpty()) clean = "cpp";
    m_langId = clean;
    rebuildRules();
    rehighlight();
}

void LexicalHighlighter::setColorScheme(const ColorScheme& scheme) {
    m_scheme = scheme;
    rehighlight();
}

bool LexicalHighlighter::loadRulesFromSyntaxFile(
    const QString& syntaxFilePath,
    QStringList& keywords, QStringList& types, QStringList& builtins,
    QString& singleLineComment, QString& multiLineStart, QString& multiLineEnd)
{
    QFile f(syntaxFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QXmlStreamReader xml(&f);
    QString currentListName;
    while (!xml.atEnd() && !xml.hasError()) {
        auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("list")) {
                currentListName = xml.attributes().value("name").toString().toLower();
            } else if (xml.name() == QLatin1String("item") && !currentListName.isEmpty()) {
                QString word = xml.readElementText().trimmed();
                if (word.isEmpty()) continue;
                if (currentListName.contains("type") || currentListName.contains("data"))
                    types.append(QRegularExpression::escape(word));
                else if (currentListName.contains("builtin") || currentListName.contains("func") || currentListName.contains("std"))
                    builtins.append(QRegularExpression::escape(word));
                else
                    keywords.append(QRegularExpression::escape(word));
            } else if (xml.name() == QLatin1String("comment")) {
                QString cname = xml.attributes().value("name").toString().toLower();
                if (cname == "singleline") {
                    singleLineComment = xml.attributes().value("start").toString();
                } else if (cname == "multiline") {
                    multiLineStart = xml.attributes().value("start").toString();
                    multiLineEnd = xml.attributes().value("end").toString();
                }
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("list")) {
            currentListName.clear();
        }
    }
    return !keywords.isEmpty() || !types.isEmpty() || !singleLineComment.isEmpty();
}

void LexicalHighlighter::rebuildRules() {
    m_rules.clear();
    QStringList keywords, types, builtins;
    QString singleComment = "//";
    QString multiStart = "/*", multiEnd = "*/";

    auto ruleCfg = ConfigManager::instance().ruleForLanguage(m_langId);
    QString syntaxPath = ConfigManager::instance().syntaxDirPath() + "/" + ruleCfg.syntaxFile;
    if (!QFile::exists(syntaxPath)) {
        syntaxPath = ConfigManager::instance().syntaxDirPath() + "/" + m_langId + ".xml";
    }

    bool loadedFromDisk = loadRulesFromSyntaxFile(
        syntaxPath, keywords, types, builtins, singleComment, multiStart, multiEnd);

    if (!loadedFromDisk && keywords.isEmpty()) {
        // Минимальный универсальный набор на случай работы без интернета до первой загрузки XML
        keywords = {"if", "else", "for", "while", "do", "switch", "case", "break", "continue",
                    "return", "class", "struct", "public", "private", "protected", "virtual",
                    "override", "const", "constexpr", "static", "namespace", "using", "template",
                    "typename", "try", "catch", "throw", "new", "delete", "def", "import", "from",
                    "as", "pass", "lambda", "yield", "async", "await", "function", "let", "var",
                    "select", "where", "insert", "update", "join", "group", "order", "by", "fi", "done"};
        types = {"void", "bool", "char", "short", "int", "long", "float", "double", "unsigned",
                 "signed", "size_t", "auto", "string", "vector", "map", "set", "QString", "QList",
                 "QVector", "QMap", "QObject", "QWidget", "str", "list", "dict", "tuple", "int64"};
        builtins = {"std", "printf", "cout", "endl", "print", "len", "range", "console", "connect", "emit"};
        if (m_langId == "python" || m_langId == "bash") {
            singleComment = "#";
            multiStart.clear();
            multiEnd.clear();
        } else if (m_langId == "sql") {
            singleComment = "--";
        }
    }

    keywords.removeDuplicates();
    types.removeDuplicates();
    builtins.removeDuplicates();

    auto addWordRules = [this](const QStringList& words, TokenRole role) {
        // Группируем по 80 слов, чтобы не превышать лимиты регулярного выражения на огромных XML
        for (int i = 0; i < words.size(); i += 80) {
            QStringList slice = words.mid(i, 80);
            QRegularExpression rx(QString("\\b(?:%1)\\b").arg(slice.join('|')));
            if (rx.isValid()) m_rules.append({rx, role});
        }
    };

    addWordRules(keywords, TokenRole::Keyword);
    addWordRules(types, TokenRole::DataType);
    addWordRules(builtins, TokenRole::BuiltinFunction);

    // Вызовы функций: identifier(...)
    m_rules.append({QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"), TokenRole::BuiltinFunction});
    // Числа
    m_rules.append({QRegularExpression("\\b(?:0x[0-9A-Fa-f]+|\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?[fFlLuU]*)\\b"), TokenRole::Number});
    // Директивы препроцессора / декораторы
    m_rules.append({QRegularExpression("^\\s*(?:#\\s*[a-zA-Z_]+|@[a-zA-Z_][a-zA-Z0-9_.]*)"), TokenRole::Preprocessor});
    // Строки в двойных и одинарных кавычках
    m_rules.append({QRegularExpression("\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*'"), TokenRole::String});

    // Однострочные комментарии
    if (!singleComment.isEmpty()) {
        m_rules.append({QRegularExpression(QRegularExpression::escape(singleComment) + "[^\n]*"), TokenRole::Comment});
    }

    // Многострочные комментарии
    if (!multiStart.isEmpty() && !multiEnd.isEmpty()) {
        m_hasMultiLineComments = true;
        m_multiLineCommentStart = QRegularExpression(QRegularExpression::escape(multiStart));
        m_multiLineCommentEnd = QRegularExpression(QRegularExpression::escape(multiEnd));
    } else {
        m_hasMultiLineComments = false;
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
    int startIndex = 0;
    if (previousBlockState() != 1) {
        auto m = m_multiLineCommentStart.match(text);
        startIndex = m.hasMatch() ? m.capturedStart() : -1;
    }

    while (startIndex >= 0) {
        auto endMatch = m_multiLineCommentEnd.match(text, startIndex + (previousBlockState() == 1 && startIndex == 0 ? 0 : m_multiLineCommentStart.pattern().length()));
        int endIndex = endMatch.hasMatch() ? endMatch.capturedStart() : -1;
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }
        setFormat(startIndex, commentLength, m_scheme.formats.value(TokenRole::Comment));
        auto nextStart = m_multiLineCommentStart.match(text, startIndex + commentLength);
        startIndex = nextStart.hasMatch() ? nextStart.capturedStart() : -1;
    }
}
