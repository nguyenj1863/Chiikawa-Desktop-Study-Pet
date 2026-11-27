#include "study/QuestionSelector.h"

#include "study/ProgressTracker.h"
#include "study/StudyDatabase.h"

#include <algorithm>

namespace QuestionSelector {

QList<QuestionType> activeFormats(const SelectionPrefs& prefs)
{
    QList<QuestionType> enabled;
    for (const QuestionType t : prefs.enabled) {
        if (!enabled.contains(t)) {
            enabled.append(t);
        }
    }
    if (prefs.mode == FormatMode::Focus) {
        if (enabled.contains(prefs.focus)) {
            return {prefs.focus};
        }
        return enabled.isEmpty() ? QList<QuestionType>{} : QList<QuestionType>{enabled.first()};
    }
    return enabled;
}

std::optional<QuestionType> chooseFormat(const QList<QuestionType>& available, const QList<QuestionType>& recentTypes,
                                         QRandomGenerator& rng)
{
    if (available.isEmpty()) {
        return std::nullopt;
    }
    QList<QuestionType> options = available;
    if (options.size() > 1 && recentTypes.size() >= 2) {
        const QuestionType last = recentTypes.last();
        if (recentTypes.at(recentTypes.size() - 2) == last) {
            options.removeAll(last);   // avoid a third question of the same format in a row
        }
    }
    return options.at(static_cast<qsizetype>(rng.bounded(static_cast<quint32>(options.size()))));
}

Question withShuffledOptions(Question question, QRandomGenerator& rng)
{
    for (qsizetype i = question.options.size() - 1; i > 0; --i) {
        const auto j = static_cast<qsizetype>(rng.bounded(static_cast<quint32>(i + 1)));
        question.options.swapItemsAt(i, j);
    }
    return question;
}

std::optional<Question> pick(const QVector<Question>& candidates, const SelectionPrefs& prefs, const QStringList& recentIds,
                             const QSet<QString>& reviewKeys, const QList<QuestionType>& recentTypes, QRandomGenerator& rng)
{
    const QList<QuestionType> formats = activeFormats(prefs);
    QVector<Question> pool;
    for (const Question& q : candidates) {
        if (formats.contains(q.type) && q.isValid()) {
            pool.append(q);
        }
    }
    if (pool.isEmpty()) {
        return std::nullopt;
    }

    // Prefer formats that still have questions not asked recently.
    QList<QuestionType> fresh;
    QList<QuestionType> any;
    for (const Question& q : pool) {
        if (!any.contains(q.type)) {
            any.append(q.type);
        }
        if (!recentIds.contains(q.id) && !fresh.contains(q.type)) {
            fresh.append(q.type);
        }
    }
    const std::optional<QuestionType> type = chooseFormat(fresh.isEmpty() ? any : fresh, recentTypes, rng);
    if (!type) {
        return std::nullopt;
    }

    QVector<Question> ofType;
    QVector<Question> unasked;
    for (const Question& q : pool) {
        if (q.type == *type) {
            ofType.append(q);
            if (!recentIds.contains(q.id)) {
                unasked.append(q);
            }
        }
    }
    if (unasked.isEmpty()) {
        // Everything was asked recently: reuse the one asked longest ago.
        const auto oldest = std::min_element(ofType.cbegin(), ofType.cend(), [&](const Question& a, const Question& b) {
            return recentIds.indexOf(a.id) < recentIds.indexOf(b.id);
        });
        return withShuffledOptions(*oldest, rng);
    }

    QVector<Question> review;
    for (const Question& q : unasked) {
        if (reviewKeys.contains(ProgressTracker::topicKey(q.documentId, q.topic))) {
            review.append(q);
        }
    }
    const QVector<Question>& from = (!review.isEmpty() && rng.generateDouble() < kReviewPreference) ? review : unasked;
    const Question chosen = from.at(static_cast<qsizetype>(rng.bounded(static_cast<quint32>(from.size()))));
    return withShuffledOptions(chosen, rng);
}

} // namespace QuestionSelector

StudyQuestionProvider::StudyQuestionProvider(StudyDatabase* db, ProgressTracker* progress)
    : m_db(db)
    , m_progress(progress)
    , m_rng(QRandomGenerator::global()->generate())
{
}

std::optional<Question> StudyQuestionProvider::nextQuestion(const QStringList& recentIds)
{
    if (!m_db || m_setId <= 0) {
        return std::nullopt;
    }
    const QVector<Question> candidates = m_db->setQuestions(m_setId, QuestionSelector::activeFormats(m_prefs));
    const QSet<QString> review = m_progress ? m_progress->reviewKeys(m_setId) : QSet<QString>();
    std::optional<Question> q = QuestionSelector::pick(candidates, m_prefs, recentIds, review, m_recentTypes, m_rng);
    if (q) {
        m_recentTypes.append(q->type);
        while (m_recentTypes.size() > 2) {
            m_recentTypes.removeFirst();
        }
    }
    return q;
}

int StudyQuestionProvider::availableCount() const
{
    if (!m_db || m_setId <= 0) {
        return 0;
    }
    return static_cast<int>(m_db->setQuestions(m_setId, QuestionSelector::activeFormats(m_prefs)).size());
}
