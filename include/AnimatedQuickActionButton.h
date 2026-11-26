#pragma once

#include "PixelTheme.h"

#include <QAbstractButton>
#include <QColor>
#include <QPixmap>
#include <QPointF>

class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;
class QTimer;

/**
 * Compact translucent quick-action card with a two-phase "pull file out of a
 * folder" hover micro-interaction:
 *
 *   Phase 1 (Pull-Out, ~100 ms): slides upward and outward from the stack while
 *                                brightening its translucent glass surface.
 *   Phase 2 (Grow & Twist, ~185 ms, staggered ~45 ms after Phase 1): enlarges
 *                                to ~1.32x and tilts ±5–7° around its visual center.
 *
 * All transforms are painted inside a fixed 84×84 canvas centered on the 32×32
 * resting tile so widget geometry never resizes and hover enlargement never clips.
 * Menu reveal/dismiss opacity is animated independently via `menuOpacity`.
 */
class AnimatedQuickActionButton : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverScale READ hoverScale WRITE setHoverScale)
    Q_PROPERTY(qreal hoverRotation READ hoverRotation WRITE setHoverRotation)
    Q_PROPERTY(QPointF hoverOffset READ hoverOffset WRITE setHoverOffset)
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal pressProgress READ pressProgress WRITE setPressProgress)
    Q_PROPERTY(qreal menuOpacity READ menuOpacity WRITE setMenuOpacity)

public:
    static constexpr int kWidgetSize = 84;
    static constexpr int kVisualSize = 32;
    static constexpr int kIconSize = 18;

    static constexpr qreal kRestScale = 1.00;
    static constexpr qreal kHoverScale = 1.32;
    static constexpr qreal kPressScale = 1.20;

    static constexpr qreal kPullOutX = 11.0;
    static constexpr qreal kPullOutY = -13.0;

    static constexpr int kPullOutMs = 100;
    static constexpr int kGrowTwistDelayMs = 45;
    static constexpr int kGrowTwistMs = 185;
    static constexpr int kHoverOutMs = 210;
    static constexpr int kPressInMs = 85;
    static constexpr int kPressOutMs = 105;

    AnimatedQuickActionButton(PixelIcon iconKind,
                              const QColor& iconColor,
                              const QColor& hoverIconColor,
                              qreal hoverRotationDegrees,
                              QWidget* parent = nullptr);

    [[nodiscard]] qreal hoverScale() const { return m_hoverScale; }
    void setHoverScale(qreal scale);

    [[nodiscard]] qreal hoverRotation() const { return m_hoverRotation; }
    void setHoverRotation(qreal degrees);

    [[nodiscard]] QPointF hoverOffset() const { return m_hoverOffset; }
    void setHoverOffset(const QPointF& offset);

    [[nodiscard]] qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    [[nodiscard]] qreal pressProgress() const { return m_pressProgress; }
    void setPressProgress(qreal progress);

    [[nodiscard]] qreal menuOpacity() const { return m_menuOpacity; }
    void setMenuOpacity(qreal opacity);

    [[nodiscard]] qreal targetHoverRotation() const { return m_targetHoverRotation; }
    void setTargetHoverRotation(qreal degrees) { m_targetHoverRotation = degrees; }

    /** +1.0 pulls outward to the right (default); -1.0 pulls outward to the left (mirrored). */
    [[nodiscard]] qreal outwardDirection() const { return m_outwardDirection; }
    void setOutwardDirection(qreal direction);

    [[nodiscard]] bool isHovered() const { return m_hovered; }

    /** Programmatic hover/press setters used by QuickActionMenu's centralized hit-testing. */
    void setHovered(bool hovered);
    void setPressedVisual(bool pressed);

    void setReducedMotion(bool reduced);
    [[nodiscard]] bool reducedMotion() const { return m_reducedMotion; }

    /** Immediately stops hover/press animations and restores resting scale=1, rotation=0, offset=(0,0). */
    void resetVisualState();

    /** Returns the 32×32 resting visual rect in this widget's local coordinates. */
    [[nodiscard]] static QRectF restingVisualRectLocal();

    [[nodiscard]] QSize sizeHint() const override { return {kWidgetSize, kWidgetSize}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {kWidgetSize, kWidgetSize}; }

signals:
    void hoveredChanged(bool hovered);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void startHoverInAnimation();
    void startPhase2GrowAndTwist();
    void startHoverOutAnimation();
    void startPressAnimation(bool pressed);
    void rebuildIconPixmaps();

    PixelIcon m_iconKind;
    QColor m_iconColor;
    QColor m_hoverIconColor;
    QPixmap m_restPixmap;
    QPixmap m_hoverPixmap;

    qreal m_targetHoverRotation = 6.0;
    qreal m_outwardDirection = 1.0;
    qreal m_hoverScale = kRestScale;
    qreal m_hoverRotation = 0.0;
    QPointF m_hoverOffset{0.0, 0.0};
    qreal m_hoverProgress = 0.0;
    qreal m_pressProgress = 0.0;
    qreal m_menuOpacity = 0.0;

    bool m_hovered = false;
    bool m_pressedVisual = false;
    bool m_reducedMotion = false;

    QPropertyAnimation* m_scaleAnim = nullptr;
    QPropertyAnimation* m_rotationAnim = nullptr;
    QPropertyAnimation* m_offsetAnim = nullptr;
    QPropertyAnimation* m_hoverProgressAnim = nullptr;
    QPropertyAnimation* m_pressProgressAnim = nullptr;
    QTimer* m_phase2Timer = nullptr;
};
