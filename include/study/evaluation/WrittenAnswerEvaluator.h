#pragma once

#include "ai/LlmClient.h"
#include "study/evaluation/Evaluation.h"
#include "study/Question.h"

#include <QHash>
#include <QObject>
#include <QPointer>

/**
 * Checks typed answers in three stages:
 *  1. normalization (case, punctuation, whitespace, articles, accents) and accepted aliases;
 *  2. fuzzy matching that offers a "Did you mean …?" suggestion for likely misspellings;
 *  3. a local-LLM judge for longer or paraphrased answers, grounded in the source passage.
 * Uncertain or failed judgements become NeedsReview, never a confident Incorrect.
 */
class WrittenAnswerEvaluator : public QObject {
    Q_OBJECT

public:
    struct Precheck {
        enum class Kind {
            Final,       // decided without the model (exact or alias match)
            Suggest,     // likely misspelling; ask the student before judging
            NeedsModel   // needs semantic judgement
        };
        Kind kind = Kind::NeedsModel;
        Evaluation evaluation;   // valid when kind == Final
        QString suggestion;      // corrected answer text when kind == Suggest
    };

    explicit WrittenAnswerEvaluator(LlmClient* llm, QObject* parent = nullptr);
    ~WrittenAnswerEvaluator() override;

    /** Stages 1 and 2. With allowSuggestion false (the student kept their wording), no suggestion is offered. */
    [[nodiscard]] static Precheck precheck(const Question& question, const QString& answer, bool allowSuggestion = true);

    /** Stage 3: asks the local model; emits judged() with the same ID. Returns 0 if no model client. */
    int judge(const Question& question, const QString& answer);
    void cancel(int judgeId);

    [[nodiscard]] static LlmRequest buildJudgeRequest(const Question& question, const QString& answer);
    [[nodiscard]] static Evaluation parseJudgement(const Question& question, const QString& json);
    /** Used when the model is unavailable or fails: ask the student to compare with the answer. */
    [[nodiscard]] static Evaluation unavailableEvaluation(const QString& reason);

    /** Optimal string alignment (Damerau-Levenshtein) distance; exposed for tests. */
    [[nodiscard]] static int editDistance(const QString& a, const QString& b);

signals:
    void judged(int judgeId, const Evaluation& evaluation);

private:
    QPointer<LlmClient> m_llm;
    QHash<int, Question> m_pending;   // LLM request ID -> question
};
