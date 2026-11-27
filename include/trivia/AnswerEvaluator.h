#pragma once

#include "trivia/TriviaTypes.h"

#include <QStringList>

/**
 * Pure answer matching and rarity scoring, independent of the UI.
 */
namespace AnswerEvaluator {

/**
 * Case-folds, strips accents and punctuation (except '+' and '#'), collapses
 * whitespace, and drops a leading article ("the", "a", "an").
 */
[[nodiscard]] QString normalize(const QString& text);

/**
 * Matches input against the question's answers and aliases.
 * alreadyGiven holds canonical answers accepted earlier for the same question;
 * repeating one scores zero and sets duplicate.
 */
[[nodiscard]] AnswerResult evaluate(const Question& question,
                                    const QString& input,
                                    const QStringList& alreadyGiven = {});

} // namespace AnswerEvaluator
