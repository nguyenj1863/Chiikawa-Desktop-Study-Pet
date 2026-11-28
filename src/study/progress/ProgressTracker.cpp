#include "study/progress/ProgressTracker.h"

#include "study/generation/QuestionValidator.h"
#include "study/progress/ScheduleStore.h"
#include "study/StudyDatabase.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <cmath>

QString ProgressTracker::topicKey(qint64 documentId, const QString& topic)
{
    return QString::number(documentId) + QLatin1Char('|') + QuestionValidator::normalizeForMatch(topic);
}

bool ProgressTracker::record(const Question& question, const Evaluation& evaluation, bool reschedule)
{
    if (!m_db) {
        return false;
    }
    const Rating rating = StudyEval::ratingFor(evaluation);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlDatabase db = m_db->connection();
    db.transaction();

    QSqlQuery attempt(db);
    attempt.prepare(QStringLiteral("INSERT INTO attempts(question_id, document_id, set_id, type, kind, verdict, score, answered_at,"
                                   " rating, rating_source) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    attempt.addBindValue(question.id);
    attempt.addBindValue(question.documentId);
    attempt.addBindValue(question.setId);
    attempt.addBindValue(static_cast<int>(question.type));
    attempt.addBindValue(static_cast<int>(evaluation.kind));
    // Self-assessed attempts store the rating; automatic ones store the verdict.
    attempt.addBindValue(evaluation.kind == AttemptKind::SelfAssessed ? static_cast<int>(evaluation.selfRating)
                                                                       : static_cast<int>(evaluation.verdict));
    attempt.addBindValue(evaluation.score);
    attempt.addBindValue(now);
    attempt.addBindValue(rating == Rating::None ? QVariant(QMetaType::fromType<int>()) : QVariant(static_cast<int>(rating)));
    attempt.addBindValue(static_cast<int>(evaluation.ratingSource));
    if (!attempt.exec()) {
        db.rollback();
        return false;
    }

    const QString topic = QuestionValidator::normalizeForMatch(question.topic.isEmpty() ? question.prompt : question.topic);
    QSqlQuery existing(db);
    existing.prepare(QStringLiteral("SELECT seen, correct, partial, incorrect, needs_review FROM concept_stats"
                                    " WHERE document_id = ? AND concept = ?"));
    existing.addBindValue(question.documentId);
    existing.addBindValue(topic);
    int seen = 0, correct = 0, partial = 0, incorrect = 0;
    bool needsReview = false;
    if (existing.exec() && existing.next()) {
        seen = existing.value(0).toInt();
        correct = existing.value(1).toInt();
        partial = existing.value(2).toInt();
        incorrect = existing.value(3).toInt();
        needsReview = existing.value(4).toBool();
    }
    existing.finish();
    ++seen;
    switch (evaluation.verdict) {
    case Verdict::Correct:
        ++correct;
        needsReview = false;
        break;
    case Verdict::Partial:
        ++partial;
        needsReview = true;
        break;
    case Verdict::Incorrect:
        ++incorrect;
        needsReview = true;
        break;
    case Verdict::NeedsReview:
        // Uncertain grading neither clears nor sets the review flag.
        break;
    }
    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral("INSERT INTO concept_stats(document_id, set_id, concept, seen, correct,"
                                  " partial, incorrect, last_seen, needs_review) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
                                  " ON CONFLICT(document_id, concept) DO UPDATE SET set_id = excluded.set_id,"
                                  " seen = excluded.seen, correct = excluded.correct, partial = excluded.partial,"
                                  " incorrect = excluded.incorrect, last_seen = excluded.last_seen,"
                                  " needs_review = excluded.needs_review"));
    upsert.addBindValue(question.documentId);
    upsert.addBindValue(question.setId);
    upsert.addBindValue(topic);
    upsert.addBindValue(seen);
    upsert.addBindValue(correct);
    upsert.addBindValue(partial);
    upsert.addBindValue(incorrect);
    upsert.addBindValue(now);
    upsert.addBindValue(needsReview ? 1 : 0);
    if (!upsert.exec()) {
        db.rollback();
        return false;
    }
    if (reschedule && rating != Rating::None) {
        const Card next = ReviewScheduler::schedule(ScheduleStore::load(db, question.id), rating,
                                                    QDateTime::fromMSecsSinceEpoch(now), {}, QRandomGenerator::global());
        if (!ScheduleStore::save(db, question.id, question.setId, next)) {
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

Card ProgressTracker::card(const QString& questionId)
{
    return m_db ? ScheduleStore::load(m_db->connection(), questionId) : Card();
}

StudyProgress ProgressTracker::progress(qint64 setId)
{
    StudyProgress p;
    if (!m_db) {
        return p;
    }
    const QString where = setId > 0 ? QStringLiteral(" WHERE set_id = %1").arg(setId) : QString();
    QSqlQuery q(m_db->connection());
    q.exec(QStringLiteral("SELECT kind, verdict, type FROM attempts") + where);
    while (q.next()) {
        const auto kind = static_cast<AttemptKind>(q.value(0).toInt());
        const int verdict = q.value(1).toInt();
        const int type = q.value(2).toInt();
        if (kind == AttemptKind::SelfAssessed) {
            ++p.selfAssessed;
            switch (static_cast<SelfRating>(verdict)) {
            case SelfRating::GotIt:
                ++p.selfGotIt;
                break;
            case SelfRating::PartlyGotIt:
                ++p.selfPartly;
                break;
            case SelfRating::NeedReview:
                ++p.selfNeedReview;
                break;
            }
            continue;
        }
        ++p.attempted;
        FormatProgress& f = p.byFormat[type];
        ++f.attempted;
        switch (static_cast<Verdict>(verdict)) {
        case Verdict::Correct:
            ++p.correct;
            ++f.correct;
            break;
        case Verdict::Partial:
            ++p.partial;
            ++f.partial;
            break;
        case Verdict::Incorrect:
            ++p.incorrect;
            break;
        case Verdict::NeedsReview:
            ++p.needsReview;
            break;
        }
    }
    QSqlQuery r(m_db->connection());
    r.exec(QStringLiteral("SELECT concept FROM concept_stats WHERE needs_review = 1")
           + (setId > 0 ? QStringLiteral(" AND set_id = %1").arg(setId) : QString())
           + QStringLiteral(" ORDER BY last_seen DESC"));
    while (r.next()) {
        const QString topic = r.value(0).toString();
        if (!p.reviewTopics.contains(topic)) {
            p.reviewTopics.append(topic);
        }
    }
    // Cards due now: review cards whose study day has come, and learning cards past their time.
    QSqlQuery due(m_db->connection());
    due.prepare(QStringLiteral("SELECT COUNT(*) FROM card_schedule WHERE ((state = 2 AND due_day <= ?) OR (state = 1 AND due_at <= ?))")
                + (setId > 0 ? QStringLiteral(" AND set_id = %1").arg(setId) : QString()));
    const QDateTime now = QDateTime::currentDateTime();
    due.addBindValue(ReviewScheduler::studyDay(now));
    due.addBindValue(now.toMSecsSinceEpoch());
    if (due.exec() && due.next()) {
        p.dueCount = due.value(0).toInt();
    }
    return p;
}

QSet<QString> ProgressTracker::reviewKeys(qint64 setId)
{
    QSet<QString> keys;
    if (!m_db) {
        return keys;
    }
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT document_id, concept FROM concept_stats WHERE needs_review = 1 AND set_id = ?"));
    q.addBindValue(setId);
    if (q.exec()) {
        while (q.next()) {
            keys.insert(q.value(0).toString() + QLatin1Char('|') + q.value(1).toString());
        }
    }
    return keys;
}

bool ProgressTracker::reset(qint64 setId)
{
    if (!m_db) {
        return false;
    }
    const QString where = setId > 0 ? QStringLiteral(" WHERE set_id = %1").arg(setId) : QString();
    QSqlQuery q(m_db->connection());
    return q.exec(QStringLiteral("DELETE FROM attempts") + where) && q.exec(QStringLiteral("DELETE FROM concept_stats") + where)
        && q.exec(QStringLiteral("DELETE FROM card_schedule") + where);
}
