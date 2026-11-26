#include "alarm/AlarmScheduler.h"
#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"

#include <QElapsedTimer>
#include <QTimeZone>
#include <algorithm>
#include <chrono>
#include <limits>

namespace {

constexpr int kWatchdogIntervalMs = 30 * 1000;
constexpr qint64 kMaxSingleShotMs = 60LL * 60LL * 1000LL; // cap at 1h so clock/DST changes re-sync

qint64 defaultMonotonicMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

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
    m_triggerTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_triggerTimer, &QTimer::timeout, this, &AlarmScheduler::onTimerTimeout);

    m_watchdogTimer.setSingleShot(false);
    m_watchdogTimer.setTimerType(Qt::VeryCoarseTimer);
    m_watchdogTimer.setInterval(kWatchdogIntervalMs);
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

QDateTime AlarmScheduler::computeNextOccurrence(const QTime& localTime, const QDateTime& nowLocal)
{
    if (!localTime.isValid() || !nowLocal.isValid()) {
        return {};
    }

    const QTime cleanTime(localTime.hour(), localTime.minute(), 0, 0);
    QDate targetDate = nowLocal.date();
    QDateTime candidate(targetDate, cleanTime, nowLocal.timeZone());

    // Resolve current-minute or already-passed times strictly to tomorrow so enabling
    // an alarm at the current minute never triggers immediately due to truncated seconds.
    const bool sameMinute = (nowLocal.time().hour() == cleanTime.hour()
                             && nowLocal.time().minute() == cleanTime.minute());
    if (sameMinute || candidate <= nowLocal) {
        candidate = QDateTime(targetDate.addDays(1), cleanTime, nowLocal.timeZone());
    }
    return candidate;
}

QString AlarmScheduler::formatOccurrence(const QDateTime& occurrence,
                                         const QDateTime& nowLocal,
                                         bool use24Hour)
{
    if (!occurrence.isValid()) {
        return QStringLiteral("Not scheduled");
    }

    const QString timeText = use24Hour
        ? occurrence.time().toString(QStringLiteral("HH:mm"))
        : occurrence.time().toString(QStringLiteral("h:mm AP"));

    const qint64 dayDiff = nowLocal.date().daysTo(occurrence.date());
    if (dayDiff == 0) {
        return QStringLiteral("Today at %1").arg(timeText);
    }
    if (dayDiff == 1) {
        return QStringLiteral("Tomorrow at %1").arg(timeText);
    }
    return QStringLiteral("%1 at %2").arg(occurrence.date().toString(QStringLiteral("MMM d")), timeText);
}

void AlarmScheduler::restoreFromSettings()
{
    if (!m_settings) {
        return;
    }

    m_alarmTime = m_settings->hasAlarmConfigured() ? m_settings->alarmTime() : QTime{};
    m_enabled = m_settings->alarmEnabled() && m_alarmTime.isValid();
    m_lastTriggeredOccurrenceUtcMs = m_settings->lastTriggeredOccurrenceUtcMs();

    const QDateTime now = nowLocal();
    const State savedState = static_cast<State>(m_settings->alarmState());
    const qint64 savedSnoozeUtc = m_settings->snoozeTargetUtcMs();
    const qint64 savedNextUtc = m_settings->alarmNextTriggerUtcMs();

    // 1. Recover pending snooze first if present
    if (savedState == State::Snoozed && savedSnoozeUtc > 0) {
        const QDateTime target = QDateTime::fromMSecsSinceEpoch(savedSnoozeUtc).toTimeZone(now.timeZone());
        const qint64 overdueSec = target.secsTo(now);
        if (overdueSec < 0) {
            m_snoozeTarget = target;
            m_snoozeMonotonicDeadlineMs = nowMonotonicMs() + now.msecsTo(target);
            m_nextOccurrence = {};
            setState(State::Snoozed);
            armTimers();
            emit scheduleUpdated();
            return;
        }
        if (overdueSec <= kGraceWindowSeconds && savedSnoozeUtc != m_lastTriggeredOccurrenceUtcMs) {
            m_snoozeTarget = {};
            m_snoozeMonotonicDeadlineMs = 0;
            m_nextOccurrence = {};
            triggerRinging(true, savedSnoozeUtc);
            return;
        }
        // Pending snooze expired past grace window -> expire this one-time alarm cleanly
        expireAndDeactivate();
        return;
    }

    // 2. Recover scheduled one-time alarm
    if (!m_enabled || !m_alarmTime.isValid()) {
        m_nextOccurrence = {};
        m_snoozeTarget = {};
        m_snoozeMonotonicDeadlineMs = 0;
        setState(State::Inactive);
        persistState();
        armTimers();
        emit scheduleUpdated();
        return;
    }

    if (savedNextUtc > 0) {
        const QDateTime savedTarget = QDateTime::fromMSecsSinceEpoch(savedNextUtc).toTimeZone(now.timeZone());
        if (savedTarget > now) {
            m_nextOccurrence = savedTarget;
            setState(State::Scheduled);
            persistState();
            armTimers();
            emit scheduleUpdated();
            return;
        }
        const qint64 overdueSec = savedTarget.secsTo(now);
        if (overdueSec >= 0 && overdueSec <= kGraceWindowSeconds
            && savedNextUtc != m_lastTriggeredOccurrenceUtcMs) {
            m_nextOccurrence = {};
            triggerRinging(false, savedNextUtc);
            return;
        }
        // Stale one-time alarm missed past grace window -> expire cleanly
        expireAndDeactivate();
        return;
    }

    // Fallback if enabled without a persisted timestamp
    m_nextOccurrence = computeNextOccurrence(m_alarmTime, now);
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
        m_nextOccurrence = computeNextOccurrence(m_alarmTime, nowLocal());
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
    if (!m_alarmTime.isValid()) {
        m_enabled = false;
        m_nextOccurrence = {};
        m_snoozeTarget = {};
        m_snoozeMonotonicDeadlineMs = 0;
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
    m_nextOccurrence = {}; // one-time alarm: no future daily occurrence

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

    m_enabled = false;
    m_nextOccurrence = {};
    m_snoozeTarget = {};
    m_snoozeMonotonicDeadlineMs = 0;

    setState(State::Inactive);
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
                triggerRinging(true, snoozeKey);
                return;
            }
            expireAndDeactivate();
            return;
        }
        armTimers();
        return;
    }

    if (m_state == State::Scheduled && m_nextOccurrence.isValid()) {
        // Detect backward system clock jump: if target is now > 24h away, recompute cleanly
        if (now.secsTo(m_nextOccurrence) > 24 * 60 * 60 + 60) {
            m_nextOccurrence = computeNextOccurrence(m_alarmTime, now);
            persistState();
            armTimers();
            emit scheduleUpdated();
            return;
        }

        if (now >= m_nextOccurrence) {
            const qint64 overdueSec = m_nextOccurrence.secsTo(now);
            const qint64 occUtc = m_nextOccurrence.toMSecsSinceEpoch();
            m_nextOccurrence = {};
            if (overdueSec <= kGraceWindowSeconds && occUtc != m_lastTriggeredOccurrenceUtcMs) {
                triggerRinging(false, occUtc);
                return;
            }
            expireAndDeactivate();
            return;
        }
    }

    armTimers();
}

void AlarmScheduler::expireAndDeactivate()
{
    m_enabled = false;
    m_nextOccurrence = {};
    m_snoozeTarget = {};
    m_snoozeMonotonicDeadlineMs = 0;
    setState(State::Inactive);
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

void AlarmScheduler::onTimerTimeout()
{
    checkNow();
}

void AlarmScheduler::onWatchdogTimeout()
{
    checkNow();
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
    m_settings->setAlarmConfigured(m_alarmTime.isValid());
    m_settings->setAlarmEnabled(m_enabled);
    m_settings->setAlarmTime(m_alarmTime);
    m_settings->setAlarmNextTriggerUtcMs(
        m_nextOccurrence.isValid() ? m_nextOccurrence.toMSecsSinceEpoch() : 0);
    m_settings->setAlarmState(static_cast<int>(m_state));
    m_settings->setSnoozeTargetUtcMs(
        m_snoozeTarget.isValid() ? m_snoozeTarget.toMSecsSinceEpoch() : 0);
    m_settings->setLastTriggeredOccurrenceUtcMs(m_lastTriggeredOccurrenceUtcMs);
    m_settings->save();
}

void AlarmScheduler::armTimers()
{
    m_triggerTimer.stop();

    QDateTime target;
    if (m_state == State::Snoozed && m_snoozeTarget.isValid()) {
        target = m_snoozeTarget;
    } else if (m_state == State::Scheduled && m_nextOccurrence.isValid()) {
        target = m_nextOccurrence;
    }

    if (!target.isValid()) {
        m_watchdogTimer.stop();
        return;
    }

    if (!m_watchdogTimer.isActive()) {
        m_watchdogTimer.start();
    }

    const qint64 delayMs = std::max<qint64>(50, nowLocal().msecsTo(target));
    const int cappedMs = static_cast<int>(std::min<qint64>(delayMs, kMaxSingleShotMs));
    m_triggerTimer.start(cappedMs);
}
