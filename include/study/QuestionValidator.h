#pragma once

#include "trivia/TriviaTypes.h"

#include <QString>
#include <QStringList>

/**
 * Rejects generated questions that are not grounded in their source passage or
 * that break the rules of their format. Pure functions; no UI or LLM access.
 */
namespace QuestionValidator {

struct Result {
    bool ok = false;
    QString reason;   // why the question was rejected (for logs and tests)
};

/** Lowercases, unifies quotes/dashes, turns punctuation into spaces, and collapses whitespace. */
[[nodiscard]] QString normalizeForMatch(const QString& text);

/**
 * True when the evidence is quoted from the source: an exact normalized match, or
 * (for longer quotes with minor model drift) at least 70% of its word 4-grams
 * appearing in the source.
 */
[[nodiscard]] bool evidenceSupported(const QString& evidence, const QString& source);

/**
 * Validates a parsed question against the passage it was generated from.
 * existingConcepts/existingPrompts hold questions already accepted for the same
 * document so the same idea is not tested twice.
 */
[[nodiscard]] Result validate(const Question& question,
                              const QString& sourceText,
                              const QStringList& existingConcepts = {},
                              const QStringList& existingPrompts = {});

} // namespace QuestionValidator
