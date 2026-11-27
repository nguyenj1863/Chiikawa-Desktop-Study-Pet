#pragma once

#include "study/Evaluation.h"
#include "study/Question.h"
#include "study/ReviewScheduler.h"

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
    int xp = 0;
    // Reveal Answer + self-assessment, counted separately
    int selfAssessed = 0;
    int selfGotIt = 0;
    int selfPartly = 0;
    int selfNeedReview = 0;
    int selfXp = 0;

    QMap<int, FormatProgress> byFormat;   // key: QuestionType
    QStringList reviewTopics;             // concepts currently flagged for review

    [[nodiscard]] int totalXp() const { return xp + selfXp; }
};

/**
 * Records attempts and per-concept progress in the study database and awards XP.
 * XP depends on difficulty (easy 10, medium 20, hard 35) times the score for
 * automatically graded answers. Answers revealed and self-assessed earn a smaller,
 * separately stored amount, never the automatic-correctness XP.
 */
class ProgressTracker {
public:
    explicit ProgressTracker(StudyDatabase* db) : m_db(db) {}

    [[nodiscard]] static int baseXp(Difficulty difficulty);
    [[nodiscard]] static int xpFor(const Question& question, const Evaluation& evaluation);

    /** Stores the attempt and updates the concept's review state. Returns the XP awarded. */
    int record(const Question& question, const Evaluation& evaluation, bool reschedule = true);

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
