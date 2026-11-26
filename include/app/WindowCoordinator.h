#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>

class QScreen;
class QWidget;

/**
 * Positions the floating settings panel beside the desktop pet when it opens
 * and keeps both windows inside the available monitor bounds.
 *
 * After opening, the pet and the settings panel move independently: dragging
 * or autonomously moving one never repositions the other.
 */
class WindowCoordinator : public QObject {
    Q_OBJECT

public:
    static constexpr int kGap = 12;

    enum class AnchorSide {
        Right,
        Left
    };

    WindowCoordinator(QWidget* petWindow, QWidget* settingsPanel, QObject* parent = nullptr);

    /** Places the settings panel beside the pet's current location. */
    void attachSettings();

    /** Keeps the settings panel on-screen after its height changes. */
    void repositionAfterSettingsResize();

    void movePetTo(const QPoint& desiredPetTopLeft);
    void moveSettingsTo(const QPoint& desiredSettingsTopLeft);

    [[nodiscard]] AnchorSide anchorSide() const { return m_side; }

    [[nodiscard]] static QRect availableGeometryFor(const QPoint& referencePoint);
    [[nodiscard]] static QPoint clampTopLeft(const QPoint& topLeft, const QSize& size, const QRect& bounds);

signals:
    void petMoved(const QPoint& newPetPos);

private:
    [[nodiscard]] QPoint computeAttachedSettingsPos(const QRect& petRect,
                                                    const QSize& settingsSize,
                                                    const QRect& bounds,
                                                    AnchorSide& outSide) const;

    QPointer<QWidget> m_pet;
    QPointer<QWidget> m_settings;
    AnchorSide m_side = AnchorSide::Right;
};
