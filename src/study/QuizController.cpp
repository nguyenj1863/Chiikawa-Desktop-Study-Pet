#include "study/QuizController.h"

#include "app/AppSettings.h"
#include "study/ChoiceEvaluators.h"
#include "study/ProgressTracker.h"
#include "study/WrittenAnswerEvaluator.h"

QuizController::QuizController(AppSettings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<Question>();
    qRegisterMetaType<Evaluation>();
}

QuizController::~QuizController()
{
    cancelJudging();
}

void QuizController::addProvider(std::unique_ptr<QuestionProvider> provider)
{
    if (provider) {
        m_providers.push_back(std::move(provider));
    }
}

void QuizController::setWrittenEvaluator(WrittenAnswerEvaluator* evaluator)
{
    if (m_written) {
        disconnect(m_written, nullptr, this, nullptr);
    }
    m_written = evaluator;
    if (m_written) {
        connect(m_written, &WrittenAnswerEvaluator::judged, this, [this](int id, const Evaluation& evaluation) {
            if (id != m_judgeId || m_state != State::Evaluating) {
                return;
            }
            m_judgeId = 0;
            record(evaluation);
        });
    }
}

bool QuizController::ask()
{
    if (m_state != State::Idle) {
        return false;
    }
    const QStringList recent = m_settings ? m_settings->recentQuestionIds() : QStringList();
    for (const auto& provider : m_providers) {
        if (!m_activeProvider.isEmpty() && provider->id() != m_activeProvider) {
            continue;
        }
        std::optional<Question> q = provider->nextQuestion(recent);
        if (q && q->isValid()) {
            m_current = std::move(q);
            m_state = State::Asking;
            rememberAsked(m_current->id);
            emit questionReady(*m_current);
            return true;
        }
    }
    return false;
}

Evaluation QuizController::record(const Evaluation& evaluation)
{
    const int xp = (m_tracker && m_current) ? m_tracker->record(*m_current, evaluation) : 0;
    m_state = State::Answered;
    emit evaluationReady(evaluation, xp);
    emit progressChanged();
    return evaluation;
}

Evaluation QuizController::submitChoice(const QString& optionId)
{
    if (m_state != State::Asking || !m_current || m_current->type != QuestionType::SingleChoice || optionId.isEmpty()) {
        return {};
    }
    return record(SingleChoiceEvaluator::evaluate(*m_current, optionId));
}

Evaluation QuizController::submitSelection(const QSet<QString>& optionIds)
{
    if (m_state != State::Asking || !m_current || m_current->type != QuestionType::MultiSelect || optionIds.isEmpty()) {
        return {};
    }
    return record(MultiSelectEvaluator::evaluate(*m_current, optionIds));
}

QuizController::WrittenStep QuizController::submitWritten(const QString& answer, bool allowSuggestion)
{
    if (m_state != State::Asking || !m_current || m_current->type != QuestionType::Written
        || answer.trimmed().isEmpty()) {
        return WrittenStep::Ignored;
    }
    const WrittenAnswerEvaluator::Precheck pre = WrittenAnswerEvaluator::precheck(*m_current, answer, allowSuggestion);
    switch (pre.kind) {
    case WrittenAnswerEvaluator::Precheck::Kind::Final:
        record(pre.evaluation);
        return WrittenStep::Final;
    case WrittenAnswerEvaluator::Precheck::Kind::Suggest:
        emit suggestionOffered(pre.suggestion);
        return WrittenStep::Suggestion;
    case WrittenAnswerEvaluator::Precheck::Kind::NeedsModel:
        break;
    }
    if (!m_written) {
        record(WrittenAnswerEvaluator::unavailableEvaluation(QStringLiteral("No local AI model is set up to check written answers.")));
        return WrittenStep::Final;
    }
    m_state = State::Evaluating;
    m_judgeId = m_written->judge(*m_current, answer);
    emit judgingStarted();
    return WrittenStep::Judging;
}

bool QuizController::reveal()
{
    if (!m_current || (m_state != State::Asking && m_state != State::Evaluating)) {
        return false;
    }
    cancelJudging();
    m_state = State::Revealed;
    return true;
}

Evaluation QuizController::selfAssess(SelfRating rating)
{
    if (m_state != State::Revealed || !m_current) {
        return {};
    }
    return record(StudyEval::selfAssessment(rating));
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
        m_settings->setRecentQuestionIds({});
        m_settings->save();
    }
    emit progressChanged();
}

void QuizController::rememberAsked(const QString& id)
{
    if (!m_settings) {
        return;
    }
    QStringList recent = m_settings->recentQuestionIds();
    recent.removeAll(id);
    recent.append(id);
    while (recent.size() > kRecentWindow) {
        recent.removeFirst();
    }
    m_settings->setRecentQuestionIds(recent);
    m_settings->save();
}

void QuizController::cancelJudging()
{
    if (m_judgeId != 0 && m_written) {
        m_written->cancel(m_judgeId);
    }
    m_judgeId = 0;
}

void QuizController::finish()
{
    cancelJudging();
    m_state = State::Idle;
    m_current.reset();
    emit finished();
}
