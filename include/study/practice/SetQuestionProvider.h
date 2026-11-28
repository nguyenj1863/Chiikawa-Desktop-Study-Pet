#pragma once

#include "study/practice/QuestionProvider.h"
#include "study/progress/ReviewScheduler.h"

#include <QDateTime>
#include <QList>
#include <QRandomGenerator>
#include <QVector>
#include <functional>

class ProgressTracker;
class StudyDatabase;

/**
 * Feeds a practice session: every question of one study set (from the documents selected for
 * study, in the enabled formats) in a fixed order for the whole session.
 * Order: questions due for review first, then new ones, then the rest; each group is shuffled.
 */
class SetQuestionProvider : public QuestionProvider {
public:
    SetQuestionProvider(StudyDatabase* db, ProgressTracker* progress);

    /** Builds the queue for a session; returns the number of questions. */
    int start(qint64 setId, const QList<QuestionType>& types);
    /** How many questions a session over this set would contain, without starting one. */
    [[nodiscard]] int countFor(qint64 setId, const QList<QuestionType>& types) const;

    [[nodiscard]] int total() const { return m_total; }
    [[nodiscard]] int remaining() const { return static_cast<int>(m_queue.size()); }
    /** Questions already handed out in this session. */
    [[nodiscard]] int asked() const { return m_total - remaining(); }
    void setSeed(quint32 seed) { m_rng.seed(seed); }

    [[nodiscard]] QString id() const override { return QStringLiteral("practice"); }
    /** Next queued question; recentIds are ignored (a session visits every question once). */
    [[nodiscard]] std::optional<Question> nextQuestion(const QStringList& recentIds) override;

    /** Pure ordering used by start(): due, then new, then the rest, each shuffled with rng. */
    [[nodiscard]] static QVector<Question> order(const QVector<Question>& questions,
                                                 const std::function<Card(const QString&)>& cardFor,
                                                 const QDateTime& now,
                                                 QRandomGenerator& rng);

private:
    StudyDatabase* m_db = nullptr;
    ProgressTracker* m_progress = nullptr;
    QList<Question> m_queue;
    int m_total = 0;
    QRandomGenerator m_rng;
};
