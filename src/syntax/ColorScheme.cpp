// src/syntax/ColorScheme.cpp
#include "ColorScheme.h"
#include "../core/ConfigManager.h"
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QStyleOption>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QWidget>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>
#include <QOperatingSystemVersion>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

class WindowDarkTitleBarFilter : public QObject {
public:
    explicit WindowDarkTitleBarFilter(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (m_inFilter) {
            return QObject::eventFilter(obj, event);
        }
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
            if (auto* w = qobject_cast<QWidget*>(obj);
                w && w->isWindow() && w->testAttribute(Qt::WA_WState_Created) && w->internalWinId() != 0) {
                m_inFilter = true;
                bool dark = ColorScheme::isDarkTheme(ConfigManager::instance().activeTheme());
                ColorScheme::applyWindowDarkTitleBar(w, dark);
                m_inFilter = false;
            }
        }
        return QObject::eventFilter(obj, event);
    }
private:
    bool m_inFilter = false;
};

// Прокси-стиль для Windows 10/8/7 и систем без плагина windows11:
// гарантирует чёткую векторную отрисовку стрелок в QComboBox и QSpinBox/QDoubleSpinBox
class LegacyFusionProxyStyle : public QProxyStyle {
public:
    explicit LegacyFusionProxyStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget = nullptr) const override {
        switch (element) {
        case PE_IndicatorArrowDown:
        case PE_IndicatorArrowUp:
        case PE_IndicatorArrowLeft:
        case PE_IndicatorArrowRight:
        case PE_IndicatorSpinUp:
        case PE_IndicatorSpinDown: {
            if (!option || !painter) break;
            QRectF r = option->rect;
            if (r.width() <= 2 || r.height() <= 2) return;

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);

            const bool enabled = (option->state & State_Enabled);
            QColor col = option->palette.color(enabled ? QPalette::Normal : QPalette::Disabled,
                                               QPalette::ButtonText);
            if (!enabled) {
                col.setAlphaF(0.55f);
            }

            const bool isSpin = (element == PE_IndicatorSpinUp || element == PE_IndicatorSpinDown);
            const double dim = qMin(r.width(), r.height());
            const double halfW = isSpin ? qBound(2.5, dim * 0.30, 4.5)
                                        : qBound(3.0, dim * 0.26, 5.0);
            const double halfH = halfW * 0.58;
            const QPointF c = r.center();

            QPainterPath path;
            if (element == PE_IndicatorArrowDown || element == PE_IndicatorSpinDown) {
                const double yOffset = isSpin ? -0.5 : 0.0;
                path.moveTo(c.x() - halfW, c.y() - halfH + yOffset);
                path.lineTo(c.x(),         c.y() + halfH + yOffset);
                path.lineTo(c.x() + halfW, c.y() - halfH + yOffset);
            } else if (element == PE_IndicatorArrowUp || element == PE_IndicatorSpinUp) {
                const double yOffset = isSpin ? 0.5 : 0.0;
                path.moveTo(c.x() - halfW, c.y() + halfH + yOffset);
                path.lineTo(c.x(),         c.y() - halfH + yOffset);
                path.lineTo(c.x() + halfW, c.y() + halfH + yOffset);
            } else if (element == PE_IndicatorArrowLeft) {
                path.moveTo(c.x() + halfH, c.y() - halfW);
                path.lineTo(c.x() - halfH, c.y());
                path.lineTo(c.x() + halfH, c.y() + halfW);
            } else if (element == PE_IndicatorArrowRight) {
                path.moveTo(c.x() - halfH, c.y() - halfW);
                path.lineTo(c.x() + halfH, c.y());
                path.lineTo(c.x() - halfH, c.y() + halfW);
            }

            QPen pen(col, isSpin ? 1.5 : 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
            painter->restore();
            return;
        }
        default:
            break;
        }

        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

bool isWindows11WithNativeStyle() {
#ifdef Q_OS_WIN
    const auto ver = QOperatingSystemVersion::current();
    if (ver >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000)) {
        return QStyleFactory::keys().contains(QStringLiteral("windows11"), Qt::CaseInsensitive);
    }
#endif
    return false;
}

QPalette createDarkPalette() {
    QPalette pal;
    const QColor windowBg("#21222c");
    const QColor baseBg("#191a22");
    const QColor altBaseBg("#252734");
    const QColor buttonBg("#2c2e3e");
    const QColor textFg("#f8f8f2");
    const QColor disabledFg("#6272a4");
    const QColor highlightBg("#44475a");

    pal.setColor(QPalette::Window, windowBg);
    pal.setColor(QPalette::WindowText, textFg);
    pal.setColor(QPalette::Base, baseBg);
    pal.setColor(QPalette::AlternateBase, altBaseBg);
    pal.setColor(QPalette::ToolTipBase, QColor("#282a36"));
    pal.setColor(QPalette::ToolTipText, textFg);
    pal.setColor(QPalette::Text, textFg);
    pal.setColor(QPalette::Button, buttonBg);
    pal.setColor(QPalette::ButtonText, textFg);
    pal.setColor(QPalette::BrightText, QColor("#ff5555"));
    pal.setColor(QPalette::Link, QColor("#8be9fd"));
    pal.setColor(QPalette::Highlight, highlightBg);
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::PlaceholderText, disabledFg);

    pal.setColor(QPalette::Light, QColor("#3a3d52"));
    pal.setColor(QPalette::Midlight, QColor("#323548"));
    pal.setColor(QPalette::Mid, QColor("#44475a"));
    pal.setColor(QPalette::Dark, QColor("#14151c"));
    pal.setColor(QPalette::Shadow, QColor("#0d0e13"));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledFg);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledFg);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledFg);
    pal.setColor(QPalette::Disabled, QPalette::Button, QColor("#232532"));
    pal.setColor(QPalette::Disabled, QPalette::Base, QColor("#1c1d26"));
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#313342"));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledFg);

    return pal;
}

QPalette createLightPalette() {
    QPalette pal;
    const QColor windowBg("#f3f4f6");
    const QColor baseBg("#ffffff");
    const QColor altBaseBg("#f6f8fa");
    const QColor buttonBg("#e5e7eb");
    const QColor textFg("#1f2328");
    const QColor disabledFg("#8c959f");
    const QColor highlightBg("#0969da");

    pal.setColor(QPalette::Window, windowBg);
    pal.setColor(QPalette::WindowText, textFg);
    pal.setColor(QPalette::Base, baseBg);
    pal.setColor(QPalette::AlternateBase, altBaseBg);
    pal.setColor(QPalette::ToolTipBase, baseBg);
    pal.setColor(QPalette::ToolTipText, textFg);
    pal.setColor(QPalette::Text, textFg);
    pal.setColor(QPalette::Button, buttonBg);
    pal.setColor(QPalette::ButtonText, textFg);
    pal.setColor(QPalette::BrightText, QColor("#cf222e"));
    pal.setColor(QPalette::Link, highlightBg);
    pal.setColor(QPalette::Highlight, highlightBg);
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::PlaceholderText, disabledFg);

    pal.setColor(QPalette::Light, QColor("#ffffff"));
    pal.setColor(QPalette::Midlight, QColor("#eaeef2"));
    pal.setColor(QPalette::Mid, QColor("#d0d7de"));
    pal.setColor(QPalette::Dark, QColor("#afb8c1"));
    pal.setColor(QPalette::Shadow, QColor("#6e7781"));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledFg);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledFg);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledFg);
    pal.setColor(QPalette::Disabled, QPalette::Button, QColor("#eaeef2"));
    pal.setColor(QPalette::Disabled, QPalette::Base, windowBg);

    return pal;
}

} // namespace

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

bool ColorScheme::isDarkTheme(const QString& themeName) {
    ColorScheme s = loadByName(themeName);
    if (themeName.contains("light", Qt::CaseInsensitive)) return false;
    return s.editorBg.lightness() < 128;
}

void ColorScheme::applyWindowDarkTitleBar(QWidget* window, bool dark) {
    if (!window || !window->isWindow()) return;

    // Пропускаем выпадающие списки (QComboBox), тултипы и окна без нативного HWND
    const Qt::WindowType wt = window->windowType();
    if (wt == Qt::Popup || wt == Qt::ToolTip || wt == Qt::SplashScreen) return;
    if (!window->testAttribute(Qt::WA_WState_Created)) return;

#ifdef Q_OS_WIN
    // ВАЖНО: используем internalWinId(), чтобы не вызывать QWidgetPrivate::create()
    // и не провоцировать рекурсивную отправку QEvent::WinIdChange
    WId wid = window->internalWinId();
    if (!wid) return;

    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd || !IsWindow(hwnd)) return;

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetWindowAttributeFn pDwmSetWindowAttribute = []() -> DwmSetWindowAttributeFn {
        HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
        if (!hDwm) return nullptr;
        return reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
    }();

    if (pDwmSetWindowAttribute) {
        BOOL useDark = dark ? TRUE : FALSE;
        // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+ / Windows 11)
        // 19 = атрибут для ранних сборок Windows 10 (1809..1909)
        if (FAILED(pDwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark)))) {
            pDwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
        }
    }
#else
    Q_UNUSED(dark);
#endif
}

void ColorScheme::installWindowThemeFilter(QApplication* app) {
    if (!app) return;
    static bool installed = false;
    if (installed) return;
    installed = true;
    app->installEventFilter(new WindowDarkTitleBarFilter(app));
}

void ColorScheme::applyApplicationTheme(const QString& themeName) {
    if (!qApp) return;

    const bool dark = isDarkTheme(themeName);

    // Сбрасываем глобальный StyleSheet, чтобы QStyleSheetStyle не ломал субконтролы QComboBox и QSpinBox
    qApp->setStyleSheet(QString());

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints()) {
        hints->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    }
#endif

    if (isWindows11WithNativeStyle()) {
        // В Windows 11 используем штатный плагин стиля windows11 для любой темы
        if (auto* win11Style = QStyleFactory::create(QStringLiteral("windows11"))) {
            QApplication::setStyle(win11Style);
        }
        QPalette pal = dark ? createDarkPalette() : createLightPalette();
        qApp->setPalette(pal);
    } else {
        // В Windows 10/8/7 (и на других ОС) используем Fusion + прокси-стиль с чёткими векторными стрелками
        QStyle* baseFusion = QStyleFactory::create(QStringLiteral("Fusion"));
        if (baseFusion) {
            QApplication::setStyle(new LegacyFusionProxyStyle(baseFusion));
        }
        QPalette pal = dark ? createDarkPalette() : createLightPalette();
        qApp->setPalette(pal);
    }

    const auto topWidgets = QApplication::topLevelWidgets();
    for (QWidget* w : topWidgets) {
        applyWindowDarkTitleBar(w, dark);
        w->update();
    }
}
