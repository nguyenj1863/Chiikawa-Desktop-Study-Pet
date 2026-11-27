#include "study/SetQuestionProvider.h"

#include "study/ProgressTracker.h"
#include "study/StudyDatabase.h"

#include <algorithm>

SetQuestionProvider::SetQuestionProvider(StudyDatabase* db, ProgressTracker* progress)
    : m_db(db)
    , m_progress(progress)
    , m_rng(QRandomGenerator::global()->generate())
{
}

QVector<Question> SetQuestionProvider::order(const QVector<Question>& questions,
                                             const std::function<Card(const QString&)>& cardFor,
                                             const QDateTime& now,
                                             QRandomGenerator& rng)
{
    QVector<Question> due;
    QVector<Question> fresh;
    QVector<Question> rest;
    for (const Question& q : questions) {
        const Card card = cardFor ? cardFor(q.id) : Card();
        if (card.state == CardState::New) {
            fresh.append(q);
        } else if (ReviewScheduler::isDue(card, now)) {
            due.append(q);
        } else {
            rest.append(q);
        }
    }
    for (QVector<Question>* group : {&due, &fresh, &rest}) {
        std::shuffle(group->begin(), group->end(), rng);
    }
    QVector<Question> out;
    out.reserve(questions.size());
    out += due;
    out += fresh;
    out += rest;
    return out;
}

int SetQuestionProvider::countFor(qint64 setId, const QList<QuestionType>& types) const
{
    if (!m_db || setId <= 0) {
        return 0;
    }
    return static_cast<int>(m_db->setQuestions(setId, types, true).size());
}

int SetQuestionProvider::start(qint64 setId, const QList<QuestionType>& types)
{
    m_queue.clear();
    m_total = 0;
    if (!m_db || setId <= 0) {
        return 0;
    }
    const QVector<Question> questions = m_db->setQuestions(setId, types, true);
    const QVector<Question> ordered = order(
        questions,
        [this](const QString& id) { return m_progress ? m_progress->card(id) : Card(); },
        QDateTime::currentDateTime(), m_rng);
    for (const Question& q : ordered) {
        m_queue.append(q);
    }
    m_total = static_cast<int>(m_queue.size());
    return m_total;
}

std::optional<Question> SetQuestionProvider::nextQuestion(const QStringList& /*recentIds*/)
{
    if (m_queue.isEmpty()) {
        return std::nullopt;
    }
    return m_queue.takeFirst();
}
