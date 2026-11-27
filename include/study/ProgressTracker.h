#pragma once

#include "study/Evaluation.h"
#include "study/Question.h"

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

/** Aggregated study progress for one course (or all courses). */
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
    int record(const Question& question, const Evaluation& evaluation);

    /** Progress for a course; courseId 0 aggregates every course. */
    [[nodiscard]] StudyProgress progress(qint64 courseId);

    /** "documentId|normalized topic" keys of concepts flagged for review in a course. */
    [[nodiscard]] QSet<QString> reviewKeys(qint64 courseId);
    [[nodiscard]] static QString topicKey(qint64 documentId, const QString& topic);

    /** Clears attempts and concept progress for a course (0 = all courses). Questions are kept. */
    bool reset(qint64 courseId);

private:
    StudyDatabase* m_db = nullptr;
};
