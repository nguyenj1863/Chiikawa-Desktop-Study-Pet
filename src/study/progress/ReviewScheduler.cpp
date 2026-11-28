#include "study/progress/ReviewScheduler.h"

#include <algorithm>
#include <QTimeZone>
#include <cmath>

namespace ReviewScheduler {
namespace {

constexpr qint64 kMsPerMinute = 60LL * 1000;
constexpr qint64 kMsPerDay = 24LL * 60 * kMsPerMinute;

int clampInterval(double days, const SchedulerConfig& c)
{
    return std::clamp(static_cast<int>(std::lround(days)), 1, std::max(1, c.maxIntervalDays));
}

/** Spreads intervals of 3+ days by +/- fuzz so reviews of cards learned together drift apart. */
double withFuzz(double days, const SchedulerConfig& c, QRandomGenerator* rng)
{
    if (!rng || days < 3.0 || c.fuzz <= 0.0) {
        return days;
    }
    const double factor = 1.0 + c.fuzz * (rng->generateDouble() * 2.0 - 1.0);
    return days * factor;
}

Card toLearning(Card card, const QDateTime& now, const SchedulerConfig& c)
{
    card.state = CardState::Learning;
    card.dueAt = now.toMSecsSinceEpoch() + c.learningStepMinutes * kMsPerMinute;
    card.dueDay = 0;
    return card;
}

Card toReview(Card card, int intervalDays, const QDateTime& now, const SchedulerConfig& c)
{
    card.state = CardState::Review;
    card.intervalDays = intervalDays;
    card.dueDay = studyDay(now, c.dayStartHour) + intervalDays;
    card.dueAt = dayStartMs(card.dueDay, c.dayStartHour);
    return card;
}

} // namespace

qint64 studyDay(const QDateTime& t, int dayStartHour)
{
    // Shift back by the rollover hour, then count local calendar days.
    return t.toLocalTime().addSecs(-static_cast<qint64>(dayStartHour) * 3600).date().toJulianDay();
}

qint64 dayStartMs(qint64 day, int dayStartHour)
{
    const QDate date = QDate::fromJulianDay(day);
    return QDateTime(date, QTime(dayStartHour, 0), QTimeZone::LocalTime).toMSecsSinceEpoch();
}

bool isDue(const Card& card, const QDateTime& now, int dayStartHour)
{
    switch (card.state) {
    case CardState::New:
        return false;   // new cards are introduced through the daily new-card limit, not "due"
    case CardState::Learning:
        return card.dueAt <= now.toMSecsSinceEpoch();
    case CardState::Review:
        return card.dueDay <= studyDay(now, dayStartHour);
    }
    return false;
}

Card schedule(const Card& before, Rating rating, const QDateTime& now, const SchedulerConfig& c, QRandomGenerator* rng)
{
    if (rating == Rating::None) {
        return before;
    }
    Card card = before;
    card.reps = before.reps + 1;
    card.lastReview = now.toMSecsSinceEpoch();

    switch (before.state) {
    case CardState::New:
    case CardState::Learning: {
        // Learning and relearning: Again and Hard repeat the step; Good graduates; Easy graduates farther out.
        if (rating == Rating::Again || rating == Rating::Hard) {
            return toLearning(card, now, c);
        }
        const int base = std::max(c.graduatingDays, before.intervalDays);   // relearning keeps its post-lapse interval
        const int days = rating == Rating::Good ? base : std::max(c.easyDays, base * 2);
        return toReview(card, clampInterval(days, c), now, c);
    }
    case CardState::Review: {
        const double old = std::max(1, before.intervalDays);
        if (rating == Rating::Again) {
            card.lapses = before.lapses + 1;
            card.ease = std::max(c.minEase, before.ease - c.lapseEasePenalty);
            card.intervalDays = std::max(1, c.graduatingDays);   // the interval after relearning
            return toLearning(card, now, c);
        }
        // Each step up is at least a day longer than the one below it.
        const double hard = std::max(old * c.hardMultiplier, old + 1.0);
        const double good = std::max(old * before.ease, hard + 1.0);
        const double easy = std::max(old * before.ease * c.easyBonus, good + 1.0);
        double next = good;
        if (rating == Rating::Hard) {
            next = hard;
            card.ease = std::max(c.minEase, before.ease - c.hardEasePenalty);
        } else if (rating == Rating::Easy) {
            next = easy;
            card.ease = before.ease + c.easyEaseBonus;
        }
        return toReview(card, clampInterval(withFuzz(next, c, rng), c), now, c);
    }
    }
    return card;
}

QString formatInterval(qint64 ms)
{
    if (ms < kMsPerMinute) {
        return QStringLiteral("<1m");
    }
    if (ms < 60 * kMsPerMinute) {
        return QStringLiteral("%1m").arg(ms / kMsPerMinute);
    }
    if (ms < kMsPerDay) {
        return QStringLiteral("%1h").arg(ms / (60 * kMsPerMinute));
    }
    const double days = static_cast<double>(ms) / kMsPerDay;
    if (days < 30.0) {
        return QStringLiteral("%1d").arg(static_cast<qint64>(std::lround(days)));
    }
    if (days < 365.0) {
        const double months = days / 30.0;
        return months < 10.0 ? QStringLiteral("%1mo").arg(months, 0, 'f', 1) : QStringLiteral("%1mo").arg(std::lround(months));
    }
    return QStringLiteral("%1y").arg(days / 365.0, 0, 'f', 1);
}

Preview preview(const Card& card, const QDateTime& now, const SchedulerConfig& c)
{
    const auto label = [&](Rating rating) {
        const Card next = schedule(card, rating, now, c, nullptr);
        const qint64 target = next.state == CardState::Learning ? next.dueAt
                                                                : dayStartMs(next.dueDay, c.dayStartHour);
        // A review due "tomorrow" shows as 1d even though the day boundary is hours away.
        const qint64 ahead = next.state == CardState::Review
            ? static_cast<qint64>(next.intervalDays) * kMsPerDay
            : std::max<qint64>(0, target - now.toMSecsSinceEpoch());
        return formatInterval(ahead);
    };
    return {label(Rating::Again), label(Rating::Hard), label(Rating::Good), label(Rating::Easy)};
}

} // namespace ReviewScheduler
