#include "app/WindowCoordinator.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <algorithm>
#include <limits>

WindowCoordinator::WindowCoordinator(QWidget* petWindow, QWidget* settingsPanel, QObject* parent)
    : QObject(parent)
    , m_pet(petWindow)
    , m_settings(settingsPanel)
{
}

QRect WindowCoordinator::availableGeometryFor(const QPoint& referencePoint)
{
    if (QScreen* screen = QGuiApplication::screenAt(referencePoint)) {
        return screen->availableGeometry();
    }

    // If the cursor/window center lies in a gap between monitors, pick the closest screen.
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* closest = nullptr;
    qint64 bestDistSq = std::numeric_limits<qint64>::max();
    for (QScreen* screen : screens) {
        const QRect avail = screen->availableGeometry();
        const int cx = std::clamp(referencePoint.x(), avail.left(), avail.right());
        const int cy = std::clamp(referencePoint.y(), avail.top(), avail.bottom());
        const qint64 dx = referencePoint.x() - cx;
        const qint64 dy = referencePoint.y() - cy;
        const qint64 distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            closest = screen;
        }
    }

    if (!closest) {
        closest = QGuiApplication::primaryScreen();
    }
    return closest ? closest->availableGeometry() : QRect{};
}

QPoint WindowCoordinator::clampTopLeft(const QPoint& topLeft, const QSize& size, const QRect& bounds)
{
    if (!bounds.isValid()) {
        return topLeft;
    }
    const int maxX = std::max(bounds.left(), bounds.right() - size.width() + 1);
    const int maxY = std::max(bounds.top(), bounds.bottom() - size.height() + 1);
    return {
        std::clamp(topLeft.x(), bounds.left(), maxX),
        std::clamp(topLeft.y(), bounds.top(), maxY)
    };
}

QPoint WindowCoordinator::computeAttachedSettingsPos(const QRect& petRect,
                                                     const QSize& settingsSize,
                                                     const QRect& bounds,
                                                     AnchorSide& outSide) const
{
    const int rightX = petRect.x() + petRect.width() + kGap;
    const int leftX = petRect.x() - kGap - settingsSize.width();

    int sx = rightX;
    if (!bounds.isValid() || rightX + settingsSize.width() <= bounds.right() + 1) {
        outSide = AnchorSide::Right;
        sx = rightX;
    } else if (leftX >= bounds.left()) {
        outSide = AnchorSide::Left;
        sx = leftX;
    } else {
        // Neither side fits completely without clamping; choose the roomier side.
        const int spaceRight = bounds.right() + 1 - (petRect.x() + petRect.width());
        const int spaceLeft = petRect.x() - bounds.left();
        if (spaceRight >= spaceLeft) {
            outSide = AnchorSide::Right;
            sx = rightX;
        } else {
            outSide = AnchorSide::Left;
            sx = leftX;
        }
    }

    const int sy = petRect.y();
    return clampTopLeft({sx, sy}, settingsSize, bounds);
}

void WindowCoordinator::attachSettings()
{
    if (!m_pet || !m_settings) {
        return;
    }
    const QRect petRect = m_pet->frameGeometry();
    const QRect bounds = availableGeometryFor(petRect.center());
    m_settings->move(computeAttachedSettingsPos(petRect, m_settings->size(), bounds, m_side));
}

void WindowCoordinator::repositionAfterSettingsResize()
{
    if (!m_settings || !m_settings->isVisible()) {
        return;
    }
    const QRect bounds = availableGeometryFor(m_settings->frameGeometry().center());
    const QPoint clamped = clampTopLeft(m_settings->pos(), m_settings->size(), bounds);
    if (clamped != m_settings->pos()) {
        m_settings->move(clamped);
    }
}

void WindowCoordinator::movePetTo(const QPoint& desiredPetTopLeft)
{
    if (!m_pet) {
        return;
    }
    const QSize petSize = m_pet->size();
    const QPoint desiredCenter = desiredPetTopLeft + QPoint(petSize.width() / 2, petSize.height() / 2);
    const QPoint clampedPetPos = clampTopLeft(desiredPetTopLeft, petSize, availableGeometryFor(desiredCenter));

    if (clampedPetPos != m_pet->pos()) {
        m_pet->move(clampedPetPos);
        emit petMoved(clampedPetPos);
    }
}

void WindowCoordinator::moveSettingsTo(const QPoint& desiredSettingsTopLeft)
{
    if (!m_settings) {
        return;
    }
    const QSize settingsSize = m_settings->size();
    const QPoint desiredCenter = desiredSettingsTopLeft + QPoint(settingsSize.width() / 2, settingsSize.height() / 2);
    const QPoint clamped = clampTopLeft(desiredSettingsTopLeft, settingsSize, availableGeometryFor(desiredCenter));
    if (clamped != m_settings->pos()) {
        m_settings->move(clamped);
    }
}
