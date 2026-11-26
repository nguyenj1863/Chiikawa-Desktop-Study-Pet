#include "timer/StudyTimer.h"

#include <algorithm>

StudyTimer::StudyTimer(QObject* parent)
    : QObject(parent)
{
    m_tickTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_tickTimer, &QTimer::timeout, this, &StudyTimer::onTick);
}

void StudyTimer::configure(std::chrono::milliseconds duration)
{
    m_configured = std::max(std::chrono::milliseconds{1}, duration);
    if (m_state == State::Idle || m_state == State::Completed) {
        m_remainingWhenPaused = m_configured;
        m_lastEmittedRemaining = std::chrono::milliseconds{-1};
        emitRemainingIfChanged();
    }
}

void StudyTimer::configure(int hours, int minutes, int seconds)
{
    hours = std::clamp(hours, 0, 23);
    minutes = std::clamp(minutes, 0, 59);
    seconds = std::clamp(seconds, 0, 59);
    const auto total = std::chrono::hours{hours}
        + std::chrono::minutes{minutes}
        + std::chrono::seconds{seconds};
    configure(std::chrono::duration_cast<std::chrono::milliseconds>(total));
}

bool StudyTimer::isActive() const
{
    return m_state == State::Running || m_state == State::Paused;
}

std::chrono::milliseconds StudyTimer::computeRemaining() const
{
    switch (m_state) {
    case State::Idle:
        return m_configured;
    case State::Paused:
        return std::max(std::chrono::milliseconds{0}, m_remainingWhenPaused);
    case State::Running: {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_deadline) {
            return std::chrono::milliseconds{0};
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - now);
    }
    case State::Completed:
        return std::chrono::milliseconds{0};
    }
    return std::chrono::milliseconds{0};
}

std::chrono::milliseconds StudyTimer::remaining() const
{
    return computeRemaining();
}

int StudyTimer::remainingSeconds() const
{
    const auto ms = remaining();
    return static_cast<int>((ms.count() + 999) / 1000); // ceil to whole seconds for display
}

QString StudyTimer::formatRemaining(std::chrono::milliseconds ms)
{
    if (ms < std::chrono::milliseconds{0}) {
        ms = std::chrono::milliseconds{0};
    }
    const auto totalSec = static_cast<qint64>((ms.count() + 999) / 1000);
    const auto hours = totalSec / 3600;
    const auto minutes = (totalSec % 3600) / 60;
    const auto seconds = totalSec % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar(u'0'))
            .arg(seconds, 2, 10, QChar(u'0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar(u'0'))
        .arg(seconds, 2, 10, QChar(u'0'));
}

void StudyTimer::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

void StudyTimer::emitRemainingIfChanged()
{
    const auto rem = computeRemaining();
    // Only notify when displayed whole-second value would change
    const auto whole = std::chrono::duration_cast<std::chrono::seconds>(rem);
    const auto lastWhole = std::chrono::duration_cast<std::chrono::seconds>(m_lastEmittedRemaining);
    if (whole != lastWhole || m_lastEmittedRemaining < std::chrono::milliseconds{0}) {
        m_lastEmittedRemaining = rem;
        emit remainingChanged(rem);
    }
}

void StudyTimer::armTickTimer()
{
    // Tick frequently enough for accuracy, but not every frame
    m_tickTimer.start(200);
}

void StudyTimer::stopTickTimer()
{
    m_tickTimer.stop();
}

void StudyTimer::start()
{
    if (m_configured <= std::chrono::milliseconds{0}) {
        return;
    }

    m_completionEmitted = false;
    m_remainingWhenPaused = m_configured;
    m_deadline = std::chrono::steady_clock::now() + m_configured;
    setState(State::Running);
    m_lastEmittedRemaining = std::chrono::milliseconds{-1};
    emitRemainingIfChanged();
    armTickTimer();
}

void StudyTimer::pause()
{
    if (m_state != State::Running) {
        return;
    }
    m_remainingWhenPaused = computeRemaining();
    stopTickTimer();
    setState(State::Paused);
    emitRemainingIfChanged();
}

void StudyTimer::resume()
{
    if (m_state != State::Paused) {
        return;
    }
    if (m_remainingWhenPaused <= std::chrono::milliseconds{0}) {
        // Edge case: paused at zero
        setState(State::Completed);
        if (!m_completionEmitted) {
            m_completionEmitted = true;
            emit completed();
        }
        emitRemainingIfChanged();
        return;
    }
    m_deadline = std::chrono::steady_clock::now() + m_remainingWhenPaused;
    setState(State::Running);
    armTickTimer();
    emitRemainingIfChanged();
}

void StudyTimer::reset()
{
    stopTickTimer();
    m_completionEmitted = false;
    m_remainingWhenPaused = m_configured;
    setState(State::Idle);
    m_lastEmittedRemaining = std::chrono::milliseconds{-1};
    emitRemainingIfChanged();
}

void StudyTimer::cancel()
{
    reset();
}

void StudyTimer::onTick()
{
    if (m_state != State::Running) {
        stopTickTimer();
        return;
    }

    const auto rem = computeRemaining();
    emitRemainingIfChanged();

    if (rem <= std::chrono::milliseconds{0}) {
        stopTickTimer();
        setState(State::Completed);
        m_remainingWhenPaused = std::chrono::milliseconds{0};
        m_lastEmittedRemaining = std::chrono::milliseconds{0};
        emit remainingChanged(std::chrono::milliseconds{0});
        if (!m_completionEmitted) {
            m_completionEmitted = true;
            emit completed();
        }
    }
}
