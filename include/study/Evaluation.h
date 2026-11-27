#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

/** Outcome of checking a document question. Values are persisted; append only. */
enum class Verdict {
    Correct = 0,
    Partial = 1,
    Incorrect = 2,
    NeedsReview = 3   // could not be judged confidently; never counted as wrong
};

/** Student's own rating after Reveal Answer. Values are persisted; append only. */
enum class SelfRating {
    GotIt = 0,
    PartlyGotIt = 1,
    NeedReview = 2
};

/** How an attempt was graded. Self-assessed results are stored and scored separately. */
enum class AttemptKind {
    Auto = 0,
    SelfAssessed = 1
};

/** The four self-assessment ratings of Anki's study loop. Values are persisted; append only. */
enum class Rating {
    None = 0,
    Again = 1,
    Hard = 2,
    Good = 3,
    Easy = 4
};

/** Where a stored rating came from. */
enum class RatingSource {
    Auto = 0,     // derived from automatic grading
    Chosen = 1    // picked by the student (after Reveal, or adjusted after a result)
};

struct OptionResult {
    QString id;
    bool selected = false;
    bool correct = false;
    QString explanation;
};

struct Evaluation {
    Verdict verdict = Verdict::NeedsReview;
    double score = 0.0;                // 0..1, partial credit for multi-select and written answers
    QString feedback;                  // short, source-grounded explanation for the student
    QList<OptionResult> options;       // per-option results for choice questions
    QStringList matchedPoints;         // written: key points the answer covered
    QStringList missingPoints;         // written: key points it missed
    AttemptKind kind = AttemptKind::Auto;
    SelfRating selfRating = SelfRating::NeedReview;   // only meaningful when kind == SelfAssessed
    Rating rating = Rating::None;                     // None: derive it from the verdict (see StudyEval::ratingFor)
    RatingSource ratingSource = RatingSource::Auto;

    [[nodiscard]] bool isCorrect() const { return verdict == Verdict::Correct; }
};

namespace StudyEval {

[[nodiscard]] QString verdictLabel(Verdict verdict);
[[nodiscard]] QString selfRatingLabel(SelfRating rating);

/** Evaluation recorded for a Reveal Answer + self-assessment, kept apart from automatic grading. */
[[nodiscard]] Evaluation selfAssessment(SelfRating rating);

/** Rating that schedules the next review: the explicit one, else Correct to Good, Partial to Hard, Incorrect to Again. */
[[nodiscard]] Rating ratingFor(const Evaluation& evaluation);

} // namespace StudyEval

Q_DECLARE_METATYPE(Evaluation)
