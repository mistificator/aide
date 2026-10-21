// src/syntax/ColorScheme.cpp
#include "ColorScheme.h"
#include "../core/ConfigManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>

static QTextCharFormat makeFmt(const QString& hexColor, bool bold = false, bool italic = false) {
    QTextCharFormat fmt;
    fmt.setForeground(QColor(hexColor));
    if (bold) fmt.setFontWeight(QFont::Bold);
    if (italic) fmt.setFontItalic(true);
    return fmt;
}

ColorScheme ColorScheme::defaultDracula() {
    ColorScheme s;
    s.formats[TokenRole::Default]         = makeFmt("#f8f8f2");
    s.formats[TokenRole::Keyword]         = makeFmt("#ff79c6", true);
    s.formats[TokenRole::DataType]        = makeFmt("#8be9fd", false, true);
    s.formats[TokenRole::BuiltinFunction] = makeFmt("#50fa7b");
    s.formats[TokenRole::String]          = makeFmt("#f1fa8c");
    s.formats[TokenRole::Comment]         = makeFmt("#6272a4", false, true);
    s.formats[TokenRole::Number]          = makeFmt("#bd93f9");
    s.formats[TokenRole::Preprocessor]    = makeFmt("#ffb86c");
    s.formats[TokenRole::Operator]        = makeFmt("#ff79c6");
    s.formats[TokenRole::Error]           = makeFmt("#ff5555", true);
    return s;
}

ColorScheme ColorScheme::loadFromFile(const QString& filePath) {
    ColorScheme s = defaultDracula();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return s;

    auto root = QJsonDocument::fromJson(f.readAll()).object();
    s.name = root["name"].toString("custom");
    auto ed = root["editor"].toObject();
    if (ed.contains("background"))   s.editorBg = QColor(ed["background"].toString());
    if (ed.contains("foreground"))   s.editorFg = QColor(ed["foreground"].toString());
    if (ed.contains("currentLine"))  s.currentLineBg = QColor(ed["currentLine"].toString());
    if (ed.contains("selection"))    s.selectionBg = QColor(ed["selection"].toString());
    if (ed.contains("lineNumberBg")) s.lineNumberBg = QColor(ed["lineNumberBg"].toString());
    if (ed.contains("lineNumberFg")) s.lineNumberFg = QColor(ed["lineNumberFg"].toString());

    auto tok = root["tokens"].toObject();
    auto parseRole = [&](TokenRole role, const QString& key) {
        if (!tok.contains(key)) return;
        auto o = tok[key].toObject();
        s.formats[role] = makeFmt(o["color"].toString("#f8f8f2"), o["bold"].toBool(false), o["italic"].toBool(false));
    };
    parseRole(TokenRole::Keyword, "keyword");
    parseRole(TokenRole::DataType, "datatype");
    parseRole(TokenRole::BuiltinFunction, "function");
    parseRole(TokenRole::String, "string");
    parseRole(TokenRole::Comment, "comment");
    parseRole(TokenRole::Number, "number");
    parseRole(TokenRole::Preprocessor, "preprocessor");
    parseRole(TokenRole::Operator, "operator");
    parseRole(TokenRole::Error, "error");
    return s;
}

ColorScheme ColorScheme::loadByName(const QString& themeName) {
    QString path = ConfigManager::instance().themesDirPath() + "/" + themeName + ".json";
    return QFile::exists(path) ? loadFromFile(path) : defaultDracula();
}