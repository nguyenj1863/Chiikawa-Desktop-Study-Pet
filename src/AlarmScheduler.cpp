#include "AlarmScheduler.h"
#include "AppSettings.h"
#include "SettingsDefaults.h"

#include <QTimeZone>
#include <algorithm>
#include <chrono>

namespace {

qint64 defaultMonotonicMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// Cap single QTimer intervals to 12 hours so 32-bit ms overflow never occurs
constexpr qint64 kMaxTimerIntervalMs = 12LL * 60 * 60 * 1000;
constexpr int kWatchdogIntervalMs = 30 * 1000;

} // namespace

AlarmScheduler::AlarmScheduler(AppSettings* settings,
                               WallClockFn wallClock,
                               MonotonicClockFn monotonicClock,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_wallClock(std::move(wallClock))
    , m_monotonicClock(std::move(monotonicClock))
{
    m_triggerTimer.setSingleShot(true);
    m_triggerTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_triggerTimer, &QTimer::timeout, this, &AlarmScheduler::onTimerTimeout);

    m_watchdogTimer.setSingleShot(false);
    m_watchdogTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_watchdogTimer, &QTimer::timeout, this, &AlarmScheduler::onWatchdogTimeout);
}

QDateTime AlarmScheduler::nowLocal() const
{
    return m_wallClock ? m_wallClock() : QDateTime::currentDateTime();
}

qint64 AlarmScheduler::nowMonotonicMs() const
{
    return m_monotonicClock ? m_monotonicClock() : defaultMonotonicMs();
}

QDateTime AlarmScheduler::computeNextDailyOccurrence(const QTime& localTime, const QDateTime& nowLocal)
{
    if (!localTime.isValid() || !nowLocal.isValid()) {
        return {};
    }

    // Normalize to HH:mm:00 in local timezone; if the current minute matches or has passed,
    // schedule tomorrow so enabling an alarm at the current minute never triggers immediately.
    const QTime cleanTime(localTime.hour(), localTime.minute(), 0, 0);
    const QTimeZone tz = nowLocal.timeZone();
    QDate targetDate = nowLocal.date();
    QDateTime candidate(targetDate, cleanTime, tz);

    const QTime nowMinute(nowLocal.time().hour(), nowLocal.time().minute(), 0, 0);
    if (cleanTime <= nowMinute || candidate <= nowLocal) {
        targetDate = targetDate.addDays(1);
        candidate = QDateTime(targetDate, cleanTime, tz);
    }
    return candidate;
}

void AlarmScheduler::restoreFromSettings()
{
    if (!m_settings) {
        return;
    }

    m_alarmTime = m_settings->alarmTime();
    m_enabled = m_settings->alarmEnabled() && m_alarmTime.isValid();
    m_lastTriggeredOccurrenceUtcMs = m_settings->lastTriggeredOccurrenceUtcMs();

    const QDateTime now = nowLocal();

    if (!m_enabled) {
        m_nextOccurrence = {};
        m_snoozeTarget = {};
        m_snoozeMonotonicDeadlineMs = 0;
        setState(State::Inactive);
        armTimers();
        emit scheduleUpdated();
        return;
    }

    const int savedState = m_settings->alarmState();
    const qint64 savedSnoozeUtc = m_settings->snoozeTargetUtcMs();
    const qint64 savedNextUtc = m_settings->alarmNextTriggerUtcMs();

    if (savedNextUtc > 0) {
        m_nextOccurrence = QDateTime::fromMSecsSinceEpoch(savedNextUtc, now.timeZone());
    } else {
        m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
    }

    // Recover pending snooze across app restart
    if (savedState == static_cast<int>(State::Snoozed) && savedSnoozeUtc > 0) {
        const QDateTime snoozeDt = QDateTime::fromMSecsSinceEpoch(savedSnoozeUtc, now.timeZone());
        const qint64 overdueSec = snoozeDt.secsTo(now);
        if (snoozeDt > now) {
            m_snoozeTarget = snoozeDt;
            const qint64 remainingMs = now.msecsTo(snoozeDt);
            m_snoozeMonotonicDeadlineMs = nowMonotonicMs() + remainingMs;
            // Prevent duplicate ringing if snooze overlaps or passes next daily occurrence
            if (m_nextOccurrence.isValid() && m_nextOccurrence <= m_snoozeTarget) {
                m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, m_snoozeTarget);
            }
            setState(State::Snoozed);
            persistState();
            armTimers();
            emit scheduleUpdated();
            return;
        }
        if (overdueSec >= 0 && overdueSec <= kGraceWindowSeconds
            && savedSnoozeUtc != m_lastTriggeredOccurrenceUtcMs) {
            m_snoozeTarget = {};
            m_snoozeMonotonicDeadlineMs = 0;
            m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
            triggerRinging(true, savedSnoozeUtc);
            return;
        }
        // Snooze is stale (> grace window); fall through to regular schedule recovery
        m_snoozeTarget = {};
        m_snoozeMonotonicDeadlineMs = 0;
    }

    // Recover regular daily occurrence across app restart
    if (m_nextOccurrence.isValid() && m_nextOccurrence <= now) {
        const qint64 overdueSec = m_nextOccurrence.secsTo(now);
        const qint64 occUtc = m_nextOccurrence.toMSecsSinceEpoch();
        if (overdueSec <= kGraceWindowSeconds && occUtc != m_lastTriggeredOccurrenceUtcMs) {
            m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
            triggerRinging(false, occUtc);
            return;
        }
        // Missed by more than grace window: advance cleanly to next future occurrence
        m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
    }

    setState(State::Scheduled);
    persistState();
    armTimers();
    emit scheduleUpdated();
}

void AlarmScheduler::configureAlarm(const QTime& localTime, bool enabled)
{
    if (!localTime.isValid()) {
        return;
    }

    m_alarmTime = QTime(localTime.hour(), localTime.minute(), 0, 0);
    m_enabled = enabled;
    m_snoozeTarget = {};
    m_snoozeMonotonicDeadlineMs = 0;

    if (m_enabled) {
        m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, nowLocal());
        setState(State::Scheduled);
    } else {
        m_nextOccurrence = {};
        setState(State::Inactive);
    }

    persistState();
    armTimers();
    emit scheduleUpdated();
}

void AlarmScheduler::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    if (!m_alarmTime.isValid()) {
        m_enabled = false;
        setState(State::Inactive);
        persistState();
        armTimers();
        emit scheduleUpdated();
        return;
    }
    configureAlarm(m_alarmTime, enabled);
}

bool AlarmScheduler::snooze(int minutes)
{
    // Prevent duplicate snoozes from rapid clicks or non-ringing states
    if (m_state != State::Ringing) {
        return false;
    }

    const int clampedMinutes = SettingsDefaults::clampSnoozeMinutes(minutes);
    const QDateTime now = nowLocal();
    const qint64 snoozeMs = static_cast<qint64>(clampedMinutes) * 60 * 1000;

    m_snoozeTarget = now.addMSecs(snoozeMs);
    m_snoozeMonotonicDeadlineMs = nowMonotonicMs() + snoozeMs;

    // Ensure next daily occurrence does not overlap this snooze and cause double ringing
    if (m_enabled && m_alarmTime.isValid()) {
        if (!m_nextOccurrence.isValid() || m_nextOccurrence <= m_snoozeTarget) {
            m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, m_snoozeTarget);
        }
    }

    setState(State::Snoozed);
    persistState();
    armTimers();
    emit alarmSnoozed(clampedMinutes, m_snoozeTarget);
    emit scheduleUpdated();
    return true;
}

void AlarmScheduler::dismiss()
{
    if (m_state != State::Ringing && m_state != State::Snoozed) {
        return;
    }

    m_snoozeTarget = {};
    m_snoozeMonotonicDeadlineMs = 0;

    setState(State::Completed);

    // Preserve recurring daily schedule when alarm remains enabled
    if (m_enabled && m_alarmTime.isValid()) {
        const QDateTime now = nowLocal();
        if (!m_nextOccurrence.isValid() || m_nextOccurrence <= now) {
            m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
        }
        setState(State::Scheduled);
    } else {
        m_nextOccurrence = {};
        setState(State::Inactive);
    }

    persistState();
    armTimers();
    emit alarmDismissed();
    emit scheduleUpdated();
}

void AlarmScheduler::checkNow()
{
    if (!m_enabled && m_state != State::Snoozed) {
        return;
    }
    if (m_state == State::Ringing) {
        return;
    }

    const QDateTime now = nowLocal();
    const qint64 monoNow = nowMonotonicMs();

    if (m_state == State::Snoozed && m_snoozeTarget.isValid()) {
        const bool monoReached = (m_snoozeMonotonicDeadlineMs > 0 && monoNow >= m_snoozeMonotonicDeadlineMs);
        const bool wallReached = (now >= m_snoozeTarget);
        if (monoReached || wallReached) {
            const qint64 snoozeKey = m_snoozeTarget.toMSecsSinceEpoch();
            const qint64 overdueSec = wallReached ? m_snoozeTarget.secsTo(now) : 0;
            m_snoozeTarget = {};
            m_snoozeMonotonicDeadlineMs = 0;

            if (overdueSec <= kGraceWindowSeconds && snoozeKey != m_lastTriggeredOccurrenceUtcMs) {
                if (m_enabled && m_alarmTime.isValid()
                    && (!m_nextOccurrence.isValid() || m_nextOccurrence <= now)) {
                    m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
                }
                triggerRinging(true, snoozeKey);
                return;
            }
            // Overdue beyond grace window after long sleep.
            // If the next daily occurrence itself is due now within grace, fall through to trigger it once.
            if (m_enabled && m_nextOccurrence.isValid() && now >= m_nextOccurrence
                && m_nextOccurrence.secsTo(now) <= kGraceWindowSeconds) {
                setState(State::Scheduled);
            } else {
                advancePastAndSchedule(now);
                return;
            }
        } else {
            armTimers();
            return;
        }
    }

    if (m_state == State::Scheduled && m_nextOccurrence.isValid()) {
        // Detect backward system clock jump: if target is now > 24h away, recompute cleanly
        if (now.secsTo(m_nextOccurrence) > 24 * 60 * 60 + 60) {
            m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
            persistState();
            armTimers();
            emit scheduleUpdated();
            return;
        }

        if (now >= m_nextOccurrence) {
            const qint64 overdueSec = m_nextOccurrence.secsTo(now);
            const qint64 occUtc = m_nextOccurrence.toMSecsSinceEpoch();
            m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
            if (overdueSec <= kGraceWindowSeconds && occUtc != m_lastTriggeredOccurrenceUtcMs) {
                triggerRinging(false, occUtc);
                return;
            }
            // Missed beyond grace window -> skip stale occurrence
            persistState();
            armTimers();
            emit scheduleUpdated();
            return;
        }
    }

    armTimers();
}

void AlarmScheduler::advancePastAndSchedule(const QDateTime& now)
{
    if (m_enabled && m_alarmTime.isValid()) {
        m_nextOccurrence = computeNextDailyOccurrence(m_alarmTime, now);
        setState(State::Scheduled);
    } else {
        m_nextOccurrence = {};
        setState(State::Inactive);
    }
    persistState();
    armTimers();
    emit scheduleUpdated();
}

void AlarmScheduler::triggerRinging(bool fromSnooze, qint64 occurrenceKeyUtcMs)
{
    m_activeOccurrenceUtcMs = occurrenceKeyUtcMs;
    m_lastTriggeredOccurrenceUtcMs = occurrenceKeyUtcMs;
    setState(State::Ringing);
    persistState();
    armTimers();
    emit alarmTriggered(m_alarmTime, fromSnooze);
    emit scheduleUpdated();
}

void AlarmScheduler::setState(State next)
{
    if (m_state == next) {
        return;
    }
    m_state = next;
    emit stateChanged(m_state);
}

void AlarmScheduler::persistState()
{
    if (!m_settings) {
        return;
    }
    if (m_alarmTime.isValid()) {
        m_settings->setAlarmTime(m_alarmTime);
        m_settings->setAlarmConfigured(true);
    }
    m_settings->setAlarmEnabled(m_enabled);
    m_settings->setAlarmState(static_cast<int>(m_state));
    m_settings->setAlarmNextTriggerUtcMs(m_nextOccurrence.isValid() ? m_nextOccurrence.toMSecsSinceEpoch() : 0);
    m_settings->setSnoozeTargetUtcMs(m_snoozeTarget.isValid() ? m_snoozeTarget.toMSecsSinceEpoch() : 0);
    m_settings->setLastTriggeredOccurrenceUtcMs(m_lastTriggeredOccurrenceUtcMs);
    m_settings->save();
}

void AlarmScheduler::armTimers()
{
    m_triggerTimer.stop();

    if (m_state != State::Scheduled && m_state != State::Snoozed) {
        m_watchdogTimer.stop();
        return;
    }

    const QDateTime now = nowLocal();
    qint64 waitMs = kMaxTimerIntervalMs;

    if (m_state == State::Snoozed && m_snoozeTarget.isValid()) {
        const qint64 wallWait = std::max<qint64>(50, now.msecsTo(m_snoozeTarget));
        if (m_snoozeMonotonicDeadlineMs > 0) {
            const qint64 monoWait = std::max<qint64>(50, m_snoozeMonotonicDeadlineMs - nowMonotonicMs());
            waitMs = std::min(wallWait, monoWait);
        } else {
            waitMs = wallWait;
        }
    } else if (m_state == State::Scheduled && m_nextOccurrence.isValid()) {
        waitMs = std::max<qint64>(50, now.msecsTo(m_nextOccurrence));
    }

    m_triggerTimer.start(static_cast<int>(std::min(waitMs, kMaxTimerIntervalMs)));
    if (!m_watchdogTimer.isActive()) {
        m_watchdogTimer.start(kWatchdogIntervalMs);
    }
}

void AlarmScheduler::onTimerTimeout()
{
    checkNow();
}

void AlarmScheduler::onWatchdogTimeout()
{
    checkNow();
}
