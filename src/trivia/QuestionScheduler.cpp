#include "trivia/QuestionScheduler.h"

#include <algorithm>

QuestionScheduler::QuestionScheduler(QObject* parent)
    : QObject(parent)
    , m_rng(QRandomGenerator::global()->generate())
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &QuestionScheduler::onTimeout);
}

void QuestionScheduler::setBaseInterval(std::chrono::milliseconds interval)
{
    m_base = std::max(interval, std::chrono::milliseconds(1));
    if (m_running) {
        start();
    }
}

std::chrono::milliseconds QuestionScheduler::nextDelay()
{
    const double factor = kMinJitter + (kMaxJitter - kMinJitter) * m_rng.generateDouble();
    const auto ms = static_cast<long long>(static_cast<double>(m_base.count()) * factor);
    return std::chrono::milliseconds(std::max(1LL, ms));
}

void QuestionScheduler::start()
{
    m_running = true;
    m_timer.start(nextDelay());
}

void QuestionScheduler::stop()
{
    m_running = false;
    m_timer.stop();
}

void QuestionScheduler::onTimeout()
{
    if (!m_running) {
        return;
    }
    if (m_canAsk && !m_canAsk()) {
        m_timer.start(m_busyRetry);
        return;
    }
    // Schedule the following check-in before emitting so handlers may stop() safely.
    m_timer.start(nextDelay());
    emit questionDue();
}
