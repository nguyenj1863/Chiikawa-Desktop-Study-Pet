#include "AnimatedQuickActionButton.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <algorithm>
#include <cmath>

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
    setFixedSize(kWidgetSize, kWidgetSize);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);

    m_scaleAnim = new QPropertyAnimation(this, "hoverScale", this);
    m_rotationAnim = new QPropertyAnimation(this, "hoverRotation", this);
    m_hoverProgressAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_pressProgressAnim = new QPropertyAnimation(this, "pressProgress", this);

    rebuildIconPixmaps();
}

void AnimatedQuickActionButton::rebuildIconPixmaps()
{
    const QIcon restIcon = PixelTheme::icon(m_iconKind, m_iconColor, kIconSize);
    const QIcon hoverIcon = PixelTheme::icon(m_iconKind, m_hoverIconColor, kIconSize);
    // Request 2x pixmaps so rotated/scaled painting stays razor-sharp on standard & HiDPI screens.
    m_restPixmap = restIcon.pixmap(QSize(kIconSize, kIconSize), 2.0);
    m_hoverPixmap = hoverIcon.pixmap(QSize(kIconSize, kIconSize), 2.0);
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

void AnimatedQuickActionButton::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    if (m_reducedMotion) {
        m_scaleAnim->stop();
        m_rotationAnim->stop();
        setHoverScale(kRestScale);
        setHoverRotation(0.0);
    }
}

void AnimatedQuickActionButton::resetVisualState()
{
    m_scaleAnim->stop();
    m_rotationAnim->stop();
    m_hoverProgressAnim->stop();
    m_pressProgressAnim->stop();

    const bool wasHovered = m_hovered;
    m_hovered = false;
    m_pressedVisual = false;
    m_hoverScale = kRestScale;
    m_hoverRotation = 0.0;
    m_hoverProgress = 0.0;
    m_pressProgress = 0.0;
    update();

    if (wasHovered) {
        emit hoveredChanged(false);
    }
}

void AnimatedQuickActionButton::animateToState(bool hovered, bool pressed)
{
    const qreal targetHoverProg = hovered ? 1.0 : 0.0;
    const qreal targetPressProg = pressed ? 1.0 : 0.0;

    if (m_reducedMotion) {
        m_scaleAnim->stop();
        m_rotationAnim->stop();
        m_hoverProgressAnim->stop();
        m_pressProgressAnim->stop();
        setHoverScale(kRestScale);
        setHoverRotation(0.0);
        setHoverProgress(targetHoverProg);
        setPressProgress(targetPressProg);
        return;
    }

    const qreal targetScale = pressed
        ? kPressScale
        : (hovered ? kHoverScale : kRestScale);
    const qreal targetRot = hovered ? m_targetHoverRotation : 0.0;

    const int duration = pressed
        ? kPressInMs
        : (hovered ? kHoverInMs : kHoverOutMs);

    QEasingCurve motionCurve(QEasingCurve::OutCubic);
    if (hovered && !pressed) {
        motionCurve.setType(QEasingCurve::OutBack);
        motionCurve.setOvershoot(0.95);
    }

    m_scaleAnim->stop();
    m_scaleAnim->setDuration(pressed ? kPressInMs : (m_pressedVisual ? kPressOutMs : duration));
    m_scaleAnim->setEasingCurve(motionCurve);
    m_scaleAnim->setStartValue(m_hoverScale);
    m_scaleAnim->setEndValue(targetScale);
    m_scaleAnim->start();

    m_rotationAnim->stop();
    m_rotationAnim->setDuration(duration);
    m_rotationAnim->setEasingCurve(motionCurve);
    m_rotationAnim->setStartValue(m_hoverRotation);
    m_rotationAnim->setEndValue(targetRot);
    m_rotationAnim->start();

    m_hoverProgressAnim->stop();
    m_hoverProgressAnim->setDuration(duration);
    m_hoverProgressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_hoverProgressAnim->setStartValue(m_hoverProgress);
    m_hoverProgressAnim->setEndValue(targetHoverProg);
    m_hoverProgressAnim->start();

    m_pressProgressAnim->stop();
    m_pressProgressAnim->setDuration(pressed ? kPressInMs : kPressOutMs);
    m_pressProgressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_pressProgressAnim->setStartValue(m_pressProgress);
    m_pressProgressAnim->setEndValue(targetPressProg);
    m_pressProgressAnim->start();
}

void AnimatedQuickActionButton::enterEvent(QEnterEvent* event)
{
    if (!m_hovered) {
        m_hovered = true;
        animateToState(true, m_pressedVisual);
        emit hoveredChanged(true);
    }
    QAbstractButton::enterEvent(event);
}

void AnimatedQuickActionButton::leaveEvent(QEvent* event)
{
    if (m_hovered) {
        m_hovered = false;
        m_pressedVisual = false;
        animateToState(false, false);
        emit hoveredChanged(false);
    }
    QAbstractButton::leaveEvent(event);
}

void AnimatedQuickActionButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressedVisual = true;
        animateToState(m_hovered, true);
    }
    QAbstractButton::mousePressEvent(event);
}

void AnimatedQuickActionButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressedVisual) {
        m_pressedVisual = false;
        animateToState(m_hovered, false);
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

    const QPointF center = QRectF(rect()).center();
    p.save();
    p.translate(center);
    p.rotate(m_hoverRotation);
    p.scale(m_hoverScale, m_hoverScale);
    p.translate(-center);

    const qreal halfVis = kVisualSize * 0.5;
    const QRectF surface(center.x() - halfVis, center.y() - halfVis, kVisualSize, kVisualSize);
    constexpr qreal kCornerRadius = 9.0;

    QPainterPath surfacePath;
    surfacePath.addRoundedRect(surface, kCornerRadius, kCornerRadius);

    // Smoked-black translucent glass base (matches PixelTheme compact glass tier)
    const int baseAlpha = std::clamp(qRound(158.0 + 18.0 * m_hoverProgress + 16.0 * m_pressProgress), 0, 220);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(PixelTheme::kBackground.red(),
                      PixelTheme::kBackground.green(),
                      PixelTheme::kBackground.blue(),
                      baseAlpha));
    p.drawPath(surfacePath);

    // Subtle translucent glass highlight on hover (~rgba(255,255,255,0.08)) and press
    const int highlightAlpha = std::clamp(qRound(21.0 * m_hoverProgress + 22.0 * m_pressProgress), 0, 64);
    if (highlightAlpha > 0) {
        p.setBrush(QColor(255, 255, 255, highlightAlpha));
        p.drawPath(surfacePath);
    }

    // Restrained translucent hairline border (never a harsh white rim)
    const int borderAlpha = std::clamp(qRound(14.0 + 24.0 * m_hoverProgress + 18.0 * m_pressProgress), 0, 72);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, borderAlpha), 1.0));
    p.drawPath(surfacePath);

    // Crisp foreground icon centered on the button surface
    const QRectF iconRect(center.x() - kIconSize * 0.5,
                          center.y() - kIconSize * 0.5,
                          kIconSize,
                          kIconSize);
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
