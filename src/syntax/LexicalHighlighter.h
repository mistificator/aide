// src/syntax/LexicalHighlighter.h
#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QVector>
#include "ColorScheme.h"

struct KdeSyntaxMetadata {
    QString name;
    QString section;
    QStringList extensions;
    bool valid = false;
};

class LexicalHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LexicalHighlighter(QTextDocument* parent = nullptr);

    void setLanguageByFilePath(const QString& filePath);
    void setLanguage(const QString& langId);
    QString language() const { return m_langId; }

    void setColorScheme(const ColorScheme& scheme);
    ColorScheme colorScheme() const { return m_scheme; }

    static KdeSyntaxMetadata inspectSyntaxXml(const QString& xmlFilePath);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        TokenRole role;
    };

    void rebuildRules();
    bool loadRulesFromSyntaxFile(const QString& syntaxFilePath,
                                 QStringList& keywords,
                                 QStringList& types,
                                 QStringList& builtins,
                                 QString& singleLineComment,
                                 QString& multiLineStart,
                                 QString& multiLineEnd);

    QString m_langId = "cpp";
    ColorScheme m_scheme;
    QVector<HighlightRule> m_rules;
    QRegularExpression m_multiLineCommentStart, m_multiLineCommentEnd;
    bool m_hasMultiLineComments = true;
};
