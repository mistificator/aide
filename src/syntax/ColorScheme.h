// src/syntax/ColorScheme.h
#pragma once
#include <QColor>
#include <QTextCharFormat>
#include <QMap>
#include <QString>

enum class TokenRole {
    Default, Keyword, DataType, BuiltinFunction, String, Comment, Number, Preprocessor, Operator, Error
};

struct ColorScheme {
    QString name = "dracula";
    QColor editorBg = QColor("#282a36"), editorFg = QColor("#f8f8f2");
    QColor currentLineBg = QColor("#44475a"), selectionBg = QColor("#6272a4");
    QColor lineNumberBg = QColor("#21222c"), lineNumberFg = QColor("#6272a4");
    QMap<TokenRole, QTextCharFormat> formats;

    static ColorScheme loadFromFile(const QString& filePath);
    static ColorScheme loadByName(const QString& themeName);
    static ColorScheme defaultDracula();
};