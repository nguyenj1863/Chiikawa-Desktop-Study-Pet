#include "study/ChoiceEvaluators.h"

#include <algorithm>

namespace StudyEval {

QString verdictLabel(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Correct:
        return QStringLiteral("Correct");
    case Verdict::Partial:
        return QStringLiteral("Partially correct");
    case Verdict::Incorrect:
        return QStringLiteral("Not quite");
    case Verdict::NeedsReview:
        return QStringLiteral("Needs review");
    }
    return QString();
}

QString selfRatingLabel(SelfRating rating)
{
    switch (rating) {
    case SelfRating::GotIt:
        return QStringLiteral("Got it");
    case SelfRating::PartlyGotIt:
        return QStringLiteral("Partially got it");
    case SelfRating::NeedReview:
        return QStringLiteral("Need review");
    }
    return QString();
}

Evaluation selfAssessment(SelfRating rating)
{
    Evaluation e;
    e.kind = AttemptKind::SelfAssessed;
    e.selfRating = rating;
    e.ratingSource = RatingSource::Chosen;
    switch (rating) {
    case SelfRating::GotIt:
        e.verdict = Verdict::Correct;
        e.score = 1.0;
        break;
    case SelfRating::PartlyGotIt:
        e.verdict = Verdict::Partial;
        e.score = 0.5;
        break;
    case SelfRating::NeedReview:
        e.verdict = Verdict::Incorrect;
        e.score = 0.0;
        break;
    }
    e.feedback = selfRatingLabel(rating);
    return e;
}

Rating ratingFor(const Evaluation& evaluation)
{
    if (evaluation.rating != Rating::None) {
        return evaluation.rating;
    }
    switch (evaluation.verdict) {
    case Verdict::Correct:
        return Rating::Good;
    case Verdict::Partial:
        return Rating::Hard;
    case Verdict::Incorrect:
        return Rating::Again;
    case Verdict::NeedsReview:
        break;   // uncertain grading leaves the schedule alone
    }
    return Rating::None;
}

} // namespace StudyEval

namespace SingleChoiceEvaluator {

Evaluation evaluate(const Question& question, const QString& selectedOptionId)
{
    Evaluation e;
    const QuestionOption* chosen = nullptr;
    const QuestionOption* right = nullptr;
    for (const QuestionOption& o : question.options) {
        e.options.append({o.id, o.id == selectedOptionId, o.correct, o.explanation});
        if (o.id == selectedOptionId) {
            chosen = &o;
        }
        if (o.correct && !right) {
            right = &o;
        }
    }
    if (chosen && chosen->correct) {
        e.verdict = Verdict::Correct;
        e.score = 1.0;
        e.feedback = chosen->explanation.isEmpty() ? question.explanation : chosen->explanation;
    } else {
        e.verdict = Verdict::Incorrect;
        e.score = 0.0;
        QStringList parts;
        if (chosen && !chosen->explanation.isEmpty()) {
            parts.append(chosen->explanation);
        }
        if (right) {
            parts.append(QStringLiteral("Answer: %1.").arg(right->text));
        }
        e.feedback = parts.join(QLatin1Char(' '));
    }
    return e;
}

} // namespace SingleChoiceEvaluator

namespace MultiSelectEvaluator {

Evaluation evaluate(const Question& question, const QSet<QString>& selectedOptionIds)
{
    Evaluation e;
    int totalCorrect = 0;
    int correctPicks = 0;
    int wrongPicks = 0;
    for (const QuestionOption& o : question.options) {
        const bool selected = selectedOptionIds.contains(o.id);
        e.options.append({o.id, selected, o.correct, o.explanation});
        if (o.correct) {
            ++totalCorrect;
            if (selected) {
                ++correctPicks;
            }
        } else if (selected) {
            ++wrongPicks;
        }
    }
    if (totalCorrect == 0) {
        e.verdict = Verdict::NeedsReview;
        return e;
    }
    e.score = std::max(0.0, static_cast<double>(correctPicks - wrongPicks) / totalCorrect);
    if (correctPicks == totalCorrect && wrongPicks == 0) {
        e.verdict = Verdict::Correct;
        e.score = 1.0;
        e.feedback = QStringLiteral("You found all %1 correct statements.").arg(totalCorrect);
    } else if (e.score > 0.0) {
        e.verdict = Verdict::Partial;
        e.feedback = QStringLiteral("%1 of %2 correct statements found%3.")
                         .arg(correctPicks)
                         .arg(totalCorrect)
                         .arg(wrongPicks > 0 ? QStringLiteral(", %1 incorrect picked").arg(wrongPicks) : QString());
    } else {
        e.verdict = Verdict::Incorrect;
        e.feedback = QStringLiteral("Check each statement against the explanation below.");
    }
    return e;
}

} // namespace MultiSelectEvaluator
