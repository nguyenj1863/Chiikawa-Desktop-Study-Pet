#include "WindowCoordinator.h"

#include <QGuiApplication>
#include <QScopedValueRollback>
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
    if (m_syncing || !m_pet || !m_settings) {
        return;
    }
    QScopedValueRollback<bool> guard(m_syncing, true);

    const QRect petRect = m_pet->frameGeometry();
    const QRect bounds = availableGeometryFor(petRect.center());
    const QPoint settingsPos = computeAttachedSettingsPos(petRect, m_settings->size(), bounds, m_side);

    m_settings->move(settingsPos);
    m_relativeOffset = settingsPos - petRect.topLeft();
}

void WindowCoordinator::realignSettings()
{
    if (!m_settings || !m_settings->isVisible()) {
        return;
    }
    attachSettings();
}

void WindowCoordinator::syncRelativeOffset()
{
    if (m_syncing || !m_pet || !m_settings || !m_settings->isVisible()) {
        return;
    }
    m_relativeOffset = m_settings->pos() - m_pet->frameGeometry().topLeft();
}

void WindowCoordinator::repositionAfterSettingsResize()
{
    if (m_syncing || !m_pet || !m_settings || !m_settings->isVisible()) {
        return;
    }
    QScopedValueRollback<bool> guard(m_syncing, true);

    const QRect petRect = m_pet->frameGeometry();
    const QRect bounds = availableGeometryFor(petRect.center());
    const QPoint clamped = clampTopLeft(m_settings->pos(), m_settings->size(), bounds);
    if (clamped != m_settings->pos()) {
        m_settings->move(clamped);
    }
    m_relativeOffset = m_settings->pos() - petRect.topLeft();
}

void WindowCoordinator::movePetTo(const QPoint& desiredPetTopLeft)
{
    if (m_syncing || !m_pet) {
        return;
    }
    QScopedValueRollback<bool> guard(m_syncing, true);

    const QSize petSize = m_pet->size();
    const QPoint desiredCenter = desiredPetTopLeft + QPoint(petSize.width() / 2, petSize.height() / 2);
    const QRect bounds = availableGeometryFor(desiredCenter);
    const QPoint clampedPetPos = clampTopLeft(desiredPetTopLeft, petSize, bounds);

    const bool petChanged = (clampedPetPos != m_pet->pos());
    if (petChanged) {
        m_pet->move(clampedPetPos);
    }

    if (m_settings && m_settings->isVisible()) {
        const QRect petRect(clampedPetPos, petSize);
        const QSize settingsSize = m_settings->size();
        const QPoint settingsPos = computeAttachedSettingsPos(petRect, settingsSize, bounds, m_side);
        if (settingsPos != m_settings->pos()) {
            m_settings->move(settingsPos);
        }
        m_relativeOffset = settingsPos - clampedPetPos;
    }

    if (petChanged) {
        emit petMoved(clampedPetPos);
    }
}

void WindowCoordinator::moveSettingsTo(const QPoint& desiredSettingsTopLeft)
{
    if (m_syncing || !m_pet || !m_settings) {
        return;
    }
    QScopedValueRollback<bool> guard(m_syncing, true);

    const QSize settingsSize = m_settings->size();
    const QSize petSize = m_pet->size();
    const QPoint desiredCenter = desiredSettingsTopLeft + QPoint(settingsSize.width() / 2, settingsSize.height() / 2);
    const QRect bounds = availableGeometryFor(desiredCenter);

    const QPoint clampedSettingsPos = clampTopLeft(desiredSettingsTopLeft, settingsSize, bounds);

    // Keep the pet in its current relative position, flipping sides gracefully at screen edges.
    int desiredPetX = (m_side == AnchorSide::Right)
        ? (clampedSettingsPos.x() - kGap - petSize.width())
        : (clampedSettingsPos.x() + settingsSize.width() + kGap);

    if (bounds.isValid()) {
        if (m_side == AnchorSide::Right && desiredPetX < bounds.left()) {
            const int flippedX = clampedSettingsPos.x() + settingsSize.width() + kGap;
            if (flippedX + petSize.width() <= bounds.right() + 1) {
                m_side = AnchorSide::Left;
                desiredPetX = flippedX;
            }
        } else if (m_side == AnchorSide::Left && desiredPetX + petSize.width() > bounds.right() + 1) {
            const int flippedX = clampedSettingsPos.x() - kGap - petSize.width();
            if (flippedX >= bounds.left()) {
                m_side = AnchorSide::Right;
                desiredPetX = flippedX;
            }
        }
    }

    const int desiredPetY = clampedSettingsPos.y() - m_relativeOffset.y();
    const QPoint clampedPetPos = clampTopLeft({desiredPetX, desiredPetY}, petSize, bounds);

    if (clampedSettingsPos != m_settings->pos()) {
        m_settings->move(clampedSettingsPos);
    }

    const bool petChanged = (clampedPetPos != m_pet->pos());
    if (petChanged) {
        m_pet->move(clampedPetPos);
    }

    m_relativeOffset = clampedSettingsPos - clampedPetPos;

    if (petChanged) {
        emit petMoved(clampedPetPos);
    }
}
