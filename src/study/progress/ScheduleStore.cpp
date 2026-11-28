#include "study/progress/ScheduleStore.h"

#include <QSqlQuery>
#include <QVariant>

namespace ScheduleStore {

Card load(QSqlDatabase db, const QString& questionId)
{
    Card card;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT state, due_day, due_at, interval_days, ease, reps, lapses, last_review"
                             " FROM card_schedule WHERE question_id = ?"));
    q.addBindValue(questionId);
    if (q.exec() && q.next()) {
        card.state = static_cast<CardState>(q.value(0).toInt());
        card.dueDay = q.value(1).toLongLong();
        card.dueAt = q.value(2).toLongLong();
        card.intervalDays = static_cast<int>(q.value(3).toDouble());
        card.ease = q.value(4).toDouble();
        card.reps = q.value(5).toInt();
        card.lapses = q.value(6).toInt();
        card.lastReview = q.value(7).toLongLong();
    }
    return card;
}

bool save(QSqlDatabase db, const QString& questionId, qint64 setId, const Card& card)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO card_schedule(question_id, set_id, state, due_day, due_at, interval_days, ease, reps, lapses, last_review)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(question_id) DO UPDATE SET set_id = excluded.set_id, state = excluded.state,"
        " due_day = excluded.due_day, due_at = excluded.due_at, interval_days = excluded.interval_days,"
        " ease = excluded.ease, reps = excluded.reps, lapses = excluded.lapses, last_review = excluded.last_review"));
    q.addBindValue(questionId);
    q.addBindValue(setId);
    q.addBindValue(static_cast<int>(card.state));
    q.addBindValue(card.dueDay);
    q.addBindValue(card.dueAt);
    q.addBindValue(card.intervalDays);
    q.addBindValue(card.ease);
    q.addBindValue(card.reps);
    q.addBindValue(card.lapses);
    q.addBindValue(card.lastReview);
    return q.exec();
}

bool replayHistory(QSqlDatabase db)
{
    struct Row {
        qint64 id;
        QString questionId;
        qint64 setId;
        int kind;
        int verdict;
        qint64 answeredAt;
    };
    QList<Row> rows;
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT id, question_id, set_id, kind, verdict, answered_at FROM attempts"
                                   " ORDER BY question_id, answered_at, id"))) {
            return false;
        }
        while (q.next()) {
            rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toLongLong(), q.value(3).toInt(),
                         q.value(4).toInt(), q.value(5).toLongLong()});
        }
    }

    QSqlQuery rate(db);
    rate.prepare(QStringLiteral("UPDATE attempts SET rating = ?, rating_source = 0 WHERE id = ?"));

    Card card;
    QString current;
    qint64 currentSet = 0;
    bool touched = false;
    const auto flush = [&]() {
        return !touched || save(db, current, currentSet, card);
    };

    for (const Row& r : rows) {
        if (r.questionId != current) {
            if (!flush()) {
                return false;
            }
            current = r.questionId;
            currentSet = r.setId;
            card = Card();
            touched = false;
        }
        // Self ratings were stored as GotIt/PartlyGotIt/NeedReview (0/1/2); automatic ones as verdicts (0..3).
        Rating rating = Rating::None;
        if (r.kind == static_cast<int>(AttemptKind::SelfAssessed)) {
            rating = r.verdict == 0 ? Rating::Good : r.verdict == 1 ? Rating::Hard : Rating::Again;
        } else {
            Evaluation e;
            e.verdict = static_cast<Verdict>(r.verdict);
            rating = StudyEval::ratingFor(e);
        }
        if (rating == Rating::None) {
            continue;
        }
        card = ReviewScheduler::schedule(card, rating, QDateTime::fromMSecsSinceEpoch(r.answeredAt));
        touched = true;
        rate.bindValue(0, static_cast<int>(rating));
        rate.bindValue(1, r.id);
        if (!rate.exec()) {
            return false;
        }
    }
    return flush();
}

} // namespace ScheduleStore
