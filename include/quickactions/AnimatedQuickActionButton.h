#pragma once

#include "ui/PixelTheme.h"
#include "app/SettingsDefaults.h"

#include <QAbstractButton>
#include <QColor>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;
class QTimer;

/**
 * Compact translucent quick-action card with a two-phase "pull file out of a
 * folder" hover micro-interaction and dynamic base-size scaling (80%–150%,
 * default 110%).
 *
 *   Phase 1 (Pull-Out, ~100 ms): slides upward and outward from the stack while
 *                                brightening its translucent glass surface.
 *   Phase 2 (Grow & Twist, ~185 ms, staggered ~45 ms after Phase 1): enlarges
 *                                to ~1.32x and tilts ±5.5–6.5° around its visual center.
 *
 * All dimensions are derived directly from immutable base constants and the
 * configured scale percentage so repeated slider adjustments never compound.
 * Transforms paint inside a generously padded fixed canvas so widget geometry
 * never shifts during hover and enlargement never clips.
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
    static constexpr int kBaseVisualSize = 34;
    static constexpr int kBaseIconSize = 19;

    static constexpr qreal kRestScale = 1.00;
    static constexpr qreal kHoverScale = 1.32;
    static constexpr qreal kPressScale = 1.20;

    static constexpr qreal kBasePullOutX = 11.0;
    static constexpr qreal kBasePullOutY = -13.0;

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

    AnimatedQuickActionButton(const QIcon& customIcon,
                              qreal hoverRotationDegrees,
                              QWidget* parent = nullptr);

    void setCustomIcon(const QIcon& customIcon);

    [[nodiscard]] static int clampScalePercent(int percent);
    [[nodiscard]] static int visualSizeForScale(int percent);
    [[nodiscard]] static int iconSizeForScale(int percent);
    [[nodiscard]] static int widgetSizeForScale(int percent);

    void setScalePercent(int percent);
    [[nodiscard]] int scalePercent() const { return m_scalePercent; }
    [[nodiscard]] int visualSize() const { return m_visualSize; }
    [[nodiscard]] int iconSizePx() const { return m_iconSize; }
    [[nodiscard]] int widgetSizePx() const { return m_widgetSize; }

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

    [[nodiscard]] QRectF restingVisualRectLocal() const;

    [[nodiscard]] QSize sizeHint() const override { return {m_widgetSize, m_widgetSize}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {m_widgetSize, m_widgetSize}; }

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
    QIcon m_customIcon;
    bool m_useCustomIcon = false;
    QPixmap m_restPixmap;
    QPixmap m_hoverPixmap;
    int m_cachedPixmapIconSize = 0;

    int m_scalePercent = SettingsDefaults::kQuickActionIconScalePercent;
    int m_visualSize = 37;
    int m_iconSize = 21;
    int m_widgetSize = 96;
    qreal m_pullOutX = kBasePullOutX;
    qreal m_pullOutY = kBasePullOutY;

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
