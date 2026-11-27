#pragma once

#include <QString>

/** Text normalization shared by written-answer matching and typo suggestions. */
namespace AnswerNormalizer {

/**
 * Case-folds, strips accents and punctuation (except '+' and '#'), collapses
 * whitespace, and drops a leading article ("the", "a", "an").
 */
[[nodiscard]] QString normalize(const QString& text);

} // namespace AnswerNormalizer
