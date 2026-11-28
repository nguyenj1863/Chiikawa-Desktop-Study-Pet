#include "studyui/StudyPracticeView.h"

#include "ai/OllamaClient.h"
#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "study/practice/QuizController.h"
#include "study/practice/SetQuestionProvider.h"
#include "study/StudyLibrary.h"
#include "study/evaluation/WrittenAnswerEvaluator.h"
#include "ui/PixelTheme.h"
#include "studyui/QuizPopup.h"
#include "studyui/StudyUiHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace StudyUi;

StudyPracticeView::StudyPracticeView(AppSettings* settings, StudyLibrary* library, ProgressTracker* progress,
                                     OllamaClient* ollama, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_library(library)
    , m_tracker(progress)
{
    setObjectName(QStringLiteral("studyPractice"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#studyPractice { background: transparent; border: none; }"));

    m_written = std::make_unique<WrittenAnswerEvaluator>(ollama);
    m_controller = std::make_unique<QuizController>(settings);
    m_controller->setPracticeMode(true);
    m_controller->setProgressTracker(progress);
    m_controller->setWrittenEvaluator(m_written.get());
    auto provider = std::make_unique<SetQuestionProvider>(library ? library->database() : nullptr, progress);
    m_provider = provider.get();
    m_controller->addProvider(std::move(provider));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages, 1);

    // Page 0: the question
    auto* questionPage = new QWidget(m_pages);
    auto* q = new QVBoxLayout(questionPage);
    q->setContentsMargins(0, 0, 0, 0);
    q->setSpacing(6);
    auto* top = new QHBoxLayout();
    top->setSpacing(6);
    m_progressLabel = makeLabel(QString(), "sectionTitle", questionPage);
    m_endBtn = makeButton(QStringLiteral("End session"), questionPage, "chip");
    top->addWidget(m_progressLabel, 1);
    top->addWidget(m_endBtn);
    q->addLayout(top);
    auto* card = makeCard(questionPage);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 9, 10, 9);
    m_question = new QuizPopup(card, true);
    cardLayout->addWidget(m_question);
    q->addWidget(card, 1);
    m_pages->addWidget(questionPage);

    // Page 1: the summary
    auto* summaryPage = new QWidget(m_pages);
    auto* s = new QVBoxLayout(summaryPage);
    s->setContentsMargins(0, 0, 0, 0);
    s->setSpacing(8);
    auto* summaryCard = makeCard(summaryPage);
    auto* summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(12, 10, 12, 12);
    summaryLayout->setSpacing(6);
    summaryLayout->addWidget(makeLabel(QStringLiteral("Session complete"), "sectionTitle", summaryCard));
    m_summaryLabel = makeLabel(QString(), "sectionSummary", summaryCard);
    summaryLayout->addWidget(m_summaryLabel);
    s->addWidget(summaryCard);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_backBtn = makeButton(QStringLiteral("Back to set"), summaryPage);
    m_againBtn = makeButton(QStringLiteral("Practice again"), summaryPage, "primary");
    buttons->addStretch(1);
    buttons->addWidget(m_backBtn);
    buttons->addWidget(m_againBtn);
    s->addLayout(buttons);
    s->addStretch(1);
    m_pages->addWidget(summaryPage);

    wireQuestion();
    connect(m_endBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        end();
    });
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        m_active = false;
        emit finished();
    });
    connect(m_againBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        if (!begin(m_setId)) {
            m_active = false;
            emit finished();
        }
    });
}

StudyPracticeView::~StudyPracticeView() = default;

QList<QuestionType> StudyPracticeView::enabledTypes(const AppSettings* settings)
{
    QList<QuestionType> types;
    if (!settings) {
        return types;
    }
    if (settings->studyFormatMode() == SettingsDefaults::kStudyFormatModeFocus) {
        switch (settings->studyFocusFormat()) {
        case SettingsDefaults::kStudyFormatMultiSelect:
            return {QuestionType::MultiSelect};
        case SettingsDefaults::kStudyFormatWritten:
            return {QuestionType::Written};
        default:
            return {QuestionType::SingleChoice};
        }
    }
    const int mask = settings->studyFormats();
    if (mask & SettingsDefaults::kStudyFormatSingleChoice) {
        types.append(QuestionType::SingleChoice);
    }
    if (mask & SettingsDefaults::kStudyFormatMultiSelect) {
        types.append(QuestionType::MultiSelect);
    }
    if (mask & SettingsDefaults::kStudyFormatWritten) {
        types.append(QuestionType::Written);
    }
    return types;
}

bool StudyPracticeView::showingSummary() const
{
    return m_pages->currentIndex() == 1;
}

void StudyPracticeView::wireQuestion()
{
    connect(m_question, &QuizPopup::buttonActivated, this, &StudyPracticeView::buttonActivated);
    connect(m_question, &QuizPopup::skipRequested, this, [this]() {
        ++m_summary.skipped;
        m_controller->skip();
    });
    connect(m_question, &QuizPopup::choiceSubmitted, this,
            [this](const QString& optionId) { m_controller->submitChoice(optionId); });
    connect(m_question, &QuizPopup::selectionSubmitted, this, [this](const QStringList& ids) {
        m_controller->submitSelection(QSet<QString>(ids.cbegin(), ids.cend()));
    });
    connect(m_question, &QuizPopup::writtenSubmitted, this, [this](const QString& answer, bool allowSuggestion) {
        if (m_controller->submitWritten(answer, allowSuggestion) == QuizController::WrittenStep::Judging) {
            m_question->showJudging();
        }
    });
    connect(m_question, &QuizPopup::revealRequested, this, [this]() {
        if (m_controller->reveal()) {
            m_question->showReveal();
        }
    });
    connect(m_question, &QuizPopup::selfAssessed, this,
            [this](int rating) { m_controller->selfAssess(static_cast<SelfRating>(rating)); });

    connect(m_controller.get(), &QuizController::questionReady, this, [this](const Question& question) {
        updateProgress();
        m_question->setAdvanceText(m_provider->remaining() == 0 ? QStringLiteral("Finish") : QStringLiteral("Next question"));
        m_question->showQuestion(question, QRect());
    });
    connect(m_controller.get(), &QuizController::suggestionOffered, m_question, &QuizPopup::showSuggestion);
    connect(m_controller.get(), &QuizController::evaluationReady, this, [this](const Evaluation& evaluation) {
        ++m_summary.answered;
        switch (evaluation.verdict) {
        case Verdict::Correct:
            ++m_summary.correct;
            break;
        case Verdict::Partial:
            ++m_summary.partial;
            break;
        case Verdict::Incorrect:
            ++m_summary.incorrect;
            break;
        case Verdict::NeedsReview:
            break;
        }
        const auto& current = m_controller->currentQuestion();
        if (current && (evaluation.verdict == Verdict::Partial || evaluation.verdict == Verdict::Incorrect)) {
            const QString topic = current->topic.isEmpty() ? current->prompt : current->topic;
            if (!m_summary.topicsToReview.contains(topic)) {
                m_summary.topicsToReview.append(topic);
            }
        }
        m_question->showEvaluation(evaluation);
        emit progressChanged();
    });
    // closed() means "move on": after Skip, or after the button that follows a result.
    connect(m_question, &QuizPopup::closed, this, [this]() {
        m_controller->dismiss();
        if (m_active) {
            askNext();
        }
    });
}

bool StudyPracticeView::begin(qint64 setId)
{
    if (!m_library || !m_library->isOpen()) {
        return false;
    }
    m_controller->skip();   // abandon anything still open
    const int total = m_provider->start(setId, enabledTypes(m_settings));
    if (total == 0) {
        return false;
    }
    m_setId = setId;
    m_summary = Summary();
    m_summary.total = total;
    m_active = true;
    m_pages->setCurrentIndex(0);
    askNext();
    return true;
}

void StudyPracticeView::askNext()
{
    if (!m_controller->ask()) {
        showSummary();
    }
}

void StudyPracticeView::updateProgress()
{
    m_progressLabel->setText(QStringLiteral("Question %1 of %2").arg(m_provider->asked()).arg(m_summary.total));
}

void StudyPracticeView::end()
{
    if (!m_active) {
        emit finished();
        return;
    }
    m_controller->skip();
    if (m_summary.answered > 0) {
        showSummary();
    } else {
        m_active = false;
        emit finished();
    }
}

void StudyPracticeView::abandon()
{
    m_controller->skip();
    m_active = false;
    m_pages->setCurrentIndex(0);
}

void StudyPracticeView::showSummary()
{
    m_active = true;   // stays "active" until the host leaves via Back
    QStringList lines;
    lines << QStringLiteral("%1 of %2 questions answered").arg(m_summary.answered).arg(m_summary.total);
    if (m_summary.answered > 0) {
        lines << QStringLiteral("%1 correct · %2 partly · %3 missed · %4% right")
                     .arg(m_summary.correct).arg(m_summary.partial).arg(m_summary.incorrect)
                     .arg(qRound(100.0 * m_summary.correct / m_summary.answered));
    }
    if (m_summary.skipped > 0) {
        lines << QStringLiteral("%1 skipped").arg(m_summary.skipped);
    }
    if (!m_summary.topicsToReview.isEmpty()) {
        lines << QStringLiteral("Worth another look: %1").arg(m_summary.topicsToReview.mid(0, 5).join(QStringLiteral(", ")));
    }
    m_summaryLabel->setText(lines.join(QLatin1Char('\n')));
    m_pages->setCurrentIndex(1);
    emit progressChanged();
}
