#pragma once

#include <QDateTime>
#include <QObject>
#include <QTime>
#include <QTimer>
#include <functional>

class AppSettings;

/**
 * Centralized local wall-clock daily alarm and snooze scheduler.
 *
 * State machine:
 *   Inactive -> Scheduled -> Ringing -> Completed (re-schedules next day)
 *                                   \-> Snoozed -> Ringing
 *
 * Missed-occurrence policy (Windows sleep/resume or app restart):
 *   - If an alarm or pending snooze is overdue by <= kGraceWindowSeconds (15 min),
 *     it rings immediately once so the user does not miss a timely reminder.
 *   - If overdue by > 15 min, the stale occurrence is skipped and the scheduler
 *     advances cleanly to the next future daily occurrence.
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

    /** Loads saved alarm/snooze state and recovers cleanly after restart. */
    void restoreFromSettings();

    /** Configures and persists the single daily alarm; never triggers immediately for current minute. */
    void configureAlarm(const QTime& localTime, bool enabled);

    /** Enables or disables the configured alarm without overwriting its saved time. */
    void setEnabled(bool enabled);

    /** Snoozes a currently ringing occurrence for the given duration (5, 10, or 15 min). */
    bool snooze(int minutes);

    /** Dismisses a currently ringing or snoozed occurrence; preserves future daily recurrence. */
    void dismiss();

    /** Evaluates current wall/monotonic clocks (called by timers, sleep/resume, and tests). */
    void checkNow();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    [[nodiscard]] bool isConfigured() const { return m_alarmTime.isValid(); }
    [[nodiscard]] QTime alarmTime() const { return m_alarmTime; }
    [[nodiscard]] QDateTime nextTriggerTime() const { return m_nextOccurrence; }
    [[nodiscard]] QDateTime snoozeTargetTime() const { return m_snoozeTarget; }

    /** Computes the next strictly future local occurrence for the given HH:mm. */
    [[nodiscard]] static QDateTime computeNextDailyOccurrence(const QTime& localTime,
                                                              const QDateTime& nowLocal);

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
    void advancePastAndSchedule(const QDateTime& now);

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
