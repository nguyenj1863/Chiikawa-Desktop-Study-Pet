#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <random>

class QWidget;

/**
 * Autonomous wandering along the lower portion of the active monitor.
 * Uses QTimer intervals — no continuous polling while idle.
 */
class MovementController : public QObject {
    Q_OBJECT

public:
    explicit MovementController(QWidget* target, QObject* parent = nullptr);

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    void setSpeedMultiplier(double speed);
    [[nodiscard]] double speedMultiplier() const { return m_speed; }

    void pause();
    void resume();
    [[nodiscard]] bool isPaused() const { return m_paused; }

    void setFacingLeft(bool left);
    [[nodiscard]] bool facingLeft() const { return m_facingLeft; }

    /** Clamp widget position into the available desktop geometry of its screen. */
    void constrainToScreen();

    [[nodiscard]] static QRect availableGeometryFor(const QWidget* widget);

signals:
    void facingChanged(bool facingLeft);
    void moved(const QPoint& globalPos);

private slots:
    void onMoveTick();
    void onSessionTimeout();

private:
    enum class Phase { Idle, Moving };

    void beginIdle();
    void beginMoving();
    void stopMoveTicks();
    void applyStep();

    QWidget* m_target = nullptr;
    bool m_enabled = true;
    bool m_paused = false;
    bool m_facingLeft = true;
    double m_speed = 1.0;
    Phase m_phase = Phase::Idle;
    QPointF m_velocity;
    QTimer m_moveTimer;
    QTimer m_sessionTimer;
    std::mt19937 m_rng;
};
