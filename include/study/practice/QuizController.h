#pragma once

#include "study/evaluation/Evaluation.h"
#include "study/practice/QuestionProvider.h"
#include "study/Question.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <memory>
#include <optional>
#include <vector>

class AppSettings;
class ProgressTracker;
class WrittenAnswerEvaluator;

/**
 * Owns the active check-in question and its lifecycle. Questions are graded by
 * format-specific evaluators and recorded through ProgressTracker.
 * Providers are tried in order; setActiveProvider() limits asking to one of them.
 */
class QuizController : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Asking,
        Evaluating,   // a written answer is being judged by the local model
        Answered,
        Revealed      // expected answer shown; waiting for self-assessment
    };

    /** Result of submitting a written answer. */
    enum class WrittenStep {
        Ignored,      // nothing to evaluate (wrong state, wrong type, or empty answer)
        Final,        // evaluationReady() was emitted
        Suggestion,   // suggestionOffered() was emitted; resubmit with allowSuggestion = false
        Judging       // the local model is checking; evaluationReady() follows
    };

    static constexpr int kRecentWindow = 10;

    explicit QuizController(AppSettings* settings, QObject* parent = nullptr);
    ~QuizController() override;

    void addProvider(std::unique_ptr<QuestionProvider> provider);
    /** Only providers with this ID are asked; an empty ID asks every provider in order. */
    void setActiveProvider(const QString& providerId) { m_activeProvider = providerId; }
    [[nodiscard]] QString activeProvider() const { return m_activeProvider; }

    void setProgressTracker(ProgressTracker* tracker) { m_tracker = tracker; }
    /**
     * Practice sessions walk a queue on purpose: recent questions are neither avoided nor
     * remembered, and answering a card that is not yet due records the attempt but leaves
     * its review schedule alone (only new or due cards move).
     */
    void setPracticeMode(bool enabled) { m_practiceMode = enabled; }
    [[nodiscard]] bool practiceMode() const { return m_practiceMode; }
    void setWrittenEvaluator(WrittenAnswerEvaluator* evaluator);

    /** Fetches a new question; returns false when one is already active or none are available. */
    bool ask();

    /** Multiple choice: grades the chosen option ID. */
    Evaluation submitChoice(const QString& optionId);
    /** Multiple select: grades the full set of chosen option IDs. */
    Evaluation submitSelection(const QSet<QString>& optionIds);
    /** Written answer: normalization, spelling suggestion, then (if needed) the local model. */
    WrittenStep submitWritten(const QString& answer, bool allowSuggestion = true);
    /** Shows the expected answer instead of grading; the student then self-assesses. */
    bool reveal();
    /** Records a self-assessment after reveal(), stored apart from automatic grading. */
    Evaluation selfAssess(SelfRating rating);

    /** Closes the active question without any penalty. */
    void skip();

    /** Closes the question after its result has been shown. */
    void dismiss();

    void resetProgress();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] const std::optional<Question>& currentQuestion() const { return m_current; }

signals:
    void questionReady(const Question& question);
    void evaluationReady(const Evaluation& evaluation);
    void suggestionOffered(const QString& suggestion);
    void judgingStarted();
    void finished();
    void progressChanged();

private:
    void rememberAsked(const QString& id);
    void finish();
    void cancelJudging();
    Evaluation record(const Evaluation& evaluation);

    AppSettings* m_settings = nullptr;
    std::vector<std::unique_ptr<QuestionProvider>> m_providers;
    QString m_activeProvider;
    ProgressTracker* m_tracker = nullptr;
    QPointer<WrittenAnswerEvaluator> m_written;
    int m_judgeId = 0;
    std::optional<Question> m_current;
    State m_state = State::Idle;
    bool m_practiceMode = false;
};
