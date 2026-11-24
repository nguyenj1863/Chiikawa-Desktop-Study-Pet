#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTime>
#include <QTimer>
#include <functional>

class AppSettings;

/**
 * Centralized local wall-clock one-time alarm and snooze scheduler.
 *
 * State machine:
 *   Inactive -> Scheduled -> Ringing -> Inactive (on Dismiss)
 *                                   \-> Snoozed -> Ringing
 *
 * Missed-occurrence policy (Windows sleep/resume or app restart):
 *   - If a scheduled one-time alarm or pending snooze is overdue by <= kGraceWindowSeconds (15 min),
 *     it rings once so the user does not miss a timely reminder.
 *   - If overdue by > 15 min, the stale one-time occurrence is expired and marked Inactive
 *     without ringing or creating a recurring alarm.
 *
 * Clock / DST / timezone handling:
 *   - Event-driven QTimer armed to the exact next deadline plus a lightweight 30 s
 *     watchdog that detects system-clock jumps, DST transitions, and sleep/resume.
 *   - Monotonic elapsed-time checks protect active snoozes against backward clock jumps,
 *     while wall-clock UTC timestamps persist across application restarts.
 */
class AlarmScheduler : public QObject {
    Q_OBJECT

public:
    enum class State {
        Inactive = 0,
        Scheduled = 1,
        Ringing = 2,
        Snoozed = 3,
        Completed = 4
    };
    Q_ENUM(State)

    using WallClockFn = std::function<QDateTime()>;
    using MonotonicClockFn = std::function<qint64()>;

    static constexpr int kGraceWindowSeconds = 15 * 60; // 15 minutes

    explicit AlarmScheduler(AppSettings* settings,
                            WallClockFn wallClock = {},
                            MonotonicClockFn monotonicClock = {},
                            QObject* parent = nullptr);

    /** Loads saved one-time alarm/snooze state and recovers cleanly after restart. */
    void restoreFromSettings();

    /** Configures and persists the one-time alarm; never triggers immediately for current minute. */
    void configureAlarm(const QTime& localTime, bool enabled);

    /** Enables or disables the configured one-time alarm without overwriting its saved time. */
    void setEnabled(bool enabled);

    /** Snoozes a currently ringing one-time occurrence for 5, 10, or 15 minutes. */
    bool snooze(int minutes);

    /** Dismisses a currently ringing or snoozed occurrence and marks the one-time alarm Inactive. */
    void dismiss();

    /** Evaluates current wall/monotonic clocks (called by timers, sleep/resume, and tests). */
    void checkNow();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    [[nodiscard]] bool isConfigured() const { return m_alarmTime.isValid(); }
    [[nodiscard]] QTime alarmTime() const { return m_alarmTime; }
    [[nodiscard]] QDateTime nextTriggerTime() const { return m_nextOccurrence; }
    [[nodiscard]] QDateTime snoozeTargetTime() const { return m_snoozeTarget; }

    /** Computes the next strictly future local occurrence (today if future, else tomorrow). */
    [[nodiscard]] static QDateTime computeNextOccurrence(const QTime& localTime,
                                                         const QDateTime& nowLocal);

    /** Formats a scheduled occurrence as e.g. "Today at 5:30 PM" or "Tomorrow at 08:00". */
    [[nodiscard]] static QString formatOccurrence(const QDateTime& occurrence,
                                                  const QDateTime& nowLocal,
                                                  bool use24Hour);

signals:
    void stateChanged(AlarmScheduler::State state);
    void alarmTriggered(const QTime& scheduledTime, bool fromSnooze);
    void alarmSnoozed(int minutes, const QDateTime& wakeTime);
    void alarmDismissed();
    void scheduleUpdated();

private slots:
    void onTimerTimeout();
    void onWatchdogTimeout();

private:
    [[nodiscard]] QDateTime nowLocal() const;
    [[nodiscard]] qint64 nowMonotonicMs() const;

    void setState(State next);
    void persistState();
    void armTimers();
    void triggerRinging(bool fromSnooze, qint64 occurrenceKeyUtcMs);
    void expireAndDeactivate();

    AppSettings* m_settings = nullptr;
    WallClockFn m_wallClock;
    MonotonicClockFn m_monotonicClock;

    State m_state = State::Inactive;
    bool m_enabled = false;
    QTime m_alarmTime;
    QDateTime m_nextOccurrence;
    QDateTime m_snoozeTarget;
    qint64 m_snoozeMonotonicDeadlineMs = 0;
    qint64 m_lastTriggeredOccurrenceUtcMs = 0;
    qint64 m_activeOccurrenceUtcMs = 0;

    QTimer m_triggerTimer;
    QTimer m_watchdogTimer;
};
