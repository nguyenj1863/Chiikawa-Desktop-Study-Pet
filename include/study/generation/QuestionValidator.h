#pragma once

#include "study/Question.h"

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

/** Reason given when a question points at a figure, table or data the student would not see. */
inline constexpr const char* kUnseenVisualReason = "refers to a figure or table the student cannot see";

/** Reason given when a question points at notes, slides or a lecture instead of naming its subject. */
inline constexpr const char* kVagueSourceReason = "refers to notes or material the student cannot see";
/** Reason given when a question asks for a value but shows none of the numbers it is worked out from. */
inline constexpr const char* kMissingInputsReason = "asks for a value without showing the numbers it needs";

/**
 * True when the question or its answers point at a visual or unseen data: "the figure", "the diagram
 * below", "Table 2", "the data shown", "according to the chart", "the observed data", "in the
 * experiment". Generic words such as "the left table" in a databases question do not count.
 */
[[nodiscard]] bool referencesVisual(const Question& question);

/** True when the question says "your notes", "the lecture", "as mentioned earlier" and the like. */
[[nodiscard]] bool referencesUnseenSource(const Question& question);

/**
 * Why a question cannot be answered from what the student sees (its text, options and attached
 * support), or an empty string when it can. One of kUnseenVisualReason, kVagueSourceReason or
 * kMissingInputsReason. Used when generating and to flag questions already stored.
 */
[[nodiscard]] QString contextProblem(const Question& question);

/** True for the reasons contextProblem() returns. */
[[nodiscard]] bool isContextReason(const QString& reason);

/**
 * A generated question (not one the student wrote or edited) that cannot be answered from what
 * it shows. Such questions are kept, flagged in the library, and left out of practice and check-ins.
 */
[[nodiscard]] bool needsContext(const Question& question);

/** The questions without needsContext(), in the same order. */
[[nodiscard]] QVector<Question> answerable(const QVector<Question>& questions);

/** Lowercases, unifies quotes/dashes, turns punctuation into spaces, and collapses whitespace. */
[[nodiscard]] QString normalizeForMatch(const QString& text);

/**
 * True when the evidence is quoted from the source: an exact normalized match, or
 * (for longer quotes with minor model drift) at least 70% of its word 4-grams
 * appearing in the source.
 */
[[nodiscard]] bool evidenceSupported(const QString& evidence, const QString& source);

/**
 * True when a data snippet shown to the student is quoted from the source: evidenceSupported(), and
 * every number in it appears in the source (the fuzzy match alone would let one changed value through).
 */
[[nodiscard]] bool dataQuoted(const QString& data, const QString& source);

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
