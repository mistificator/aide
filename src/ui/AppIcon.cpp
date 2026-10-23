// src/ui/AppIcon.cpp
#include "AppIcon.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>

QPixmap AppIcon::renderIconPixmap(int size) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double s = size;
    const double pad = s * 0.06;
    const double r = s * 0.22;
    QRectF bgRect(pad, pad, s - 2 * pad, s - 2 * pad);

    // 1. Тёмный графитовый фон (скруглённый квадрат в стиле Dracula)
    QLinearGradient bgGrad(bgRect.topLeft(), bgRect.bottomRight());
    bgGrad.setColorAt(0.0, QColor("#282a36"));
    bgGrad.setColorAt(0.5, QColor("#191a24"));
    bgGrad.setColorAt(1.0, QColor("#11121a"));

    p.setPen(QPen(QColor("#bd93f9"), qMax(1.0, s * 0.035)));
    p.setBrush(bgGrad);
    p.drawRoundedRect(bgRect, r, r);

    // 2. Внутренняя тонкая технологическая рамка
    if (size >= 32) {
        QRectF innerRect = bgRect.adjusted(s * 0.04, s * 0.04, -s * 0.04, -s * 0.04);
        p.setPen(QPen(QColor(139, 233, 253, 55), qMax(1.0, s * 0.012)));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(innerRect, r * 0.82, r * 0.82);
    }

    // 3. Боковые кодовые скобки < и > (бирюзовый #8be9fd)
    QPen bracketPen(QColor("#8be9fd"), qMax(1.4, s * 0.065), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(bracketPen);
    p.setBrush(Qt::NoBrush);

    // Левая скобка '<'
    QPainterPath leftBr;
    leftBr.moveTo(s * 0.27, s * 0.34);
    leftBr.lineTo(s * 0.16, s * 0.50);
    leftBr.lineTo(s * 0.27, s * 0.66);
    p.drawPath(leftBr);

    // Правая скобка '>'
    QPainterPath rightBr;
    rightBr.moveTo(s * 0.73, s * 0.34);
    rightBr.lineTo(s * 0.84, s * 0.50);
    rightBr.lineTo(s * 0.73, s * 0.66);
    p.drawPath(rightBr);

    // 4. Центральная монограмма 'A' (градиент от белого к пурпурному #bd93f9)
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

    // 5. Светящийся нейро-узел (ромб на месте перекладины 'A', #50fa7b)
    QPointF center(s * 0.50, s * 0.55);
    double diamondR = s * 0.085;

    QRadialGradient glow(center, diamondR * 2.2);
    glow.setColorAt(0.0, QColor(80, 250, 123, 180));
    glow.setColorAt(1.0, QColor(80, 250, 123, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(center, diamondR * 2.2, diamondR * 2.2);

    // Линия-мост перекладины
    p.setPen(QPen(QColor("#50fa7b"), qMax(1.2, s * 0.045), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(s * 0.39, s * 0.55), QPointF(s * 0.61, s * 0.55));

    // Центральный кристалл
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
