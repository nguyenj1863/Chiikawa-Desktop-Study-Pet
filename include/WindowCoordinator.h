#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>

class QScreen;
class QWidget;

/**
 * Coordinates positioning and synchronized bidirectional dragging between
 * the desktop pet window and its floating settings panel.
 *
 * Uses a re-entrancy guard so neither window can recursively trigger
 * position updates on the other.
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

    void attachSettings();
    void realignSettings();

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
    QPoint m_relativeOffset{0, 0}; // settings.topLeft() - pet.topLeft()
    bool m_syncing = false;
};
