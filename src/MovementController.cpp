#include "MovementController.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <chrono>
#include <cmath>

MovementController::MovementController(QWidget* target, QObject* parent)
    : QObject(parent)
    , m_target(target)
    , m_rng(static_cast<unsigned>(
          std::chrono::steady_clock::now().time_since_epoch().count()))
{
    m_moveTimer.setTimerType(Qt::CoarseTimer);
    m_sessionTimer.setSingleShot(true);

    connect(&m_moveTimer, &QTimer::timeout, this, &MovementController::onMoveTick);
    connect(&m_sessionTimer, &QTimer::timeout, this, &MovementController::onSessionTimeout);

    beginIdle();
}

QRect MovementController::availableGeometryFor(const QWidget* widget)
{
    if (!widget) {
        if (auto* screen = QGuiApplication::primaryScreen()) {
            return screen->availableGeometry();
        }
        return {};
    }

    QScreen* screen = widget->screen();
    if (!screen) {
        const QPoint center = widget->geometry().center();
        screen = QGuiApplication::screenAt(center);
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->availableGeometry() : QRect{};
}

void MovementController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        stopMoveTicks();
        m_sessionTimer.stop();
        m_phase = Phase::Idle;
    } else if (!m_paused) {
        beginIdle();
    }
}

void MovementController::setSpeedMultiplier(double speed)
{
    m_speed = std::clamp(speed, 0.25, 3.0);
}

void MovementController::pause()
{
    m_paused = true;
    stopMoveTicks();
    m_sessionTimer.stop();
}

void MovementController::resume()
{
    if (!m_paused) {
        return;
    }
    m_paused = false;
    if (m_enabled) {
        beginIdle();
    }
}

void MovementController::setFacingLeft(bool left)
{
    if (m_facingLeft == left) {
        return;
    }
    m_facingLeft = left;
    emit facingChanged(m_facingLeft);
}

void MovementController::constrainToScreen()
{
    if (!m_target) {
        return;
    }
    const QRect avail = availableGeometryFor(m_target);
    if (!avail.isValid()) {
        return;
    }

    QRect geo = m_target->frameGeometry();
    int x = geo.x();
    int y = geo.y();
    x = std::clamp(x, avail.left(), avail.right() - geo.width() + 1);
    y = std::clamp(y, avail.top(), avail.bottom() - geo.height() + 1);
    if (QPoint(x, y) != geo.topLeft()) {
        m_target->move(x, y);
        emit moved(QPoint(x, y));
    }
}

void MovementController::stopMoveTicks()
{
    m_moveTimer.stop();
}

void MovementController::beginIdle()
{
    if (!m_enabled || m_paused || !m_target) {
        return;
    }
    m_phase = Phase::Idle;
    stopMoveTicks();
    std::uniform_int_distribution<int> idleMs(3'000, 12'000);
    m_sessionTimer.start(idleMs(m_rng));
}

void MovementController::beginMoving()
{
    if (!m_enabled || m_paused || !m_target) {
        return;
    }
    m_phase = Phase::Moving;

    std::uniform_real_distribution<double> dirRoll(0.0, 1.0);
    const bool goLeft = dirRoll(m_rng) < 0.5;
    setFacingLeft(goLeft);

    // Mostly horizontal; occasional slight vertical drift
    const double base = 1.2 * m_speed;
    std::uniform_real_distribution<double> jitter(-0.35, 0.35);
    m_velocity = QPointF(goLeft ? -base : base, jitter(m_rng) * m_speed);

    std::uniform_int_distribution<int> moveMs(2'000, 8'000);
    m_sessionTimer.start(moveMs(m_rng));

    // ~30–60 FPS while moving
    const int interval = std::clamp(static_cast<int>(33.0 / m_speed), 16, 33);
    m_moveTimer.start(interval);
}

void MovementController::onSessionTimeout()
{
    if (!m_enabled || m_paused) {
        return;
    }
    if (m_phase == Phase::Idle) {
        beginMoving();
    } else {
        beginIdle();
    }
}

void MovementController::onMoveTick()
{
    if (m_phase != Phase::Moving || m_paused || !m_enabled) {
        stopMoveTicks();
        return;
    }
    applyStep();
}

void MovementController::applyStep()
{
    if (!m_target) {
        return;
    }

    const QRect avail = availableGeometryFor(m_target);
    if (!avail.isValid()) {
        return;
    }

    // Prefer the lower ~40% of the available area
    const int bandTop = avail.top() + static_cast<int>(avail.height() * 0.55);
    QRect band(avail.left(), bandTop, avail.width(), avail.bottom() - bandTop);
    if (band.height() < m_target->height()) {
        band = avail;
    }

    QPoint pos = m_target->pos();
    pos.rx() += static_cast<int>(std::lround(m_velocity.x()));
    pos.ry() += static_cast<int>(std::lround(m_velocity.y()));

    const int maxX = band.right() - m_target->width() + 1;
    const int maxY = band.bottom() - m_target->height() + 1;
    const int minX = band.left();
    const int minY = band.top();

    if (pos.x() <= minX) {
        pos.setX(minX);
        m_velocity.setX(std::abs(m_velocity.x()));
        setFacingLeft(false);
    } else if (pos.x() >= maxX) {
        pos.setX(maxX);
        m_velocity.setX(-std::abs(m_velocity.x()));
        setFacingLeft(true);
    }

    if (pos.y() <= minY) {
        pos.setY(minY);
        m_velocity.setY(std::abs(m_velocity.y()));
    } else if (pos.y() >= maxY) {
        pos.setY(maxY);
        m_velocity.setY(-std::abs(m_velocity.y()));
    }

    if (pos != m_target->pos()) {
        m_target->move(pos);
        emit moved(pos);
    }
}
