#pragma once

#include "study/evaluation/Evaluation.h"
#include "study/Question.h"
#include "study/progress/ReviewScheduler.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

class StudyDatabase;

struct FormatProgress {
    int attempted = 0;
    int correct = 0;
    int partial = 0;
};

/** Aggregated study progress for one study set (or all studySets). */
struct StudyProgress {
    // Automatically graded answers
    int attempted = 0;
    int correct = 0;
    int partial = 0;
    int incorrect = 0;
    int needsReview = 0;
    // Reveal Answer + self-assessment, counted separately
    int selfAssessed = 0;
    int selfGotIt = 0;
    int selfPartly = 0;
    int selfNeedReview = 0;

    int dueCount = 0;                     // cards due for review right now
    QMap<int, FormatProgress> byFormat;   // key: QuestionType
    QStringList reviewTopics;             // concepts currently flagged for review

    /** Questions answered, automatically graded or self-assessed. */
    [[nodiscard]] int answered() const { return attempted + selfAssessed; }
    /** Share answered fully right (0..1) among answers that could be judged; 0 with no answers. */
    [[nodiscard]] double accuracy() const
    {
        const int judged = answered() - needsReview;
        return judged > 0 ? static_cast<double>(correct + selfGotIt) / judged : 0.0;
    }
};

/**
 * Records attempts and per-concept progress in the study database and keeps each question's
 * review schedule. Progress is reported as plain study signals: questions answered, accuracy,
 * cards due, and concepts to review.
 */
class ProgressTracker {
public:
    explicit ProgressTracker(StudyDatabase* db) : m_db(db) {}

    /** Stores the attempt and updates the concept's review state. Returns false if nothing was stored. */
    bool record(const Question& question, const Evaluation& evaluation, bool reschedule = true);

    /** Spaced-repetition state of a question; a question never answered is a new card. */
    [[nodiscard]] Card card(const QString& questionId);

    /** Progress for a study set; setId 0 aggregates every study set. */
    [[nodiscard]] StudyProgress progress(qint64 setId);

    /** "documentId|normalized topic" keys of concepts flagged for review in a study set. */
    [[nodiscard]] QSet<QString> reviewKeys(qint64 setId);
    [[nodiscard]] static QString topicKey(qint64 documentId, const QString& topic);

    /** Clears attempts and concept progress for a study set (0 = all studySets). Questions are kept. */
    bool reset(qint64 setId);

private:
    StudyDatabase* m_db = nullptr;
};
