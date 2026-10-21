// src/syntax/LexicalHighlighter.h
#pragma once
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QVector>
#include "ColorScheme.h"

class LexicalHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LexicalHighlighter(QTextDocument* parent = nullptr);

    void setLanguageByFilePath(const QString& filePath);
    void setLanguage(const QString& langId); // "cpp", "c", "python", "bash", "sql", "javascript"
    QString language() const { return m_langId; }

    void setColorScheme(const ColorScheme& scheme);
    ColorScheme colorScheme() const { return m_scheme; }

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        TokenRole role;
    };

    void rebuildRules();
    bool loadRulesFromSyntaxFile(const QString& langId, QStringList& keywords, QStringList& types, QStringList& builtins);
    void populateFallbackLists(const QString& langId, QStringList& keywords, QStringList& types, QStringList& builtins);

    QString m_langId = "cpp";
    ColorScheme m_scheme;
    QVector<HighlightRule> m_rules;
    QRegularExpression m_multiLineCommentStart, m_multiLineCommentEnd;
    bool m_hasMultiLineComments = true;
};