#pragma once

#include "trivia/QuestionProvider.h"
#include "trivia/TriviaTypes.h"

#include <QObject>
#include <memory>
#include <optional>
#include <vector>

class AppSettings;

/**
 * Owns the active check-in question, evaluates answers, and persists
 * progress (total points, answered count, recently asked IDs) via AppSettings.
 * Providers are tried in order, so a document-based provider can be added
 * ahead of or after the offline trivia set.
 */
class QuizController : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Asking,
        Answered
    };

    static constexpr int kRecentWindow = 10;

    explicit QuizController(AppSettings* settings, QObject* parent = nullptr);
    ~QuizController() override;

    void addProvider(std::unique_ptr<QuestionProvider> provider);

    /** Fetches a new question; returns false when one is already active or none are available. */
    bool ask();

    /** Evaluates the answer and records progress for valid, non-duplicate answers. */
    AnswerResult submit(const QString& answer);

    /** Closes the active question without any penalty. */
    void skip();

    /** Closes the question after its result has been shown. */
    void dismiss();

    void resetProgress();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] const std::optional<Question>& currentQuestion() const { return m_current; }
    [[nodiscard]] int totalPoints() const;
    [[nodiscard]] int answeredCount() const;

signals:
    void questionReady(const Question& question);
    void answerEvaluated(const AnswerResult& result);
    void finished();
    void progressChanged();

private:
    void rememberAsked(const QString& id);
    void finish();

    AppSettings* m_settings = nullptr;
    std::vector<std::unique_ptr<QuestionProvider>> m_providers;
    std::optional<Question> m_current;
    QStringList m_givenThisRound;
    State m_state = State::Idle;
};
