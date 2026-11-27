#pragma once

#include "study/Evaluation.h"
#include "study/Question.h"

#include <QSet>
#include <QString>

/**
 * Grading for option-based questions. Answers are option IDs, never screen
 * positions, so shuffled presentation order cannot break the answer mapping.
 */
namespace SingleChoiceEvaluator {

[[nodiscard]] Evaluation evaluate(const Question& question, const QString& selectedOptionId);

} // namespace SingleChoiceEvaluator

namespace MultiSelectEvaluator {

/**
 * Score = max(0, (correct picks - wrong picks) / number of correct options).
 * Exactly the right set is Correct; a positive score below 1 is Partial.
 */
[[nodiscard]] Evaluation evaluate(const Question& question, const QSet<QString>& selectedOptionIds);

} // namespace MultiSelectEvaluator
