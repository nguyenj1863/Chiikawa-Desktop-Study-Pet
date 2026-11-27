#pragma once

#include <QObject>
#include <QRandomGenerator>
#include <QTimer>
#include <chrono>
#include <functional>

/**
 * Fires questionDue() at randomized intervals while running.
 * When the canAsk predicate reports the app is busy (dialogs, dragging,
 * notifications), the check-in is deferred rather than dropped.
 */
class QuestionScheduler : public QObject {
    Q_OBJECT

public:
    static constexpr double kMinJitter = 0.6;
    static constexpr double kMaxJitter = 1.4;
    static constexpr std::chrono::milliseconds kDefaultBusyRetry{60'000};

    explicit QuestionScheduler(QObject* parent = nullptr);

    void setBaseInterval(std::chrono::milliseconds interval);
    [[nodiscard]] std::chrono::milliseconds baseInterval() const { return m_base; }

    void setBusyRetry(std::chrono::milliseconds retry) { m_busyRetry = retry; }
    void setCanAskPredicate(std::function<bool()> canAsk) { m_canAsk = std::move(canAsk); }
    void setSeed(quint32 seed) { m_rng.seed(seed); }

    /** Starts (or restarts) the countdown to the next check-in. */
    void start();
    void stop();
    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] int remainingMs() const { return m_timer.remainingTime(); }

    /** Randomized delay in [base * kMinJitter, base * kMaxJitter]. */
    [[nodiscard]] std::chrono::milliseconds nextDelay();

signals:
    void questionDue();

private:
    void onTimeout();

    QTimer m_timer;
    QRandomGenerator m_rng;
    std::chrono::milliseconds m_base{15 * 60 * 1000};
    std::chrono::milliseconds m_busyRetry = kDefaultBusyRetry;
    std::function<bool()> m_canAsk;
    bool m_running = false;
};
