#include "ui/PixelTheme.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QEvent>
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace {

// 12x12 bitmap pixel-art definitions ('#' = filled pixel, '.' = transparent)
using PixelGrid = std::array<const char*, 12>;

PixelGrid gridFor(PixelIcon kind)
{
    switch (kind) {
    case PixelIcon::Play:
        return {{
            "............",
            "..##........",
            "..####......",
            "..######....",
            "..########..",
            "..#########.",
            "..#########.",
            "..########..",
            "..######....",
            "..####......",
            "..##........",
            "............",
        }};
    case PixelIcon::Pause:
        return {{
            "............",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "............",
        }};
    case PixelIcon::Reset:
    case PixelIcon::RestoreDefaults:
        return {{
            "............",
            "....#####...",
            "..##.....##.",
            ".##.......#.",
            ".##.....####",
            ".##......###",
            ".##.........",
            ".##.......#.",
            "..##.....##.",
            "....#####...",
            "............",
            "............",
        }};
    case PixelIcon::Bell:
        return {{
            ".....##.....",
            "....####....",
            "...######...",
            "...######...",
            "..########..",
            "..########..",
            "..########..",
            ".##########.",
            ".##########.",
            "....####....",
            ".....##.....",
            "............",
        }};
    case PixelIcon::Volume:
        return {{
            "............",
            ".....##.....",
            "....###..#..",
            "..#####...#.",
            ".######.#.#.",
            ".######.#.#.",
            ".######.#.#.",
            "..#####...#.",
            "....###..#..",
            ".....##.....",
            "............",
            "............",
        }};
    case PixelIcon::Preview:
        return {{
            "............",
            ".##.........",
            ".####....#..",
            ".######...#.",
            ".#######.#.#",
            ".#######.#.#",
            ".######...#.",
            ".####....#..",
            ".##.........",
            "............",
            "............",
            "............",
        }};
    case PixelIcon::Snooze:
        return {{
            "............",
            "..######....",
            "......##....",
            ".....##.....",
            "....##......",
            "...##.......",
            "..######....",
            ".......####.",
            ".........##.",
            "........##..",
            ".......####.",
            "............",
        }};
    case PixelIcon::Close:
        return {{
            "............",
            "..##....##..",
            "..###..###..",
            "...######...",
            "....####....",
            "....####....",
            "...######...",
            "..###..###..",
            "..##....##..",
            "............",
            "............",
            "............",
        }};
    case PixelIcon::ChevronDown:
        return {{
            "............",
            "............",
            "............",
            ".##......##.",
            "..##....##..",
            "...##..##...",
            "....####....",
            ".....##.....",
            "............",
            "............",
            "............",
            "............",
        }};
    case PixelIcon::ChevronRight:
        return {{
            "............",
            "...##.......",
            "...###......",
            "....###.....",
            ".....###....",
            "......###...",
            ".....###....",
            "....###.....",
            "...###......",
            "...##.......",
            "............",
            "............",
        }};
    case PixelIcon::Exit:
        return {{
            "............",
            ".#####......",
            ".##.........",
            ".##....##...",
            ".##.....##..",
            ".##..######.",
            ".##..######.",
            ".##.....##..",
            ".##....##...",
            ".##.........",
            ".#####......",
            "............",
        }};
    case PixelIcon::Edit:
        return {{
            "........##..",
            ".......####.",
            "......####..",
            ".....####...",
            "....####....",
            "...####.....",
            "..####......",
            ".####.......",
            ".###........",
            ".##.........",
            "............",
            "............",
        }};
    case PixelIcon::Help:
        return {{
            "............",
            "...######...",
            "..##....##..",
            "..##....##..",
            ".......##...",
            "......##....",
            ".....##.....",
            ".....##.....",
            "............",
            ".....##.....",
            ".....##.....",
            "............",
        }};
    case PixelIcon::Gear:
    case PixelIcon::Spotify:
    case PixelIcon::Discord:
    case PixelIcon::LeagueOfLegends:
    case PixelIcon::GenericApp:
    case PixelIcon::Code:
    case PixelIcon::Study:
        return {{
            "....####....",
            ".##.####.##.",
            ".##########.",
            "..########..",
            "####....####",
            "####....####",
            "####....####",
            "####....####",
            "..########..",
            ".##########.",
            ".##.####.##.",
            "....####....",
        }};
    }
    return {{
        "............", "............", "............", "............",
        "............", "............", "............", "............",
        "............", "............", "............", "............",
    }};
}

} // namespace

namespace PixelTheme {

namespace {
bool g_fontsLoaded = false;
QString g_displayFamily;
QString g_bodyFamily;
} // namespace

void ensureFontsLoaded()
{
    if (g_fontsLoaded) {
        return;
    }
    g_fontsLoaded = true;

    const int displayId = QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/PixelifySans.ttf"));
    if (displayId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(displayId);
        if (!families.isEmpty()) {
            g_displayFamily = families.first();
        }
    }
    if (g_displayFamily.isEmpty()) {
        g_displayFamily = QStringLiteral("Segoe UI");
    }

    const int bodyId = QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/Nunito.ttf"));
    if (bodyId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(bodyId);
        if (!families.isEmpty()) {
            g_bodyFamily = families.first();
        }
    }
    if (g_bodyFamily.isEmpty()) {
        g_bodyFamily = QStringLiteral("Segoe UI");
    }
}

QString displayFontFamily()
{
    ensureFontsLoaded();
    return g_displayFamily;
}

QString bodyFontFamily()
{
    ensureFontsLoaded();
    return g_bodyFamily;
}

QIcon icon(PixelIcon kind, const QColor& color, int logicalSize)
{
    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1_%2_%3")
        .arg(static_cast<int>(kind))
        .arg(color.rgba(), 8, 16, QChar(u'0'))
        .arg(logicalSize);
    if (auto it = cache.constFind(key); it != cache.constEnd()) {
        return it.value();
    }

    constexpr int dpr = 2;
    const int px = std::max(12, logicalSize) * dpr;
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(static_cast<qreal>(dpr));

    QPainter p(&pm);
    if (kind == PixelIcon::Reset) {
        // Crisp, centered counterclockwise circular arrow
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        const qreal stroke = std::max<qreal>(1.8, s * 0.135);
        const qreal margin = stroke * 0.95 + 1.0;
        const QRectF arcRect(margin, margin, s - margin * 2.0, s - margin * 2.0);

        QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // 270-degree arc ending at 135 deg (top-left) going counterclockwise:
        // In Qt angles (1/16 deg, 0=3 o'clock, positive=CCW):
        // start at 150 deg (top-left) and sweep -275 deg (clockwise back around) so arrowhead points CCW at 140 deg
        p.drawArc(arcRect, 145 * 16, -280 * 16);

        // Counterclockwise arrowhead at top-left of circle (pointing down-left)
        const qreal cx = arcRect.center().x();
        const qreal cy = arcRect.center().y();
        const qreal r = arcRect.width() * 0.5;
        // Point at ~142 degrees
        const qreal ax = cx - r * 0.78;
        const qreal ay = cy - r * 0.62;
        const qreal hs = std::max<qreal>(2.6, s * 0.22);

        QPolygonF head;
        head << QPointF(ax - hs * 0.35, ay + hs * 0.65)
             << QPointF(ax - hs * 0.55, ay - hs * 0.65)
             << QPointF(ax + hs * 0.80, ay - hs * 0.10);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(head);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Gear) {
        // Crisp, symmetrical 8-tooth pixel-inspired cog with distinct teeth and clear center hole
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        const qreal cx = s * 0.5;
        const qreal cy = s * 0.5;
        const qreal ringR = s * 0.32;
        const qreal toothOuter = s * 0.46;
        const qreal toothHalfW = std::max<qreal>(1.15, s * 0.082);
        const qreal holeR = s * 0.145;

        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // 8 evenly distributed cog teeth (0, 45, 90, ..., 315 degrees)
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(cx, cy);
            p.rotate(i * 45.0);
            const QRectF tooth(-toothHalfW, -toothOuter, toothHalfW * 2.0, toothOuter - ringR + 1.4);
            p.drawRoundedRect(tooth, 0.9, 0.9);
            p.restore();
        }

        // Circular cog body
        p.drawEllipse(QPointF(cx, cy), ringR, ringR);

        // Punch out crisp circular center hole
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.drawEllipse(QPointF(cx, cy), holeR, holeR);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Spotify) {
        // Recognizable Spotify circular mark with three gently arched soundwaves
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        const qreal cx = s * 0.5;
        const qreal cy = s * 0.5;
        const qreal radius = s * 0.45;

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(cx, cy), radius, radius);

        // Cut out three slightly tilted concentric soundwave arcs with rounded caps
        p.save();
        p.translate(cx, cy);
        p.rotate(8.0);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setBrush(Qt::NoBrush);

        auto drawWave = [&](qreal halfSpan, qreal yEnd, qreal yCtrl, qreal widthFactor) {
            QPen wavePen(Qt::black, std::max<qreal>(1.1, s * widthFactor),
                         Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(wavePen);
            QPainterPath arc;
            arc.moveTo(-s * halfSpan, s * yEnd);
            arc.quadTo(0.0, s * yCtrl, s * halfSpan, s * yEnd);
            p.drawPath(arc);
        };

        drawWave(0.265, -0.075, -0.205, 0.095);
        drawWave(0.220,  0.055, -0.055, 0.082);
        drawWave(0.170,  0.175,  0.090, 0.070);
        p.restore();

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Exit) {
        // Crisp vector exit doorway + outgoing arrow (stays sharp at 1.22x hover scale & rotation)
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        const qreal stroke = std::max<qreal>(1.5, s * 0.115);
        QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Open doorway bracket on the left
        QPainterPath door;
        const qreal left = s * 0.16;
        const qreal rightEdge = s * 0.50;
        const qreal top = s * 0.17;
        const qreal bottom = s * 0.83;
        door.moveTo(rightEdge, top);
        door.lineTo(left + s * 0.08, top);
        door.quadTo(left, top, left, top + s * 0.08);
        door.lineTo(left, bottom - s * 0.08);
        door.quadTo(left, bottom, left + s * 0.08, bottom);
        door.lineTo(rightEdge, bottom);
        p.drawPath(door);

        // Outgoing arrow pointing right
        const qreal midY = s * 0.50;
        const qreal arrowStart = s * 0.40;
        const qreal arrowTip = s * 0.85;
        const qreal head = s * 0.18;
        p.drawLine(QPointF(arrowStart, midY), QPointF(arrowTip, midY));
        QPainterPath arrowHead;
        arrowHead.moveTo(arrowTip - head, midY - head);
        arrowHead.lineTo(arrowTip, midY);
        arrowHead.lineTo(arrowTip - head, midY + head);
        p.drawPath(arrowHead);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Discord) {
        // Recognizable Discord Clyde mark with rounded silhouette and two cutout eyes
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        QPainterPath body;
        body.moveTo(s * 0.24, s * 0.22);
        body.quadTo(s * 0.34, s * 0.17, s * 0.42, s * 0.20);
        body.lineTo(s * 0.44, s * 0.24);
        body.quadTo(s * 0.50, s * 0.23, s * 0.56, s * 0.24);
        body.lineTo(s * 0.58, s * 0.20);
        body.quadTo(s * 0.66, s * 0.17, s * 0.76, s * 0.22);
        body.cubicTo(s * 0.87, s * 0.38, s * 0.90, s * 0.58, s * 0.86, s * 0.74);
        body.quadTo(s * 0.76, s * 0.82, s * 0.65, s * 0.84);
        body.lineTo(s * 0.61, s * 0.76);
        body.quadTo(s * 0.50, s * 0.78, s * 0.39, s * 0.76);
        body.lineTo(s * 0.35, s * 0.84);
        body.quadTo(s * 0.24, s * 0.82, s * 0.14, s * 0.74);
        body.cubicTo(s * 0.10, s * 0.58, s * 0.13, s * 0.38, s * 0.24, s * 0.22);
        body.closeSubpath();
        p.drawPath(body);

        // Two clean circular cutout eyes
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        const qreal eyeR = s * 0.088;
        p.drawEllipse(QPointF(s * 0.375, s * 0.535), eyeR, eyeR * 1.08);
        p.drawEllipse(QPointF(s * 0.625, s * 0.535), eyeR, eyeR * 1.08);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::LeagueOfLegends) {
        // Recognizable League of Legends gold L emblem framed by an open circular crest
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);

        const qreal ringStroke = std::max<qreal>(1.15, s * 0.075);
        QColor ringColor = color;
        ringColor.setAlpha(185);
        p.setPen(QPen(ringColor, ringStroke, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const QRectF ringRect(s * 0.13, s * 0.13, s * 0.74, s * 0.74);
        p.drawArc(ringRect, -25 * 16, 105 * 16);
        p.drawArc(ringRect, 145 * 16, 85 * 16);

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPainterPath letter;
        letter.moveTo(s * 0.29, s * 0.16);
        letter.lineTo(s * 0.49, s * 0.16);
        letter.lineTo(s * 0.45, s * 0.24);
        letter.lineTo(s * 0.45, s * 0.69);
        letter.lineTo(s * 0.67, s * 0.69);
        letter.lineTo(s * 0.75, s * 0.62);
        letter.lineTo(s * 0.71, s * 0.83);
        letter.lineTo(s * 0.28, s * 0.83);
        letter.lineTo(s * 0.33, s * 0.75);
        letter.lineTo(s * 0.33, s * 0.24);
        letter.closeSubpath();
        p.drawPath(letter);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Code) {
        // Generic code-editor mark: angle brackets around a slash (not a vendor logo).
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        QPen pen(color, std::max<qreal>(1.6, s * 0.10), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QPainterPath left;
        left.moveTo(s * 0.34, s * 0.24);
        left.lineTo(s * 0.12, s * 0.50);
        left.lineTo(s * 0.34, s * 0.76);
        p.drawPath(left);
        QPainterPath right;
        right.moveTo(s * 0.66, s * 0.24);
        right.lineTo(s * 0.88, s * 0.50);
        right.lineTo(s * 0.66, s * 0.76);
        p.drawPath(right);
        p.drawLine(QPointF(s * 0.56, s * 0.18), QPointF(s * 0.44, s * 0.82));
        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Study) {
        // A nerd face: round yellow face, big glasses, a small smile with buck teeth. The tint only
        // decides whether the face is drawn in its resting or brighter hover shade.
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        const bool hover = color == PixelTheme::kPrimaryAccentHover;
        const QColor face = hover ? QColor(0xFF, 0xE0, 0x66) : QColor(0xFF, 0xCF, 0x3D);
        const QColor outline(0x8A, 0x5A, 0x00);
        const QColor ink(0x3A, 0x26, 0x00);

        p.setPen(QPen(outline, std::max<qreal>(1.0, s * 0.045)));
        p.setBrush(face);
        p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.44, s * 0.44);

        // Glasses: two lenses joined by a bridge, with the pupils inside.
        const qreal lensR = s * 0.165;
        const QPointF leftLens(s * 0.33, s * 0.43);
        const QPointF rightLens(s * 0.67, s * 0.43);
        p.setPen(QPen(ink, std::max<qreal>(1.2, s * 0.06), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(leftLens.x() + lensR, leftLens.y() - s * 0.01),
                   QPointF(rightLens.x() - lensR, rightLens.y() - s * 0.01));
        p.setBrush(QColor(255, 255, 255, 245));
        p.drawEllipse(leftLens, lensR, lensR);
        p.drawEllipse(rightLens, lensR, lensR);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawEllipse(QPointF(leftLens.x() + s * 0.02, leftLens.y() + s * 0.01), s * 0.05, s * 0.05);
        p.drawEllipse(QPointF(rightLens.x() + s * 0.02, rightLens.y() + s * 0.01), s * 0.05, s * 0.05);

        // Smile with two buck teeth.
        p.setPen(QPen(ink, std::max<qreal>(1.1, s * 0.05), Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath smile;
        smile.moveTo(s * 0.34, s * 0.69);
        smile.quadTo(s * 0.5, s * 0.82, s * 0.66, s * 0.69);
        p.drawPath(smile);
        p.setPen(QPen(ink, std::max<qreal>(0.8, s * 0.025)));
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(s * 0.44, s * 0.735, s * 0.06, s * 0.085), s * 0.012, s * 0.012);
        p.drawRoundedRect(QRectF(s * 0.50, s * 0.735, s * 0.06, s * 0.085), s * 0.012, s * 0.012);
        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::GenericApp) {
        // Isometric faceted cube used as the fallback application mark
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        const QPointF top(s * 0.50, s * 0.10);
        const QPointF topRight(s * 0.85, s * 0.30);
        const QPointF botRight(s * 0.85, s * 0.70);
        const QPointF bottom(s * 0.50, s * 0.90);
        const QPointF botLeft(s * 0.15, s * 0.70);
        const QPointF topLeft(s * 0.15, s * 0.30);
        const QPointF center(s * 0.50, s * 0.50);

        p.setPen(Qt::NoPen);

        QColor leftFace = color;
        leftFace.setAlpha(155);
        p.setBrush(leftFace);
        QPolygonF leftPoly;
        leftPoly << top << center << bottom << botLeft << topLeft;
        p.drawPolygon(leftPoly);

        QColor rightFace = color;
        rightFace.setAlpha(205);
        p.setBrush(rightFace);
        QPolygonF rightPoly;
        rightPoly << topRight << botRight << bottom << center;
        p.drawPolygon(rightPoly);

        p.setBrush(color);
        QPolygonF topPoly;
        topPoly << top << topRight << center << topLeft;
        p.drawPolygon(topPoly);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    if (kind == PixelIcon::Edit) {
        // Crisp, immediately recognizable diagonal pencil: eraser cap + ferrule gap + barrel + pointed tip
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal s = static_cast<qreal>(logicalSize);
        p.translate(s * 0.5, s * 0.5);
        p.rotate(-45.0);

        const qreal halfH = std::max<qreal>(1.8, s * 0.135);
        const qreal eraserX1 = s * 0.23;
        const qreal eraserX2 = s * 0.42;
        const qreal bodyX1 = -s * 0.22;
        const qreal bodyX2 = s * 0.15;
        const qreal tipX = -s * 0.45;

        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // Eraser end cap (top-right after -45deg rotation)
        p.drawRoundedRect(QRectF(eraserX1, -halfH, eraserX2 - eraserX1, halfH * 2.0), 1.0, 1.0);

        // Pencil barrel with clean ferrule separation from eraser
        p.drawRect(QRectF(bodyX1, -halfH, bodyX2 - bodyX1, halfH * 2.0));

        // Subtle inner highlight groove along barrel so silhouette reads clearly as a pencil
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(QRectF(bodyX1 + 0.8, -halfH * 0.22, (bodyX2 - bodyX1) - 1.6, halfH * 0.44), Qt::transparent);
        p.restore();

        // Sharpened cone + distinct lead point (bottom-left after -45deg rotation)
        QPolygonF cone;
        cone << QPointF(bodyX1 - 0.8, -halfH)
             << QPointF(bodyX1 - 0.8, halfH)
             << QPointF(tipX, 0.0);
        p.setBrush(color);
        p.drawPolygon(cone);

        p.end();
        QIcon result(pm);
        cache.insert(key, result);
        return result;
    }

    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const auto grid = gridFor(kind);
    const qreal cell = static_cast<qreal>(logicalSize) / 12.0;
    for (int y = 0; y < 12; ++y) {
        const char* row = grid[static_cast<std::size_t>(y)];
        for (int x = 0; x < 12; ++x) {
            if (row[x] == '#') {
                const int rx = qRound(x * cell);
                const int ry = qRound(y * cell);
                const int rw = std::max(1, qRound((x + 1) * cell) - rx);
                const int rh = std::max(1, qRound((y + 1) * cell) - ry);
                p.fillRect(rx, ry, rw, rh, color);
            }
        }
    }

    QIcon result(pm);
    cache.insert(key, result);
    return result;
}

void drawDropdownArrow(QPainter& p, const QRectF& area, bool pointingUp, int size)
{
    static QPixmap downPm;
    static QPixmap upPm;
    if (downPm.isNull()) {
        downPm = QPixmap(QStringLiteral(":/assets/icons/arrow_down.png"));
    }
    if (upPm.isNull()) {
        upPm = QPixmap(QStringLiteral(":/assets/icons/arrow_up.png"));
    }
    const QPixmap& src = pointingUp ? upPm : downPm;
    if (src.isNull()) {
        return;
    }
    const QRectF target(area.center().x() - size * 0.5,
                        area.center().y() - size * 0.5,
                        size,
                        size);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawPixmap(target, src, QRectF(src.rect()));
    p.restore();
}

void paintGlassSurface(QPainter& p,
                       const QRect& bounds,
                       GlassLevel level,
                       qreal radius,
                       int margin)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal resolvedRadius = (radius >= 0.0)
        ? radius
        : (level == GlassLevel::Primary
           ? kRadiusWindow
           : (level == GlassLevel::Secondary ? kRadiusCard : kRadiusBubble));

    const QRectF card = QRectF(bounds).adjusted(margin, margin, -margin, -margin);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, resolvedRadius, resolvedRadius);

    // Minimal ambient shadow only on floating top-level windows (no dark halo).
    p.setPen(Qt::NoPen);
    if (level != GlassLevel::Secondary) {
        for (int i = 2; i >= 1; --i) {
            const qreal spread = static_cast<qreal>(i) * 1.1;
            QPainterPath shadowPath;
            shadowPath.addRoundedRect(card.adjusted(-spread * 0.3, spread * 0.4, spread * 0.3, spread * 0.8),
                                      resolvedRadius + spread * 0.25, resolvedRadius + spread * 0.25);
            p.setBrush(QColor(0, 0, 0, 4 + (3 - i) * 3));
            p.drawPath(shadowPath);
        }
    }

    // Clean, uniform smoked-black translucent fill so the desktop stays clearly visible.
    const int alpha = level == GlassLevel::Primary
        ? kGlassPrimaryAlpha
        : (level == GlassLevel::Secondary ? kGlassSecondaryAlpha : kGlassCompactAlpha);
    QColor fill = level == GlassLevel::Primary
        ? kGlassPrimaryTint
        : (level == GlassLevel::Secondary ? kGlassSurface : kGlassCompactTint);
    fill.setAlpha(alpha);
    p.setBrush(fill);
    p.setPen(Qt::NoPen);
    p.drawPath(cardPath);

    // Ultra-subtle translucent outer boundary (~3% white, no visible white rim).
    if (level != GlassLevel::Secondary) {
        p.setPen(QPen(kBorderSubtle, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(cardPath);
    }

    p.restore();
}

void paintPixelCard(QPainter& p,
                    const QRect& bounds,
                    const QColor& fill,
                    const QColor& border,
                    int margin)
{
    Q_UNUSED(fill);
    Q_UNUSED(border);
    paintGlassSurface(p, bounds, GlassLevel::Primary, kRadiusWindow, margin);
}

QString styleSheet()
{
    return QStringLiteral(R"(
        QWidget#header, QWidget#scrollContent, QScrollArea#bodyScroll, QWidget#sectionBody {
            background: transparent;
        }
        QLabel {
            color: #F3F6FC;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            background: transparent;
        }
        QLabel#title {
            color: #F3F6FC;
            font-size: 15px;
            font-weight: 700;
            letter-spacing: 0.4px;
        }
        QLabel#toastTitle {
            color: #F3F6FC;
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#onboardingWelcomeHeading {
            color: #F3F6FC;
            font-family: '%1', 'Segoe UI', sans-serif;
            font-size: 44px;
            font-weight: 500;
            letter-spacing: 0.8px;
        }
        QLabel#onboardingAppTitle {
            color: #8AB9FF;
            font-family: '%1', 'Segoe UI', sans-serif;
            font-size: 24px;
            font-weight: 500;
            letter-spacing: 0.6px;
        }
        QLabel#onboardingHeading {
            color: #F3F6FC;
            font-family: '%1', 'Segoe UI', sans-serif;
            font-size: 30px;
            font-weight: 500;
            letter-spacing: 0.6px;
        }
        QLabel#onboardingBody {
            color: #BCC7D9;
            font-family: '%2', 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 16px;
            font-weight: 500;
        }
        QLabel#onboardingProgress {
            color: #BCC7D9;
            font-family: '%2', 'Segoe UI', sans-serif;
            font-size: 14px;
            font-weight: 700;
        }
        QLabel#onboardingFooter {
            color: #8E9AAD;
            font-family: '%2', 'Segoe UI Symbol', 'Segoe UI', sans-serif;
            font-size: 12px;
            font-weight: 600;
            letter-spacing: 0.2px;
        }
        QPushButton#onboardingPrimary {
            background: rgba(85, 150, 245, 132);
            color: #FFFFFF;
            border: 1px solid rgba(155, 198, 255, 45);
            border-radius: 8px;
            padding: 7px 20px;
            font-family: '%2', 'Segoe UI', sans-serif;
            font-size: 16px;
            font-weight: 700;
            min-height: 24px;
        }
        QPushButton#onboardingPrimary:hover {
            background: rgba(105, 166, 252, 165);
            border-color: rgba(180, 214, 255, 72);
        }
        QPushButton#onboardingPrimary:pressed {
            background: rgba(70, 132, 225, 185);
            border-color: rgba(155, 198, 255, 56);
        }
        QPushButton#onboardingPrimary:focus {
            border: 1px solid rgba(156, 198, 255, 185);
        }
        QPushButton#onboardingSecondary {
            background: rgba(255, 255, 255, 22);
            color: #F3F6FC;
            border: 1px solid rgba(255, 255, 255, 12);
            border-radius: 8px;
            padding: 7px 18px;
            font-family: '%2', 'Segoe UI', sans-serif;
            font-size: 15px;
            font-weight: 700;
            min-height: 24px;
        }
        QPushButton#onboardingSecondary:hover {
            background: rgba(255, 255, 255, 36);
            border-color: rgba(255, 255, 255, 22);
        }
        QPushButton#onboardingSecondary:pressed {
            background: rgba(85, 150, 245, 105);
            color: #FFFFFF;
            border-color: rgba(138, 185, 255, 95);
        }
        QPushButton#onboardingSecondary:focus {
            border-color: rgba(110, 168, 255, 165);
        }
        QPushButton#onboardingSkip {
            background: rgba(20, 24, 32, 72);
            color: #BCC7D9;
            border: 1px solid rgba(255, 255, 255, 10);
            border-radius: 8px;
            padding: 7px 16px;
            font-family: '%2', 'Segoe UI', sans-serif;
            font-size: 15px;
            font-weight: 700;
            min-height: 24px;
        }
        QPushButton#onboardingSkip:hover {
            background: rgba(255, 255, 255, 30);
            color: #F3F6FC;
            border-color: rgba(255, 255, 255, 18);
        }
        QPushButton#onboardingSkip:pressed {
            background: rgba(255, 255, 255, 44);
            color: #FFFFFF;
        }
        QPushButton#onboardingSkip:focus {
            border-color: rgba(110, 168, 255, 165);
        }
        QLabel#sectionTitle {
            color: #F3F6FC;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#sectionSummary {
            color: #BCC7D9;
            font-size: 11px;
            font-weight: 600;
        }
        QLabel#muted {
            color: #BCC7D9;
            font-size: 11px;
        }
        QLabel#valueBadge {
            color: #BCC7D9;
            font-size: 11px;
            font-weight: 700;
        }
        QFrame#pixelDivider {
            background: rgba(255, 255, 255, 14);
            max-height: 1px;
            min-height: 1px;
            border: none;
        }
        QFrame#timerCard {
            background: rgba(20, 24, 32, 64);
            border: 1px solid rgba(255, 255, 255, 10);
            border-radius: 12px;
        }
        QPushButton#countdownDisplay {
            background: transparent;
            color: #F3F6FC;
            border: none;
            font-family: 'Cascadia Mono', 'Consolas', 'Segoe UI', monospace;
            font-size: 28px;
            font-weight: 700;
            padding: 1px 4px;
        }
        QPushButton#countdownDisplay:hover {
            color: #FFFFFF;
            background: rgba(110, 168, 255, 42);
            border-radius: 8px;
        }
        QPushButton#countdownDisplay:disabled {
            color: #F3F6FC;
            background: transparent;
        }
        QSpinBox, QTimeEdit {
            background: rgba(0, 0, 0, 72);
            border: 1px solid rgba(255, 255, 255, 12);
            border-radius: 8px;
            padding: 2px 20px 2px 6px;
            color: #F3F6FC;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            font-weight: 700;
            min-height: 24px;
            selection-background-color: rgba(85, 150, 245, 115);
            selection-color: #FFFFFF;
        }
        QSpinBox:hover, QTimeEdit:hover {
            border-color: rgba(138, 185, 255, 85);
            background: rgba(255, 255, 255, 18);
        }
        QSpinBox:focus, QTimeEdit:focus {
            border-color: rgba(110, 168, 255, 165);
            background: rgba(255, 255, 255, 22);
        }
        QSpinBox::up-button, QTimeEdit::up-button {
            subcontrol-origin: border;
            subcontrol-position: top right;
            width: 19px;
            border-left: 1px solid rgba(255, 255, 255, 12);
            border-bottom: 1px solid rgba(255, 255, 255, 10);
            background: rgba(255, 255, 255, 20);
            border-top-right-radius: 7px;
        }
        QSpinBox::down-button, QTimeEdit::down-button {
            subcontrol-origin: border;
            subcontrol-position: bottom right;
            width: 19px;
            border-left: 1px solid rgba(255, 255, 255, 12);
            border-top: 1px solid rgba(255, 255, 255, 10);
            background: rgba(255, 255, 255, 20);
            border-bottom-right-radius: 7px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover,
        QTimeEdit::up-button:hover, QTimeEdit::down-button:hover {
            background: rgba(255, 255, 255, 36);
        }
        QSpinBox::up-button:pressed, QSpinBox::down-button:pressed,
        QTimeEdit::up-button:pressed, QTimeEdit::down-button:pressed {
            background: rgba(85, 150, 245, 105);
        }
        QSpinBox::up-arrow, QTimeEdit::up-arrow {
            image: url(:/assets/icons/arrow_up.png);
            width: 9px;
            height: 9px;
        }
        QSpinBox::down-arrow, QTimeEdit::down-arrow {
            image: url(:/assets/icons/arrow_down.png);
            width: 9px;
            height: 9px;
        }
        QComboBox {
            background: rgba(0, 0, 0, 72);
            border: 1px solid rgba(255, 255, 255, 12);
            border-radius: 8px;
            padding: 2px 20px 2px 8px;
            color: #F3F6FC;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
            font-weight: 600;
            min-height: 20px;
        }
        QComboBox#timeSelect {
            padding: 2px 20px 2px 6px;
            font-family: 'Consolas', 'Cascadia Mono', 'Segoe UI', monospace;
            font-size: 12px;
            font-weight: 700;
        }
        QComboBox#timeSelect::drop-down {
            width: 20px;
        }
        QComboBox:hover {
            border-color: rgba(138, 185, 255, 85);
            background: rgba(255, 255, 255, 18);
        }
        QComboBox:focus {
            border-color: rgba(110, 168, 255, 165);
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 20px;
            border-left: 1px solid rgba(255, 255, 255, 12);
            background: rgba(255, 255, 255, 18);
            border-top-right-radius: 7px;
            border-bottom-right-radius: 7px;
        }
        QComboBox::drop-down:hover {
            background: rgba(255, 255, 255, 34);
        }
        QComboBox::down-arrow {
            image: url(:/assets/icons/arrow_down.png);
            width: 10px;
            height: 10px;
        }
    )").arg(displayFontFamily(), bodyFontFamily()) + QStringLiteral(R"(
        QComboBox QAbstractItemView {
            background: rgba(10, 12, 16, 210);
            border: 1px solid rgba(255, 255, 255, 16);
            border-radius: 8px;
            color: #F3F6FC;
            selection-background-color: rgba(85, 150, 245, 105);
            selection-color: #FFFFFF;
            outline: 0;
            padding: 3px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            font-weight: 600;
        }
        QComboBox QAbstractItemView::item {
            min-height: 24px;
            padding: 3px 8px;
            border-radius: 6px;
        }
        QComboBox QAbstractItemView::item:hover {
            background: rgba(255, 255, 255, 26);
        }
        QSlider {
            background: transparent;
            min-height: 22px;
        }
        QSlider::groove:horizontal {
            height: 5px;
            background: rgba(255, 255, 255, 28);
            border: none;
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: rgba(110, 168, 255, 145);
            border: none;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: rgba(235, 242, 255, 195);
            border: 1px solid rgba(110, 168, 255, 165);
            width: 12px;
            height: 12px;
            margin: -4px 0;
            border-radius: 6px;
        }
        QSlider::handle:horizontal:hover {
            background: rgba(255, 255, 255, 220);
            border-color: rgba(138, 185, 255, 200);
        }
        QSlider::handle:horizontal:pressed {
            background: rgba(138, 185, 255, 215);
            border-color: rgba(255, 255, 255, 190);
        }
        QPushButton {
            background: rgba(255, 255, 255, 22);
            color: #F3F6FC;
            border: 1px solid rgba(255, 255, 255, 12);
            border-radius: 8px;
            padding: 4px 8px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
            font-weight: 700;
            min-height: 18px;
        }
        QPushButton:hover {
            background: rgba(255, 255, 255, 36);
            border-color: rgba(255, 255, 255, 22);
        }
        QPushButton:pressed {
            background: rgba(85, 150, 245, 105);
            color: #FFFFFF;
            border-color: rgba(138, 185, 255, 95);
        }
        QPushButton:focus {
            border-color: rgba(110, 168, 255, 165);
        }
        QPushButton:disabled {
            background: rgba(255, 255, 255, 10);
            color: #637289;
            border-color: rgba(255, 255, 255, 8);
        }
        QPushButton#chip {
            background: rgba(0, 0, 0, 58);
            color: #C8D4E6;
            border: 1px solid rgba(255, 255, 255, 10);
            border-radius: 8px;
            padding: 3px 6px;
            min-height: 16px;
        }
        QPushButton#chip:hover {
            background: rgba(255, 255, 255, 30);
            color: #F3F6FC;
            border-color: rgba(255, 255, 255, 20);
        }
        QPushButton#chip[active="true"] {
            background: rgba(85, 150, 245, 92);
            color: #FFFFFF;
            border-color: rgba(138, 185, 255, 95);
        }
        QPushButton#chip[active="true"]:hover {
            background: rgba(105, 166, 252, 118);
        }
        QPushButton#primary {
            background: rgba(85, 150, 245, 132);
            color: #FFFFFF;
            border: 1px solid rgba(155, 198, 255, 45);
        }
        QPushButton#primary:hover {
            background: rgba(105, 166, 252, 165);
            border-color: rgba(180, 214, 255, 72);
        }
        QPushButton#primary:pressed {
            background: rgba(70, 132, 225, 185);
        }
        QPushButton#primary:disabled {
            background: rgba(255, 255, 255, 10);
            color: #637289;
            border-color: rgba(255, 255, 255, 8);
        }
        QPushButton#iconBtn {
            background: rgba(255, 255, 255, 22);
            border: 1px solid rgba(255, 255, 255, 12);
            border-radius: 8px;
            padding: 0px;
        }
        QPushButton#iconBtn:hover {
            background: rgba(255, 255, 255, 36);
            border-color: rgba(255, 255, 255, 24);
        }
        QPushButton#iconBtn:pressed {
            background: rgba(85, 150, 245, 105);
        }
        QPushButton#iconBtnPrimary {
            background: rgba(85, 150, 245, 132);
            border: 1px solid rgba(155, 198, 255, 45);
            border-radius: 8px;
            padding: 0px;
        }
        QPushButton#iconBtnPrimary:hover {
            background: rgba(105, 166, 252, 165);
            border-color: rgba(180, 214, 255, 72);
        }
        QPushButton#iconBtnPrimary:pressed {
            background: rgba(70, 132, 225, 185);
        }
        QPushButton#closeBtn {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 0px;
        }
        QPushButton#closeBtn:hover {
            background: rgba(255, 255, 255, 20);
            border-color: transparent;
        }
        QPushButton#closeBtn:pressed {
            background: rgba(255, 255, 255, 31);
            border-color: transparent;
        }
        QPushButton#closeBtn:focus {
            border-color: rgba(110, 168, 255, 165);
        }
        QPushButton#subtle {
            background: rgba(20, 24, 32, 68);
            color: #BCC7D9;
            border: 1px solid rgba(255, 255, 255, 10);
        }
        QPushButton#subtle:hover {
            background: rgba(255, 255, 255, 30);
            color: #F3F6FC;
            border-color: rgba(255, 255, 255, 20);
        }
        QPushButton#danger {
            background: rgba(160, 42, 62, 58);
            color: #F48C94;
            border: 1px solid rgba(244, 140, 148, 34);
        }
        QPushButton#danger:hover {
            background: rgba(185, 48, 72, 86);
            border-color: rgba(244, 140, 148, 62);
        }
        QPushButton#danger:pressed {
            background: rgba(205, 56, 82, 110);
        }
        QFrame#studyCard {
            background: rgba(20, 24, 32, 68);
            border: 1px solid rgba(255, 255, 255, 10);
            border-radius: 10px;
        }
        QProgressBar {
            background: rgba(255, 255, 255, 28);
            border: none;
            border-radius: 2px;
            max-height: 5px;
            min-height: 5px;
            text-align: center;
            color: transparent;
        }
        QProgressBar::chunk {
            background: rgba(110, 168, 255, 145);
            border-radius: 2px;
        }
        QToolTip {
            background: rgba(10, 12, 16, 210);
            color: #F3F6FC;
            border: 1px solid rgba(255, 255, 255, 16);
            border-radius: 8px;
            padding: 4px 8px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 6px;
            margin: 2px 0 2px 2px;
        }
        QScrollBar::handle:vertical {
            background: rgba(255, 255, 255, 36);
            min-height: 26px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(138, 185, 255, 95);
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
            border: none;
            background: none;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    )");
}

void paintCheckIndicator(QPainter& p, const QRectF& box, bool checked, bool hovered, bool down, bool enabled)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (checked) {
        const QColor fill = enabled
            ? (down ? kPrimaryAccentGlassPressed : (hovered ? kPrimaryAccentGlassHover : kPrimaryAccentGlass))
            : kDisabled;
        const QColor border = enabled ? QColor(0x9B, 0xC6, 0xFF, 58) : kBorder;
        p.setPen(QPen(border, 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(box, 4.0, 4.0);

        // Crisp opaque checkmark inside translucent 14x14 rounded box
        QPen checkPen(enabled ? Qt::white : kMutedText, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(checkPen);
        p.setBrush(Qt::NoBrush);
        QPainterPath check;
        check.moveTo(box.left() + 3.0, box.top() + 6.8);
        check.lineTo(box.left() + 5.6, box.top() + 9.4);
        check.lineTo(box.left() + 10.2, box.top() + 4.2);
        p.drawPath(check);
    } else {
        const QColor fill = hovered && enabled ? QColor(0xFF, 0xFF, 0xFF, 24) : QColor(0x00, 0x00, 0x00, 68);
        const QColor border = hovered && enabled ? QColor(0x8A, 0xB9, 0xFF, 110) : QColor(0xFF, 0xFF, 0xFF, 28);
        p.setPen(QPen(border, 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(box, 4.0, 4.0);
    }
    p.restore();
}

void paintRadioIndicator(QPainter& p, const QRectF& box, bool checked, bool hovered, bool down, bool enabled)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (checked) {
        const QColor fill = enabled
            ? (down ? kPrimaryAccentGlassPressed : (hovered ? kPrimaryAccentGlassHover : kPrimaryAccentGlass))
            : kDisabled;
        p.setPen(QPen(enabled ? QColor(0x9B, 0xC6, 0xFF, 58) : kBorder, 1.0));
        p.setBrush(fill);
        p.drawEllipse(box);
        p.setPen(Qt::NoPen);
        p.setBrush(enabled ? QColor(Qt::white) : kMutedText);
        p.drawEllipse(box.center(), 3.0, 3.0);
    } else {
        const QColor fill = hovered && enabled ? QColor(0xFF, 0xFF, 0xFF, 24) : QColor(0x00, 0x00, 0x00, 68);
        const QColor border = hovered && enabled ? QColor(0x8A, 0xB9, 0xFF, 110) : QColor(0xFF, 0xFF, 0xFF, 28);
        p.setPen(QPen(border, 1.0));
        p.setBrush(fill);
        p.drawEllipse(box);
    }
    p.restore();
}

QString textInputStyleSheet()
{
    // Same tokens as the shared QSpinBox/QTimeEdit/QComboBox inputs in styleSheet().
    return QStringLiteral(
        "QLineEdit, QPlainTextEdit {"
        "  background: rgba(0, 0, 0, 72);"
        "  border: 1px solid rgba(255, 255, 255, 12);"
        "  border-radius: 8px;"
        "  padding: 2px 8px;"
        "  color: #F3F6FC;"
        "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  min-height: 22px;"
        "  selection-background-color: rgba(85, 150, 245, 115);"
        "  selection-color: #FFFFFF;"
        "}"
        "QPlainTextEdit { padding: 4px 6px; }"
        "QLineEdit:hover, QPlainTextEdit:hover {"
        "  border-color: rgba(138, 185, 255, 85);"
        "  background: rgba(255, 255, 255, 18);"
        "}"
        "QLineEdit:focus, QPlainTextEdit:focus {"
        "  border-color: rgba(110, 168, 255, 165);"
        "  background: rgba(255, 255, 255, 22);"
        "}"
        "QLineEdit:disabled, QPlainTextEdit:disabled {"
        "  color: #8E9AAD;"
        "  background: rgba(255, 255, 255, 10);"
        "}");
}

} // namespace PixelTheme

// --- PixelCheckBox ---

PixelCheckBox::PixelCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
}

QSize PixelCheckBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int w = 4 + kBoxSize + kSpacing + fm.horizontalAdvance(text()) + 8;
    const int h = std::max(21, fm.height() + 4);
    return {w, h};
}

QSize PixelCheckBox::minimumSizeHint() const
{
    return sizeHint();
}

void PixelCheckBox::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QCheckBox::enterEvent(event);
}

void PixelCheckBox::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QCheckBox::leaveEvent(event);
}

void PixelCheckBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    if (m_hovered && isEnabled()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xFF, 0xFF, 0xFF, 16));
        p.drawRoundedRect(r.adjusted(1, 2, -1, -2), 6, 6);
    }

    const int boxY = (r.height() - kBoxSize) / 2;
    const QRectF box(4.5, boxY + 0.5, kBoxSize - 1.0, kBoxSize - 1.0);

    PixelTheme::paintCheckIndicator(p, box, isChecked(), m_hovered, isDown(), isEnabled());

    if (hasFocus()) {
        p.setPen(QPen(PixelTheme::kFocusBorder, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box.adjusted(-2.0, -2.0, 2.0, 2.0), 5.0, 5.0);
    }

    const QRect textRect(static_cast<int>(box.right()) + kSpacing, 0,
                         r.width() - static_cast<int>(box.right()) - kSpacing - 4, r.height());
    p.setPen(isEnabled() ? PixelTheme::kPrimaryText : PixelTheme::kMutedText);
    p.setFont(font());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
}

// --- PixelIconButton ---

PixelIconButton::PixelIconButton(PixelIcon iconKind,
                                 int buttonSize,
                                 int iconSize,
                                 bool primary,
                                 QWidget* parent)
    : QPushButton(parent)
    , m_iconKind(iconKind)
    , m_buttonSize(buttonSize)
    , m_iconSize(iconSize)
    , m_primary(primary)
{
    setFixedSize(buttonSize, buttonSize);
    if (iconKind == PixelIcon::Close && !primary) {
        setObjectName(QStringLiteral("closeBtn"));
    } else {
        setObjectName(primary ? QStringLiteral("iconBtnPrimary") : QStringLiteral("iconBtn"));
    }
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setIconSize(QSize(iconSize, iconSize));
    refreshIcon();
}

void PixelIconButton::setPixelIcon(PixelIcon iconKind)
{
    m_iconKind = iconKind;
    refreshIcon();
}

void PixelIconButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    if (m_iconKind == PixelIcon::Close && !m_primary) {
        refreshIcon();
    }
    QPushButton::enterEvent(event);
}

void PixelIconButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    if (m_iconKind == PixelIcon::Close && !m_primary) {
        refreshIcon();
    }
    QPushButton::leaveEvent(event);
}

void PixelIconButton::changeEvent(QEvent* event)
{
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        refreshIcon();
    }
}

void PixelIconButton::refreshIcon()
{
    QColor color;
    if (!isEnabled()) {
        color = PixelTheme::kSecondaryText;
    } else if (m_primary) {
        color = QColor(Qt::white);
    } else if (m_iconKind == PixelIcon::Close) {
        color = m_hovered ? QColor(Qt::white) : PixelTheme::kSecondaryText;
    } else {
        color = PixelTheme::kPrimaryText;
    }
    setIcon(PixelTheme::icon(m_iconKind, color, m_iconSize));
}

// --- PixelConfirmDialog ---

PixelConfirmDialog::PixelConfirmDialog(const QString& title,
                                       const QString& body,
                                       const QString& confirmText,
                                       const QString& cancelText,
                                       QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(268);
    setStyleSheet(PixelTheme::styleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(10);

    auto* titleLbl = new QLabel(title, this);
    titleLbl->setObjectName(QStringLiteral("title"));
    titleLbl->setWordWrap(true);
    root->addWidget(titleLbl);

    auto* bodyLbl = new QLabel(body, this);
    bodyLbl->setObjectName(QStringLiteral("muted"));
    bodyLbl->setWordWrap(true);
    root->addWidget(bodyLbl);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto* cancelBtn = new QPushButton(cancelText, this);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    auto* confirmBtn = new QPushButton(confirmText, this);
    confirmBtn->setObjectName(QStringLiteral("primary"));
    confirmBtn->setCursor(Qt::PointingHandCursor);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirmBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnRow->addWidget(cancelBtn, 1);
    btnRow->addWidget(confirmBtn, 1);
    root->addLayout(btnRow);

    adjustSize();
}

void PixelConfirmDialog::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintGlassSurface(p, rect(), PixelTheme::GlassLevel::Primary, PixelTheme::kRadiusWindow, 5);
}

// --- PixelOptionButton ---

PixelOptionButton::PixelOptionButton(const QString& text, Indicator indicator, QWidget* parent)
    : QAbstractButton(parent)
    , m_indicator(indicator)
{
    setText(text);
    setCheckable(true);
    setAutoExclusive(indicator == Indicator::Radio);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setAccessibleName(text);
}

void PixelOptionButton::setMark(Mark mark)
{
    if (mark == m_mark) {
        return;
    }
    m_mark = mark;
    updateGeometry();
    update();
}

void PixelOptionButton::setDetail(const QString& detail)
{
    if (detail == m_detail) {
        return;
    }
    m_detail = detail;
    updateGeometry();
    update();
}

QFont PixelOptionButton::detailFont() const
{
    QFont f = font();
    if (f.pixelSize() > 0) {
        f.setPixelSize(std::max(9, f.pixelSize() - 1));
    } else {
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    }
    return f;
}

QRect PixelOptionButton::textRect(int width) const
{
    const int left = kPadX + kBoxSize + kSpacing;
    const int right = kPadX + (m_mark != Mark::None ? kMarkWidth : 0);
    return QRect(left, kPadY, std::max(20, width - left - right), 0);
}

int PixelOptionButton::heightForWidth(int width) const
{
    const QRect area = textRect(width);
    const QFontMetrics fm(font());
    int h = fm.boundingRect(QRect(0, 0, area.width(), 10000), Qt::TextWordWrap, text()).height();
    if (!m_detail.isEmpty()) {
        const QFontMetrics dm(detailFont());
        h += 2 + dm.boundingRect(QRect(0, 0, area.width(), 10000), Qt::TextWordWrap, m_detail).height();
    }
    return std::max(kBoxSize + 2 * kPadY, h + 2 * kPadY);
}

QSize PixelOptionButton::sizeHint() const
{
    const int w = std::max(160, width() > 0 ? width() : 260);
    return {w, heightForWidth(w)};
}

QSize PixelOptionButton::minimumSizeHint() const
{
    return {120, kBoxSize + 2 * kPadY};
}

void PixelOptionButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void PixelOptionButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void PixelOptionButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect r = rect();
    const bool interactive = isEnabled() && m_mark == Mark::None;

    // Row wash: selected rows use the translucent blue selected-state token, hover the neutral wash.
    QColor wash = Qt::transparent;
    QColor rim = Qt::transparent;
    if (isChecked()) {
        wash = PixelTheme::kSecondaryAccent;
        rim = QColor(0x9B, 0xC6, 0xFF, 40);
    } else if (m_hovered && interactive) {
        wash = QColor(0xFF, 0xFF, 0xFF, 16);
    }
    if (m_mark == Mark::Incorrect) {
        wash = PixelTheme::kDangerBg;
        rim = PixelTheme::kDangerBorder;
    }
    if (wash.alpha() > 0 || rim.alpha() > 0) {
        p.setPen(rim.alpha() > 0 ? QPen(rim, 1.0) : Qt::NoPen);
        p.setBrush(wash);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), PixelTheme::kRadiusButton, PixelTheme::kRadiusButton);
    }

    const QFontMetrics fm(font());
    const QRectF box(kPadX + 0.5, kPadY + std::max(0, (fm.height() - kBoxSize) / 2) + 0.5, kBoxSize - 1.0, kBoxSize - 1.0);
    if (m_indicator == Indicator::Radio) {
        PixelTheme::paintRadioIndicator(p, box, isChecked(), m_hovered && interactive, isDown(), isEnabled() || m_mark != Mark::None);
    } else {
        PixelTheme::paintCheckIndicator(p, box, isChecked(), m_hovered && interactive, isDown(), isEnabled() || m_mark != Mark::None);
    }
    if (hasFocus() && interactive) {
        p.setPen(QPen(PixelTheme::kFocusBorder, 1.2));
        p.setBrush(Qt::NoBrush);
        if (m_indicator == Indicator::Radio) {
            p.drawEllipse(box.adjusted(-2.0, -2.0, 2.0, 2.0));
        } else {
            p.drawRoundedRect(box.adjusted(-2.0, -2.0, 2.0, 2.0), 5.0, 5.0);
        }
    }

    QRect textArea = textRect(r.width());
    const QRect bounds = fm.boundingRect(QRect(textArea.left(), textArea.top(), textArea.width(), 10000),
                                         Qt::TextWordWrap, text());
    p.setFont(font());
    p.setPen(isEnabled() || m_mark != Mark::None ? PixelTheme::kPrimaryText : PixelTheme::kMutedText);
    p.drawText(QRect(textArea.left(), textArea.top(), textArea.width(), bounds.height()),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text());
    if (!m_detail.isEmpty()) {
        p.setFont(detailFont());
        p.setPen(PixelTheme::kSecondaryText);
        p.drawText(QRect(textArea.left(), textArea.top() + bounds.height() + 2, textArea.width(), r.height()),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_detail);
    }

    // Grading mark on the right: check for correct, cross for a wrong pick, hollow check for a missed one.
    if (m_mark != Mark::None) {
        const QPointF c(r.right() - kPadX - kMarkWidth / 2.0 + 2.0, box.center().y());
        p.setBrush(Qt::NoBrush);
        if (m_mark == Mark::Incorrect) {
            p.setPen(QPen(PixelTheme::kDangerText, 1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + QPointF(-4, -4), c + QPointF(4, 4));
            p.drawLine(c + QPointF(-4, 4), c + QPointF(4, -4));
        } else {
            const QColor color = m_mark == Mark::Correct ? PixelTheme::kPrimaryAccentHover : PixelTheme::kMutedText;
            p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath tick;
            tick.moveTo(c + QPointF(-5, 0));
            tick.lineTo(c + QPointF(-1.5, 3.5));
            tick.lineTo(c + QPointF(5, -4));
            p.drawPath(tick);
        }
    }
}
