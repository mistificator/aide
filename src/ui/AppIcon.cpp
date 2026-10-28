// src/ui/AppIcon.cpp
#include "AppIcon.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QMap>
#include <cmath>

QPixmap AppIcon::renderIconPixmap(int size) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double s = size;
    const double pad = s * 0.06;
    const double r = s * 0.22;
    QRectF bgRect(pad, pad, s - 2 * pad, s - 2 * pad);

    // 1. Фон (Dracula)
    QLinearGradient bgGrad(bgRect.topLeft(), bgRect.bottomRight());
    bgGrad.setColorAt(0.0, QColor("#282a36"));
    bgGrad.setColorAt(0.5, QColor("#191a24"));
    bgGrad.setColorAt(1.0, QColor("#11121a"));

    p.setPen(QPen(QColor("#bd93f9"), qMax(1.0, s * 0.035)));
    p.setBrush(bgGrad);
    p.drawRoundedRect(bgRect, r, r);

    // 2. Внутренний контур
    if (size >= 32) {
        QRectF innerRect = bgRect.adjusted(s * 0.04, s * 0.04, -s * 0.04, -s * 0.04);
        p.setPen(QPen(QColor(139, 233, 253, 55), qMax(1.0, s * 0.012)));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(innerRect, r * 0.82, r * 0.82);
    }

    // 3. Скобки '<' и '>' (#8be9fd)
    QPen bracketPen(QColor("#8be9fd"), qMax(1.4, s * 0.065), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(bracketPen);
    p.setBrush(Qt::NoBrush);

    QPainterPath leftBr;
    leftBr.moveTo(s * 0.27, s * 0.34);
    leftBr.lineTo(s * 0.16, s * 0.50);
    leftBr.lineTo(s * 0.27, s * 0.66);
    p.drawPath(leftBr);

    QPainterPath rightBr;
    rightBr.moveTo(s * 0.73, s * 0.34);
    rightBr.lineTo(s * 0.84, s * 0.50);
    rightBr.lineTo(s * 0.73, s * 0.66);
    p.drawPath(rightBr);

    // 4. Буква 'A' (#f8f8f2 -> #bd93f9)
    QLinearGradient aGrad(QPointF(s * 0.50, s * 0.20), QPointF(s * 0.50, s * 0.78));
    aGrad.setColorAt(0.0, QColor("#f8f8f2"));
    aGrad.setColorAt(1.0, QColor("#bd93f9"));

    QPen aPen(QBrush(aGrad), qMax(1.6, s * 0.082), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(aPen);

    QPainterPath letterA;
    letterA.moveTo(s * 0.34, s * 0.74);
    letterA.lineTo(s * 0.50, s * 0.24);
    letterA.lineTo(s * 0.66, s * 0.74);
    p.drawPath(letterA);

    // 5. Искра ИИ в центре 'A' (#50fa7b)
    QPointF center(s * 0.50, s * 0.55);
    double diamondR = s * 0.085;

    QRadialGradient glow(center, diamondR * 2.2);
    glow.setColorAt(0.0, QColor(80, 250, 123, 180));
    glow.setColorAt(1.0, QColor(80, 250, 123, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(center, diamondR * 2.2, diamondR * 2.2);

    p.setPen(QPen(QColor("#50fa7b"), qMax(1.2, s * 0.045), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(s * 0.39, s * 0.55), QPointF(s * 0.61, s * 0.55));

    QPolygonF diamond;
    diamond << QPointF(center.x(), center.y() - diamondR)
            << QPointF(center.x() + diamondR, center.y())
            << QPointF(center.x(), center.y() + diamondR)
            << QPointF(center.x() - diamondR, center.y());
    p.setPen(QPen(QColor("#191a24"), qMax(1.0, s * 0.02)));
    p.setBrush(QColor("#50fa7b"));
    p.drawPolygon(diamond);

    return px;
}

QIcon AppIcon::appIcon() {
    static QIcon cached;
    if (!cached.isNull()) return cached;
    for (int sz : {16, 24, 32, 48, 64, 128, 256}) {
        cached.addPixmap(renderIconPixmap(sz));
    }
    return cached;
}

namespace {

void drawFolderBase(QPainter& p, double s, const QColor& strokeCol) {
    QPainterPath folder;
    folder.moveTo(s * 0.14, s * 0.28);
    folder.lineTo(s * 0.38, s * 0.28);
    folder.lineTo(s * 0.45, s * 0.36);
    folder.lineTo(s * 0.86, s * 0.36);
    folder.lineTo(s * 0.86, s * 0.78);
    folder.lineTo(s * 0.14, s * 0.78);
    folder.closeSubpath();

    p.setPen(QPen(strokeCol, qMax(1.4, s * 0.065), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor("#232534"));
    p.drawPath(folder);
}

void drawDocumentBase(QPainter& p, double s, const QColor& strokeCol) {
    QPainterPath doc;
    doc.moveTo(s * 0.22, s * 0.14);
    doc.lineTo(s * 0.62, s * 0.14);
    doc.lineTo(s * 0.78, s * 0.30);
    doc.lineTo(s * 0.78, s * 0.86);
    doc.lineTo(s * 0.22, s * 0.86);
    doc.closeSubpath();

    p.setPen(QPen(strokeCol, qMax(1.4, s * 0.065), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor("#232534"));
    p.drawPath(doc);

    QPainterPath fold;
    fold.moveTo(s * 0.62, s * 0.14);
    fold.lineTo(s * 0.62, s * 0.30);
    fold.lineTo(s * 0.78, s * 0.30);
    p.setBrush(Qt::NoBrush);
    p.drawPath(fold);
}

void drawRefreshArrows(QPainter& p, QPointF center, double radius, double strokeW, const QColor& col) {
    p.setPen(QPen(col, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
    p.drawArc(arcRect, 35 * 16, 125 * 16);
    p.drawArc(arcRect, 215 * 16, 125 * 16);

    // Стрелки на концах дуг
    double a1 = 35.0 * M_PI / 180.0;
    QPointF tip1(center.x() + radius * std::cos(a1), center.y() - radius * std::sin(a1));
    QPainterPath arr1;
    arr1.moveTo(tip1.x() - strokeW * 1.6, tip1.y() - strokeW * 0.6);
    arr1.lineTo(tip1);
    arr1.lineTo(tip1.x() + strokeW * 0.5, tip1.y() - strokeW * 1.7);
    p.drawPath(arr1);

    double a2 = 215.0 * M_PI / 180.0;
    QPointF tip2(center.x() + radius * std::cos(a2), center.y() - radius * std::sin(a2));
    QPainterPath arr2;
    arr2.moveTo(tip2.x() + strokeW * 1.6, tip2.y() + strokeW * 0.6);
    arr2.lineTo(tip2);
    arr2.lineTo(tip2.x() - strokeW * 0.5, tip2.y() + strokeW * 1.7);
    p.drawPath(arr2);
}

void drawAiSpark(QPainter& p, QPointF center, double r, const QColor& col) {
    QPolygonF star;
    star << QPointF(center.x(), center.y() - r)
         << QPointF(center.x() + r * 0.38, center.y() - r * 0.38)
         << QPointF(center.x() + r, center.y())
         << QPointF(center.x() + r * 0.38, center.y() + r * 0.38)
         << QPointF(center.x(), center.y() + r)
         << QPointF(center.x() - r * 0.38, center.y() + r * 0.38)
         << QPointF(center.x() - r, center.y())
         << QPointF(center.x() - r * 0.38, center.y() - r * 0.38);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPolygon(star);
}

} // namespace

QPixmap AppIcon::renderActionPixmap(ActionIcon type, int size) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;

    switch (type) {
    case ActionIcon::ProjectCustomPrompt: {
        drawFolderBase(p, s, QColor("#bd93f9"));
        // Символ промпта >_ внутри папки
        p.setPen(QPen(QColor("#50fa7b"), qMax(1.4, s * 0.07), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chevron;
        chevron.moveTo(s * 0.28, s * 0.48);
        chevron.lineTo(s * 0.42, s * 0.57);
        chevron.lineTo(s * 0.28, s * 0.66);
        p.drawPath(chevron);
        p.drawLine(QPointF(s * 0.48, s * 0.66), QPointF(s * 0.64, s * 0.66));
        drawAiSpark(p, QPointF(s * 0.74, s * 0.24), s * 0.14, QColor("#8be9fd"));
        break;
    }
    case ActionIcon::ProjectSummaryRebuild: {
        drawFolderBase(p, s, QColor("#bd93f9"));
        drawRefreshArrows(p, QPointF(s * 0.50, s * 0.57), s * 0.16, qMax(1.4, s * 0.065), QColor("#50fa7b"));
        break;
    }
    case ActionIcon::ProjectRecursiveSummary: {
        // Иерархическое дерево файлов проекта + искра ИИ
        p.setPen(QPen(QColor("#8be9fd"), qMax(1.3, s * 0.06), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(s * 0.28, s * 0.26), QPointF(s * 0.28, s * 0.76));
        p.drawLine(QPointF(s * 0.28, s * 0.50), QPointF(s * 0.52, s * 0.50));
        p.drawLine(QPointF(s * 0.28, s * 0.76), QPointF(s * 0.52, s * 0.76));

        p.setPen(QPen(QColor("#bd93f9"), qMax(1.2, s * 0.05)));
        p.setBrush(QColor("#bd93f9"));
        p.drawRoundedRect(QRectF(s * 0.14, s * 0.14, s * 0.30, s * 0.18), s * 0.04, s * 0.04);

        p.setPen(QPen(QColor("#50fa7b"), qMax(1.2, s * 0.05)));
        p.setBrush(QColor("#232534"));
        p.drawRoundedRect(QRectF(s * 0.52, s * 0.41, s * 0.32, s * 0.18), s * 0.04, s * 0.04);
        p.drawRoundedRect(QRectF(s * 0.52, s * 0.67, s * 0.32, s * 0.18), s * 0.04, s * 0.04);

        drawAiSpark(p, QPointF(s * 0.74, s * 0.22), s * 0.14, QColor("#ffb86c"));
        break;
    }
    case ActionIcon::FileCustomPrompt: {
        drawDocumentBase(p, s, QColor("#8be9fd"));
        p.setPen(QPen(QColor("#50fa7b"), qMax(1.4, s * 0.07), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chevron;
        chevron.moveTo(s * 0.33, s * 0.46);
        chevron.lineTo(s * 0.46, s * 0.56);
        chevron.lineTo(s * 0.33, s * 0.66);
        p.drawPath(chevron);
        p.drawLine(QPointF(s * 0.51, s * 0.66), QPointF(s * 0.66, s * 0.66));
        drawAiSpark(p, QPointF(s * 0.34, s * 0.28), s * 0.11, QColor("#ffb86c"));
        break;
    }
    case ActionIcon::FileSummaryRebuild: {
        drawDocumentBase(p, s, QColor("#8be9fd"));
        drawRefreshArrows(p, QPointF(s * 0.50, s * 0.56), s * 0.16, qMax(1.4, s * 0.065), QColor("#ffb86c"));
        break;
    }
    case ActionIcon::CodeExplain: {
        // Скобки кода < > и символ ? в центре
        p.setPen(QPen(QColor("#8be9fd"), qMax(1.5, s * 0.07), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath leftBr;
        leftBr.moveTo(s * 0.28, s * 0.28);
        leftBr.lineTo(s * 0.12, s * 0.50);
        leftBr.lineTo(s * 0.28, s * 0.72);
        p.drawPath(leftBr);

        QPainterPath rightBr;
        rightBr.moveTo(s * 0.72, s * 0.28);
        rightBr.lineTo(s * 0.88, s * 0.50);
        rightBr.lineTo(s * 0.72, s * 0.72);
        p.drawPath(rightBr);

        p.setPen(QPen(QColor("#f1fa8c"), qMax(1.6, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath qmark;
        qmark.moveTo(s * 0.42, s * 0.36);
        qmark.cubicTo(s * 0.42, s * 0.24, s * 0.60, s * 0.24, s * 0.60, s * 0.37);
        qmark.cubicTo(s * 0.60, s * 0.46, s * 0.50, s * 0.47, s * 0.50, s * 0.56);
        p.drawPath(qmark);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#f1fa8c"));
        p.drawEllipse(QPointF(s * 0.50, s * 0.70), qMax(1.2, s * 0.045), qMax(1.2, s * 0.045));
        break;
    }
    case ActionIcon::AddDocs: {
        drawDocumentBase(p, s, QColor("#bd93f9"));
        // Строки документации внутри листа
        p.setPen(QPen(QColor("#8be9fd"), qMax(1.3, s * 0.06), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(s * 0.33, s * 0.42), QPointF(s * 0.65, s * 0.42));
        p.drawLine(QPointF(s * 0.33, s * 0.55), QPointF(s * 0.58, s * 0.55));
        p.drawLine(QPointF(s * 0.33, s * 0.68), QPointF(s * 0.50, s * 0.68));

        // Круглый значок '+' в правом нижнем углу
        QPointF plusCenter(s * 0.74, s * 0.74);
        double badgeR = s * 0.18;
        p.setPen(QPen(QColor("#191a24"), qMax(1.0, s * 0.03)));
        p.setBrush(QColor("#50fa7b"));
        p.drawEllipse(plusCenter, badgeR, badgeR);

        p.setPen(QPen(QColor("#191a24"), qMax(1.5, s * 0.065), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(plusCenter.x() - badgeR * 0.52, plusCenter.y()),
                   QPointF(plusCenter.x() + badgeR * 0.52, plusCenter.y()));
        p.drawLine(QPointF(plusCenter.x(), plusCenter.y() - badgeR * 0.52),
                   QPointF(plusCenter.x(), plusCenter.y() + badgeR * 0.52));
        break;
    }
    }

    return px;
}

QIcon AppIcon::actionIcon(ActionIcon type) {
    static QMap<int, QIcon> cache;
    int key = static_cast<int>(type);
    if (cache.contains(key)) return cache.value(key);

    QIcon icon;
    for (int sz : {16, 20, 24, 32, 48}) {
        QPixmap normalPx = renderActionPixmap(type, sz);
        icon.addPixmap(normalPx, QIcon::Normal, QIcon::Off);

        // Полупрозрачный вариант для Disabled-состояния на тулбаре
        QPixmap disabledPx(sz, sz);
        disabledPx.fill(Qt::transparent);
        QPainter dp(&disabledPx);
        dp.setOpacity(0.35);
        dp.drawPixmap(0, 0, normalPx);
        dp.end();
        icon.addPixmap(disabledPx, QIcon::Disabled, QIcon::Off);
    }
    cache.insert(key, icon);
    return icon;
}
