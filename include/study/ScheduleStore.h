#pragma once

#include "study/ReviewScheduler.h"

#include <QSqlDatabase>
#include <QString>

/** Reads and writes the card_schedule table, and seeds it from answer history. */
namespace ScheduleStore {

/** Stored state of a question, or a new card if it has no row. */
[[nodiscard]] Card load(QSqlDatabase db, const QString& questionId);
[[nodiscard]] bool save(QSqlDatabase db, const QString& questionId, qint64 setId, const Card& card);

/**
 * One-time upgrade step: replays every stored attempt (oldest first, per question) through
 * the scheduler to build card_schedule, and fills in the rating of old attempts as Auto.
 * Attempts are never modified apart from that rating.
 */
[[nodiscard]] bool replayHistory(QSqlDatabase db);

} // namespace ScheduleStore
