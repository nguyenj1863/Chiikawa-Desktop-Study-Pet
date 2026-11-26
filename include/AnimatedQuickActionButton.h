#pragma once

#include "PixelTheme.h"

#include <QAbstractButton>
#include <QColor>
#include <QPixmap>

class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QPropertyAnimation;

/**
 * Compact translucent quick-action button with centered scale-and-twist hover
 * micro-interactions and stable click geometry.
 *
 * Visual hover transforms (scale, rotation, glass highlight) are painted inside
 * a fixed-size widget with internal padding so layout geometry and hitboxes
 * never shift during animation. Menu reveal/dismiss opacity is animated
 * independently via the `menuOpacity` property.
 */
class AnimatedQuickActionButton : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverScale READ hoverScale WRITE setHoverScale)
    Q_PROPERTY(qreal hoverRotation READ hoverRotation WRITE setHoverRotation)
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal pressProgress READ pressProgress WRITE setPressProgress)
    Q_PROPERTY(qreal menuOpacity READ menuOpacity WRITE setMenuOpacity)

public:
    static constexpr int kWidgetSize = 42;
    static constexpr int kVisualSize = 32;
    static constexpr int kIconSize = 18;

    static constexpr qreal kRestScale = 1.00;
    static constexpr qreal kHoverScale = 1.11;
    static constexpr qreal kPressScale = 1.04;

    static constexpr int kHoverInMs = 180;
    static constexpr int kHoverOutMs = 165;
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

    [[nodiscard]] qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    [[nodiscard]] qreal pressProgress() const { return m_pressProgress; }
    void setPressProgress(qreal progress);

    [[nodiscard]] qreal menuOpacity() const { return m_menuOpacity; }
    void setMenuOpacity(qreal opacity);

    [[nodiscard]] qreal targetHoverRotation() const { return m_targetHoverRotation; }
    void setTargetHoverRotation(qreal degrees) { m_targetHoverRotation = degrees; }

    [[nodiscard]] bool isHovered() const { return m_hovered; }

    void setReducedMotion(bool reduced);
    [[nodiscard]] bool reducedMotion() const { return m_reducedMotion; }

    /** Immediately stops hover/press animations and restores resting scale=1, rotation=0. */
    void resetVisualState();

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
    void animateToState(bool hovered, bool pressed);
    void rebuildIconPixmaps();

    PixelIcon m_iconKind;
    QColor m_iconColor;
    QColor m_hoverIconColor;
    QPixmap m_restPixmap;
    QPixmap m_hoverPixmap;

    qreal m_targetHoverRotation = 6.0;
    qreal m_hoverScale = kRestScale;
    qreal m_hoverRotation = 0.0;
    qreal m_hoverProgress = 0.0;
    qreal m_pressProgress = 0.0;
    qreal m_menuOpacity = 0.0;

    bool m_hovered = false;
    bool m_pressedVisual = false;
    bool m_reducedMotion = false;

    QPropertyAnimation* m_scaleAnim = nullptr;
    QPropertyAnimation* m_rotationAnim = nullptr;
    QPropertyAnimation* m_hoverProgressAnim = nullptr;
    QPropertyAnimation* m_pressProgressAnim = nullptr;
};
