#include "trivia/QuizController.h"

#include "app/AppSettings.h"
#include "trivia/AnswerEvaluator.h"

QuizController::QuizController(AppSettings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<Question>();
    qRegisterMetaType<AnswerResult>();
}

QuizController::~QuizController() = default;

void QuizController::addProvider(std::unique_ptr<QuestionProvider> provider)
{
    if (provider) {
        m_providers.push_back(std::move(provider));
    }
}

bool QuizController::ask()
{
    if (m_state != State::Idle) {
        return false;
    }
    const QStringList recent = m_settings ? m_settings->triviaRecentIds() : QStringList();
    for (const auto& provider : m_providers) {
        std::optional<Question> q = provider->nextQuestion(recent);
        if (q && q->isValid()) {
            m_current = std::move(q);
            m_givenThisRound.clear();
            m_state = State::Asking;
            rememberAsked(m_current->id);
            emit questionReady(*m_current);
            return true;
        }
    }
    return false;
}

AnswerResult QuizController::submit(const QString& answer)
{
    if (m_state != State::Asking || !m_current) {
        return {};
    }
    AnswerResult result = AnswerEvaluator::evaluate(*m_current, answer, m_givenThisRound);
    if (result.empty) {
        // Nothing typed: keep asking without consuming the question.
        return result;
    }
    m_state = State::Answered;
    if (result.isCorrect() && !result.duplicate) {
        m_givenThisRound.append(result.matchedCanonical);
    }
    if (m_settings) {
        m_settings->setTriviaAnsweredCount(m_settings->triviaAnsweredCount() + 1);
        m_settings->setTriviaTotalPoints(m_settings->triviaTotalPoints() + result.points);
        m_settings->save();
    }
    emit answerEvaluated(result);
    emit progressChanged();
    return result;
}

void QuizController::skip()
{
    if (m_state == State::Idle) {
        return;
    }
    finish();
}

void QuizController::dismiss()
{
    if (m_state == State::Idle) {
        return;
    }
    finish();
}

void QuizController::resetProgress()
{
    if (m_settings) {
        m_settings->setTriviaTotalPoints(0);
        m_settings->setTriviaAnsweredCount(0);
        m_settings->setTriviaRecentIds({});
        m_settings->save();
    }
    emit progressChanged();
}

int QuizController::totalPoints() const
{
    return m_settings ? m_settings->triviaTotalPoints() : 0;
}

int QuizController::answeredCount() const
{
    return m_settings ? m_settings->triviaAnsweredCount() : 0;
}

void QuizController::rememberAsked(const QString& id)
{
    if (!m_settings) {
        return;
    }
    QStringList recent = m_settings->triviaRecentIds();
    recent.removeAll(id);
    recent.append(id);
    while (recent.size() > kRecentWindow) {
        recent.removeFirst();
    }
    m_settings->setTriviaRecentIds(recent);
    m_settings->save();
}

void QuizController::finish()
{
    m_state = State::Idle;
    m_current.reset();
    m_givenThisRound.clear();
    emit finished();
}
