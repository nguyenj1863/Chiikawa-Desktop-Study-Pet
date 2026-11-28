#pragma once

#include "study/evaluation/Evaluation.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QString>

/** Scheduling state of one question. Values are persisted; append only. */
enum class CardState {
    New = 0,
    Learning = 1,   // being learned or relearned; reviewed again within the session
    Review = 2      // graduated; scheduled in whole days
};

/** Spaced-repetition state for one question. A question without a stored row is a new card. */
struct Card {
    CardState state = CardState::New;
    qint64 dueDay = 0;          // study day a Review card is due (see ReviewScheduler::studyDay)
    qint64 dueAt = 0;           // epoch ms a Learning card is due again; Review cards: start of their due day
    int intervalDays = 0;       // current review interval (the post-relearning interval while Learning)
    double ease = 2.5;
    int reps = 0;
    int lapses = 0;
    qint64 lastReview = 0;      // epoch ms

    [[nodiscard]] bool operator==(const Card& other) const = default;
};

/** Tunable numbers. Defaults follow Anki's classic scheduler (SM-2 family). */
struct SchedulerConfig {
    double startEase = 2.5;
    double minEase = 1.3;
    double hardMultiplier = 1.2;
    double easyBonus = 1.3;
    double lapseEasePenalty = 0.20;
    double hardEasePenalty = 0.15;
    double easyEaseBonus = 0.15;
    int learningStepMinutes = 10;
    int graduatingDays = 1;
    int easyDays = 4;
    int maxIntervalDays = 365;
    double fuzz = 0.05;          // +/- share applied to intervals of 3 days or more (needs an RNG)
    int dayStartHour = 4;        // the study day rolls over at 4 am local time
};

/**
 * Pure scheduling logic: no database, UI, or clock access. All times are passed in so tests
 * are deterministic, and randomness (interval fuzz) is only applied when an RNG is given.
 */
namespace ReviewScheduler {

/** Number of the study day containing t: local days since the epoch, rolling over at dayStartHour. */
[[nodiscard]] qint64 studyDay(const QDateTime& t, int dayStartHour = 4);
/** Epoch ms of the moment a study day starts (dayStartHour local time). */
[[nodiscard]] qint64 dayStartMs(qint64 studyDay, int dayStartHour = 4);

/** Applies one rating and returns the card's next state. Rating::None leaves the card unchanged. */
[[nodiscard]] Card schedule(const Card& card, Rating rating, const QDateTime& now,
                            const SchedulerConfig& config = {}, QRandomGenerator* fuzzRng = nullptr);

/** True when the card should be reviewed now: a due Review card, or a Learning card past its due time. */
[[nodiscard]] bool isDue(const Card& card, const QDateTime& now, int dayStartHour = 4);

/** Next-interval labels shown on the four rating buttons ("10m", "3d", "2.5mo"). */
struct Preview {
    QString again;
    QString hard;
    QString good;
    QString easy;
};
[[nodiscard]] Preview preview(const Card& card, const QDateTime& now, const SchedulerConfig& config = {});

/** "10m", "2h", "1d", "3d", "1.5mo", "1y": how far in the future a card comes back. */
[[nodiscard]] QString formatInterval(qint64 millisecondsAhead);

} // namespace ReviewScheduler
