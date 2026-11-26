#pragma once

#include <QObject>
#include <QTimer>
#include <chrono>

/**
 * Accurate study countdown independent of GUI widgets.
 *
 * State machine: Idle -> Running <-> Paused -> Completed
 *                any non-Completed state may return to Idle via reset/cancel.
 *
 * Timing uses std::chrono::steady_clock so ordinary GUI delays do not drift
 * the remaining duration. After Windows sleep/resume, steady_clock may jump;
 * remaining time is recomputed from the deadline, so the timer advances by
 * the real elapsed wake time (sessions paused across sleep stay paused).
 */
class StudyTimer : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Running,
        Paused,
        Completed
    };
    Q_ENUM(State)

    explicit StudyTimer(QObject* parent = nullptr);

    void configure(std::chrono::milliseconds duration);
    void configure(int hours, int minutes, int seconds);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isRunning() const { return m_state == State::Running; }
    [[nodiscard]] bool isPaused() const { return m_state == State::Paused; }

    [[nodiscard]] std::chrono::milliseconds configuredDuration() const { return m_configured; }
    [[nodiscard]] std::chrono::milliseconds remaining() const;
    [[nodiscard]] int remainingSeconds() const;

    [[nodiscard]] static QString formatRemaining(std::chrono::milliseconds ms);

public slots:
    void start();
    void pause();
    void resume();
    void reset();
    void cancel();

signals:
    void stateChanged(StudyTimer::State state);
    void remainingChanged(std::chrono::milliseconds remaining);
    void completed();

private slots:
    void onTick();

private:
    void setState(State state);
    void emitRemainingIfChanged();
    void armTickTimer();
    void stopTickTimer();
    [[nodiscard]] std::chrono::milliseconds computeRemaining() const;

    State m_state = State::Idle;
    std::chrono::milliseconds m_configured{25 * 60 * 1000};
    std::chrono::milliseconds m_remainingWhenPaused{0};
    std::chrono::steady_clock::time_point m_deadline{};
    std::chrono::milliseconds m_lastEmittedRemaining{-1};
    bool m_completionEmitted = false;
    QTimer m_tickTimer;
};
