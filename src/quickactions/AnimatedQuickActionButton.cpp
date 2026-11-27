#include "quickactions/AnimatedQuickActionButton.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTimer>
#include <algorithm>
#include <cmath>

int AnimatedQuickActionButton::clampScalePercent(int percent)
{
    return std::clamp(percent,
                      SettingsDefaults::kMinQuickActionIconScalePercent,
                      SettingsDefaults::kMaxQuickActionIconScalePercent);
}

int AnimatedQuickActionButton::visualSizeForScale(int percent)
{
    const qreal factor = clampScalePercent(percent) / 100.0;
    return std::max(24, qRound(kBaseVisualSize * factor));
}

int AnimatedQuickActionButton::iconSizeForScale(int percent)
{
    const qreal factor = clampScalePercent(percent) / 100.0;
    return std::max(14, qRound(kBaseIconSize * factor));
}

int AnimatedQuickActionButton::widgetSizeForScale(int percent)
{
    const qreal factor = clampScalePercent(percent) / 100.0;
    const int vis = visualSizeForScale(percent);
    const qreal pullScale = std::clamp(factor, 0.85, 1.30);
    const qreal pullReach = std::max(std::abs(kBasePullOutX), std::abs(kBasePullOutY)) * pullScale;
    // Account for 1.32x hover scale, ±6.5° rotation, and OutBack overshoot (~1.16x)
    const qreal halfExtent = vis * 0.5 * kHoverScale * 1.18 + pullReach + 5.0;
    const int size = static_cast<int>(std::ceil(halfExtent)) * 2;
    return std::max(80, size);
}

AnimatedQuickActionButton::AnimatedQuickActionButton(PixelIcon iconKind,
                                                     const QColor& iconColor,
                                                     const QColor& hoverIconColor,
                                                     qreal hoverRotationDegrees,
                                                     QWidget* parent)
    : QAbstractButton(parent)
    , m_iconKind(iconKind)
    , m_iconColor(iconColor)
    , m_hoverIconColor(hoverIconColor)
    , m_targetHoverRotation(hoverRotationDegrees)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);

    m_scaleAnim = new QPropertyAnimation(this, "hoverScale", this);
    m_rotationAnim = new QPropertyAnimation(this, "hoverRotation", this);
    m_offsetAnim = new QPropertyAnimation(this, "hoverOffset", this);
    m_hoverProgressAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_pressProgressAnim = new QPropertyAnimation(this, "pressProgress", this);

    m_phase2Timer = new QTimer(this);
    m_phase2Timer->setSingleShot(true);
    connect(m_phase2Timer, &QTimer::timeout, this, &AnimatedQuickActionButton::startPhase2GrowAndTwist);

    setScalePercent(SettingsDefaults::kQuickActionIconScalePercent);
}

AnimatedQuickActionButton::AnimatedQuickActionButton(const QIcon& customIcon,
                                                     qreal hoverRotationDegrees,
                                                     QWidget* parent)
    : AnimatedQuickActionButton(PixelIcon::GenericApp,
                                PixelTheme::kPrimaryText,
                                QColor(Qt::white),
                                hoverRotationDegrees,
                                parent)
{
    setCustomIcon(customIcon);
}

void AnimatedQuickActionButton::setCustomIcon(const QIcon& customIcon)
{
    m_customIcon = customIcon;
    m_useCustomIcon = !customIcon.isNull();
    m_cachedPixmapIconSize = 0;
    rebuildIconPixmaps();
    update();
}

void AnimatedQuickActionButton::setScalePercent(int percent)
{
    const int clamped = clampScalePercent(percent);
    const qreal factor = clamped / 100.0;
    const qreal pullScale = std::clamp(factor, 0.85, 1.30);

    m_scalePercent = clamped;
    m_visualSize = visualSizeForScale(clamped);
    m_iconSize = iconSizeForScale(clamped);
    m_widgetSize = widgetSizeForScale(clamped);
    m_pullOutX = kBasePullOutX * pullScale;
    m_pullOutY = kBasePullOutY * pullScale;

    if (size() != QSize(m_widgetSize, m_widgetSize)) {
        setFixedSize(m_widgetSize, m_widgetSize);
    }
    if (m_cachedPixmapIconSize != m_iconSize) {
        rebuildIconPixmaps();
    }
    update();
}

QRectF AnimatedQuickActionButton::restingVisualRectLocal() const
{
    const qreal margin = (m_widgetSize - m_visualSize) * 0.5;
    return QRectF(margin, margin, m_visualSize, m_visualSize);
}

void AnimatedQuickActionButton::rebuildIconPixmaps()
{
    m_cachedPixmapIconSize = m_iconSize;
    if (m_useCustomIcon && !m_customIcon.isNull()) {
        m_restPixmap = m_customIcon.pixmap(QSize(m_iconSize, m_iconSize), 3.0);
        m_hoverPixmap = m_restPixmap;
        return;
    }
    const QIcon restIcon = PixelTheme::icon(m_iconKind, m_iconColor, m_iconSize);
    const QIcon hoverIcon = PixelTheme::icon(m_iconKind, m_hoverIconColor, m_iconSize);
    // Request 3x DPR pixmaps so 1.32x hover enlargement + rotation stays crisp at any scale.
    m_restPixmap = restIcon.pixmap(QSize(m_iconSize, m_iconSize), 3.0);
    m_hoverPixmap = hoverIcon.pixmap(QSize(m_iconSize, m_iconSize), 3.0);
}

void AnimatedQuickActionButton::setHoverScale(qreal scale)
{
    if (std::abs(m_hoverScale - scale) < 1e-4) {
        return;
    }
    m_hoverScale = scale;
    update();
}

void AnimatedQuickActionButton::setHoverRotation(qreal degrees)
{
    if (std::abs(m_hoverRotation - degrees) < 1e-4) {
        return;
    }
    m_hoverRotation = degrees;
    update();
}

void AnimatedQuickActionButton::setHoverOffset(const QPointF& offset)
{
    if (std::abs(m_hoverOffset.x() - offset.x()) < 1e-3 &&
        std::abs(m_hoverOffset.y() - offset.y()) < 1e-3) {
        return;
    }
    m_hoverOffset = offset;
    update();
}

void AnimatedQuickActionButton::setHoverProgress(qreal progress)
{
    const qreal clamped = std::clamp(progress, 0.0, 1.0);
    if (std::abs(m_hoverProgress - clamped) < 1e-4) {
        return;
    }
    m_hoverProgress = clamped;
    update();
}

void AnimatedQuickActionButton::setPressProgress(qreal progress)
{
    const qreal clamped = std::clamp(progress, 0.0, 1.0);
    if (std::abs(m_pressProgress - clamped) < 1e-4) {
        return;
    }
    m_pressProgress = clamped;
    update();
}

void AnimatedQuickActionButton::setMenuOpacity(qreal opacity)
{
    const qreal clamped = std::clamp(opacity, 0.0, 1.0);
    if (std::abs(m_menuOpacity - clamped) < 1e-4) {
        return;
    }
    m_menuOpacity = clamped;
    update();
}

void AnimatedQuickActionButton::setOutwardDirection(qreal direction)
{
    m_outwardDirection = (direction < 0.0) ? -1.0 : 1.0;
}

void AnimatedQuickActionButton::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    if (m_reducedMotion) {
        m_phase2Timer->stop();
        m_scaleAnim->stop();
        m_rotationAnim->stop();
        m_offsetAnim->stop();
        setHoverScale(kRestScale);
        setHoverRotation(0.0);
        setHoverOffset(QPointF(0.0, 0.0));
    }
}

void AnimatedQuickActionButton::resetVisualState()
{
    m_phase2Timer->stop();
    m_scaleAnim->stop();
    m_rotationAnim->stop();
    m_offsetAnim->stop();
    m_hoverProgressAnim->stop();
    m_pressProgressAnim->stop();

    const bool wasHovered = m_hovered;
    m_hovered = false;
    m_pressedVisual = false;
    m_hoverScale = kRestScale;
    m_hoverRotation = 0.0;
    m_hoverOffset = QPointF(0.0, 0.0);
    m_hoverProgress = 0.0;
    m_pressProgress = 0.0;
    update();

    if (wasHovered) {
        emit hoveredChanged(false);
    }
}

void AnimatedQuickActionButton::setHovered(bool hovered)
{
    if (m_hovered == hovered) {
        return;
    }
    m_hovered = hovered;
    if (!m_hovered) {
        m_pressedVisual = false;
        startHoverOutAnimation();
    } else {
        startHoverInAnimation();
    }
    emit hoveredChanged(m_hovered);
}

void AnimatedQuickActionButton::setPressedVisual(bool pressed)
{
    if (m_pressedVisual == pressed) {
        return;
    }
    m_pressedVisual = pressed;
    startPressAnimation(pressed);
}

void AnimatedQuickActionButton::startHoverInAnimation()
{
    m_phase2Timer->stop();

    if (m_reducedMotion) {
        m_scaleAnim->stop();
        m_rotationAnim->stop();
        m_offsetAnim->stop();
        m_hoverProgressAnim->stop();
        setHoverScale(kRestScale);
        setHoverRotation(0.0);
        setHoverOffset(QPointF(0.0, 0.0));
        setHoverProgress(1.0);
        return;
    }

    // Phase 1: Pull out upward & outward from the overlapping arc + brighten glass
    const QPointF targetOffset(m_pullOutX * m_outwardDirection, m_pullOutY);
    m_offsetAnim->stop();
    m_offsetAnim->setDuration(kPullOutMs);
    m_offsetAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_offsetAnim->setStartValue(m_hoverOffset);
    m_offsetAnim->setEndValue(targetOffset);
    m_offsetAnim->start();

    m_hoverProgressAnim->stop();
    m_hoverProgressAnim->setDuration(kPullOutMs + 20);
    m_hoverProgressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_hoverProgressAnim->setStartValue(m_hoverProgress);
    m_hoverProgressAnim->setEndValue(1.0);
    m_hoverProgressAnim->start();

    m_scaleAnim->stop();
    m_rotationAnim->stop();

    // Phase 2: Enlarge to 1.32x and twist shortly after pull-out begins
    m_phase2Timer->start(kGrowTwistDelayMs);
}

void AnimatedQuickActionButton::startPhase2GrowAndTwist()
{
    if (!m_hovered || m_reducedMotion) {
        return;
    }

    QEasingCurve springCurve(QEasingCurve::OutBack);
    springCurve.setOvershoot(0.85);

    const qreal targetScale = m_pressedVisual ? kPressScale : kHoverScale;
    const qreal targetRot = m_targetHoverRotation * m_outwardDirection;

    m_scaleAnim->stop();
    m_scaleAnim->setDuration(kGrowTwistMs);
    m_scaleAnim->setEasingCurve(springCurve);
    m_scaleAnim->setStartValue(m_hoverScale);
    m_scaleAnim->setEndValue(targetScale);
    m_scaleAnim->start();

    m_rotationAnim->stop();
    m_rotationAnim->setDuration(kGrowTwistMs);
    m_rotationAnim->setEasingCurve(springCurve);
    m_rotationAnim->setStartValue(m_hoverRotation);
    m_rotationAnim->setEndValue(targetRot);
    m_rotationAnim->start();
}

void AnimatedQuickActionButton::startHoverOutAnimation()
{
    m_phase2Timer->stop();

    if (m_reducedMotion) {
        m_scaleAnim->stop();
        m_rotationAnim->stop();
        m_offsetAnim->stop();
        m_hoverProgressAnim->stop();
        m_pressProgressAnim->stop();
        setHoverScale(kRestScale);
        setHoverRotation(0.0);
        setHoverOffset(QPointF(0.0, 0.0));
        setHoverProgress(0.0);
        setPressProgress(0.0);
        return;
    }

    m_rotationAnim->stop();
    m_rotationAnim->setDuration(kHoverOutMs);
    m_rotationAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_rotationAnim->setStartValue(m_hoverRotation);
    m_rotationAnim->setEndValue(0.0);
    m_rotationAnim->start();

    m_scaleAnim->stop();
    m_scaleAnim->setDuration(kHoverOutMs);
    m_scaleAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_scaleAnim->setStartValue(m_hoverScale);
    m_scaleAnim->setEndValue(kRestScale);
    m_scaleAnim->start();

    m_offsetAnim->stop();
    m_offsetAnim->setDuration(kHoverOutMs);
    m_offsetAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_offsetAnim->setStartValue(m_hoverOffset);
    m_offsetAnim->setEndValue(QPointF(0.0, 0.0));
    m_offsetAnim->start();

    m_hoverProgressAnim->stop();
    m_hoverProgressAnim->setDuration(kHoverOutMs);
    m_hoverProgressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_hoverProgressAnim->setStartValue(m_hoverProgress);
    m_hoverProgressAnim->setEndValue(0.0);
    m_hoverProgressAnim->start();

    m_pressProgressAnim->stop();
    m_pressProgressAnim->setDuration(kPressOutMs);
    m_pressProgressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_pressProgressAnim->setStartValue(m_pressProgress);
    m_pressProgressAnim->setEndValue(0.0);
    m_pressProgressAnim->start();
}

void AnimatedQuickActionButton::startPressAnimation(bool pressed)
{
    if (m_reducedMotion) {
        setPressProgress(pressed ? 1.0 : 0.0);
        return;
    }

    const qreal targetScale = pressed
        ? kPressScale
        : (m_hovered ? kHoverScale : kRestScale);

    m_scaleAnim->stop();
    m_scaleAnim->setDuration(pressed ? kPressInMs : kPressOutMs);
    m_scaleAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_scaleAnim->setStartValue(m_hoverScale);
    m_scaleAnim->setEndValue(targetScale);
    m_scaleAnim->start();

    m_pressProgressAnim->stop();
    m_pressProgressAnim->setDuration(pressed ? kPressInMs : kPressOutMs);
    m_pressProgressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_pressProgressAnim->setStartValue(m_pressProgress);
    m_pressProgressAnim->setEndValue(pressed ? 1.0 : 0.0);
    m_pressProgressAnim->start();
}

void AnimatedQuickActionButton::enterEvent(QEnterEvent* event)
{
    setHovered(true);
    QAbstractButton::enterEvent(event);
}

void AnimatedQuickActionButton::leaveEvent(QEvent* event)
{
    setHovered(false);
    QAbstractButton::leaveEvent(event);
}

void AnimatedQuickActionButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setPressedVisual(true);
    }
    QAbstractButton::mousePressEvent(event);
}

void AnimatedQuickActionButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setPressedVisual(false);
    }
    QAbstractButton::mouseReleaseEvent(event);
}

void AnimatedQuickActionButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_menuOpacity <= 0.001) {
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(m_menuOpacity);

    const QPointF center = QRectF(rect()).center() + m_hoverOffset;
    p.save();
    p.translate(center);
    p.rotate(m_hoverRotation);
    p.scale(m_hoverScale, m_hoverScale);
    p.translate(-center);

    const qreal halfVis = m_visualSize * 0.5;
    const QRectF surface(center.x() - halfVis, center.y() - halfVis, m_visualSize, m_visualSize);
    const qreal cornerRadius = std::clamp(m_visualSize * 0.27, 7.5, 13.0);

    QPainterPath surfacePath;
    surfacePath.addRoundedRect(surface, cornerRadius, cornerRadius);

    // Subtle elevation shadow when pulled out of the stack
    if (m_hoverProgress > 0.02) {
        const QRectF shadowRect = surface.translated(0.0, 2.5 * m_hoverProgress);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, cornerRadius, cornerRadius);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, qRound(58.0 * m_hoverProgress)));
        p.drawPath(shadowPath);
    }

    // Smoked-black translucent glass base
    const int baseAlpha = std::clamp(qRound(162.0 + 34.0 * m_hoverProgress + 16.0 * m_pressProgress), 0, 224);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(PixelTheme::kBackground.red(),
                      PixelTheme::kBackground.green(),
                      PixelTheme::kBackground.blue(),
                      baseAlpha));
    p.drawPath(surfacePath);

    // Restrained translucent glass highlight on hover & press
    const int highlightAlpha = std::clamp(qRound(24.0 * m_hoverProgress + 20.0 * m_pressProgress), 0, 60);
    if (highlightAlpha > 0) {
        p.setBrush(QColor(255, 255, 255, highlightAlpha));
        p.drawPath(surfacePath);
    }

    // Restrained translucent hairline border
    const int borderAlpha = std::clamp(qRound(22.0 + 28.0 * m_hoverProgress + 16.0 * m_pressProgress), 0, 74);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, borderAlpha), 1.0));
    p.drawPath(surfacePath);

    // Crisp foreground icon centered on the button surface
    const QRectF iconRect(center.x() - m_iconSize * 0.5,
                          center.y() - m_iconSize * 0.5,
                          m_iconSize,
                          m_iconSize);
    if (!m_restPixmap.isNull()) {
        if (m_hoverProgress <= 0.01 || m_hoverPixmap.isNull()) {
            p.drawPixmap(iconRect, m_restPixmap, QRectF(m_restPixmap.rect()));
        } else if (m_hoverProgress >= 0.99) {
            p.drawPixmap(iconRect, m_hoverPixmap, QRectF(m_hoverPixmap.rect()));
        } else {
            p.save();
            p.setOpacity(m_menuOpacity * (1.0 - m_hoverProgress));
            p.drawPixmap(iconRect, m_restPixmap, QRectF(m_restPixmap.rect()));
            p.setOpacity(m_menuOpacity * m_hoverProgress);
            p.drawPixmap(iconRect, m_hoverPixmap, QRectF(m_hoverPixmap.rect()));
            p.restore();
        }
    }

    p.restore();
}
