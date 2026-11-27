#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "pet/PetWindow.h"
#include "study/StudyDatabase.h"
#include "timer/StudyTimer.h"
#include "ui/SettingsPanel.h"
#include "trivia/AnswerEvaluator.h"
#include "trivia/QuestionScheduler.h"
#include "trivia/QuizController.h"
#include "trivia/QuizPopup.h"
#include "trivia/TriviaQuestionProvider.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

namespace {
QString studyStorageDir()
{
    return QDir::tempPath() + QStringLiteral("/chiikawa-trivia-tests-%1").arg(QCoreApplication::applicationPid());
}

[[maybe_unused]] const bool kIsolatedSettingsStore = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetTriviaTests"));
    // PetWindow may open the Study Library; keep it out of the real app data folder.
    StudyDatabase::setStorageDirectory(studyStorageDir());
    return true;
}();

Question planetQuestion()
{
    Question q;
    q.id = QStringLiteral("test-planet");
    q.prompt = QStringLiteral("Name a planet.");
    q.category = QStringLiteral("Science");
    q.answers = {
        {QStringLiteral("Mars"), {}, Rarity::Common},
        {QStringLiteral("Neptune"), {}, Rarity::Uncommon},
        {QStringLiteral("Uranus"), {QStringLiteral("Ouranos")}, Rarity::Rare},
        {QStringLiteral("Bose-Einstein condensate"), {QStringLiteral("BEC")}, Rarity::Legendary},
    };
    return q;
}

/** Deterministic provider for controller tests. */
class FixedProvider : public QuestionProvider {
public:
    explicit FixedProvider(QVector<Question> questions) : m_questions(std::move(questions)) {}
    [[nodiscard]] QString id() const override { return QStringLiteral("fixed"); }
    std::optional<Question> nextQuestion(const QStringList& recentIds) override
    {
        for (const Question& q : m_questions) {
            if (!recentIds.contains(q.id)) {
                return q;
            }
        }
        if (m_questions.isEmpty()) {
            return std::nullopt;
        }
        return m_questions.first();
    }

private:
    QVector<Question> m_questions;
};
} // namespace

class TestTrivia : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    void bundledQuestionsLoad();
    void parserSkipsMalformedAndDuplicateEntries();
    void normalizesAnswers();
    void matchesAliasesAndScoresRanks();
    void handlesIncorrectEmptyAndDuplicateAnswers();
    void keepsSymbolLanguagesDistinct();
    void providerAvoidsRecentQuestions();
    void providerFallsBackWhenAllRecent();

    void schedulerDelayStaysWithinJitterBounds();
    void schedulerFiresAndStopCancels();
    void schedulerDefersWhileBusy();

    void controllerPersistsProgressAcrossReload();
    void controllerSkipHasNoPenalty();
    void controllerRejectsSecondQuestionWhileActive();
    void controllerTracksRecentWindow();
    void resetProgressClearsStats();
    void settingsPersistAndClampTriviaPreferences();
    void restoreDefaultsKeepsTriviaProgress();

    void popupLayoutStaysOnScreen();
    void popupLayoutPrefersAboveThenSides();
    void popupDismissAndSkipHide();
    void popupEscapeSkips();
    void popupAutoDismissesAfterResult();
    void popupDoesNotStealFocusOnShow();
    void popupDestroyedWhileVisibleIsSafe();

    void petSchedulesOnlyWhileEnabledAndStudying();
    void petBlocksCheckInsWhileBusy();
    void petAskNowShowsPopupAndShutsDownSafely();

private:
    static void clearSettings();
};

void TestTrivia::clearSettings()
{
    QSettings settings(AppSettings::storageOrganization(), AppSettings::storageApplication());
    settings.clear();
    settings.sync();
}

void TestTrivia::initTestCase()
{
    clearSettings();
}

void TestTrivia::cleanup()
{
    clearSettings();
}

void TestTrivia::cleanupTestCase()
{
    QDir(studyStorageDir()).removeRecursively();
}

void TestTrivia::bundledQuestionsLoad()
{
    TriviaQuestionProvider provider;
    const auto& questions = provider.questions();
    QVERIFY2(questions.size() >= 30, qPrintable(QString::number(questions.size())));

    QSet<QString> ids;
    QSet<QString> categories;
    for (const Question& q : questions) {
        QVERIFY(q.isValid());
        QVERIFY2(!ids.contains(q.id), qPrintable(q.id));
        ids.insert(q.id);
        categories.insert(q.category);
        QVERIFY2(q.answers.size() >= 3, qPrintable(q.id));

        // No two accepted answers (or aliases) of one question may normalize to the same text.
        QSet<QString> forms;
        for (const AcceptedAnswer& a : q.answers) {
            QStringList all = a.aliases;
            all.prepend(a.canonical);
            for (const QString& form : all) {
                const QString n = AnswerEvaluator::normalize(form);
                QVERIFY2(!n.isEmpty(), qPrintable(q.id + QStringLiteral(": ") + form));
                QVERIFY2(!forms.contains(n), qPrintable(q.id + QStringLiteral(": ") + form));
                forms.insert(n);
            }
        }
    }
    for (const char* c : {"Science", "Geography", "History", "Technology", "Mathematics", "General Knowledge"}) {
        QVERIFY2(categories.contains(QString::fromLatin1(c)), c);
    }
}

void TestTrivia::parserSkipsMalformedAndDuplicateEntries()
{
    QVERIFY(TriviaQuestionProvider::parse("not json").isEmpty());
    QVERIFY(TriviaQuestionProvider::parse("[]").isEmpty());

    const QByteArray json = R"({"questions": [
        {"id": "ok", "question": "Q?", "category": "C", "answers": [{"answer": "A", "rarity": "common"}]},
        {"id": "ok", "question": "Dup?", "answers": [{"answer": "B", "rarity": "rare"}]},
        {"id": "no-answers", "question": "Q?", "answers": []},
        {"id": "bad-rarity", "question": "Q?", "answers": [{"answer": "A", "rarity": "mythic"}]},
        {"question": "No id", "answers": [{"answer": "A", "rarity": "common"}]},
        {"id": "partial", "question": "Q?", "answers": [{"answer": "", "rarity": "common"}, {"answer": "Z", "rarity": "legendary"}]}
    ]})";
    const QVector<Question> parsed = TriviaQuestionProvider::parse(json);
    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).id, QStringLiteral("ok"));
    QCOMPARE(parsed.at(0).prompt, QStringLiteral("Q?"));
    QCOMPARE(parsed.at(1).id, QStringLiteral("partial"));
    QCOMPARE(parsed.at(1).answers.size(), 1);
    QCOMPARE(parsed.at(1).answers.first().rarity, Rarity::Legendary);

    TriviaQuestionProvider missing(QStringLiteral(":/does/not/exist.json"));
    QVERIFY(missing.questions().isEmpty());
    QVERIFY(!missing.nextQuestion({}).has_value());
}

void TestTrivia::normalizesAnswers()
{
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("  the  MARS ")), QStringLiteral("mars"));
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("North\tAmerica")), QStringLiteral("north america"));
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("Apéry's constant")), QStringLiteral("aperys constant"));
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("Ulysses S. Grant")), QStringLiteral("ulysses s grant"));
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("cherry-pick")), QStringLiteral("cherry pick"));
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("A Midsummer Night's Dream")),
             QStringLiteral("midsummer nights dream"));
    // A lone article is kept rather than normalized to nothing.
    QCOMPARE(AnswerEvaluator::normalize(QStringLiteral("a")), QStringLiteral("a"));
    QVERIFY(AnswerEvaluator::normalize(QStringLiteral("  ?! ")).isEmpty());
}

void TestTrivia::matchesAliasesAndScoresRanks()
{
    const Question q = planetQuestion();

    AnswerResult r = AnswerEvaluator::evaluate(q, QStringLiteral("mars"));
    QCOMPARE(r.rarity, Rarity::Common);
    QCOMPARE(r.points, 10);
    QCOMPARE(r.matchedCanonical, QStringLiteral("Mars"));

    r = AnswerEvaluator::evaluate(q, QStringLiteral("  NEPTUNE  "));
    QCOMPARE(r.rarity, Rarity::Uncommon);
    QCOMPARE(r.points, 30);

    r = AnswerEvaluator::evaluate(q, QStringLiteral("ouranos"));
    QCOMPARE(r.rarity, Rarity::Rare);
    QCOMPARE(r.points, 60);
    QCOMPARE(r.matchedCanonical, QStringLiteral("Uranus"));

    r = AnswerEvaluator::evaluate(q, QStringLiteral("bose einstein condensate"));
    QCOMPARE(r.rarity, Rarity::Legendary);
    QCOMPARE(r.points, 100);

    QCOMPARE(Trivia::pointsFor(Rarity::Incorrect), 0);
    QCOMPARE(Trivia::rarityName(Rarity::Legendary), QStringLiteral("Legendary"));
}

void TestTrivia::handlesIncorrectEmptyAndDuplicateAnswers()
{
    const Question q = planetQuestion();

    AnswerResult r = AnswerEvaluator::evaluate(q, QStringLiteral("Pluto"));
    QVERIFY(!r.isCorrect());
    QCOMPARE(r.points, 0);
    QVERIFY(!r.empty);

    r = AnswerEvaluator::evaluate(q, QStringLiteral("   "));
    QVERIFY(r.empty);
    QCOMPARE(r.points, 0);

    r = AnswerEvaluator::evaluate(q, QStringLiteral("mars"), {QStringLiteral("Mars")});
    QVERIFY(r.duplicate);
    QCOMPARE(r.points, 0);
    QCOMPARE(r.rarity, Rarity::Common);
}

void TestTrivia::keepsSymbolLanguagesDistinct()
{
    TriviaQuestionProvider provider;
    const Question* lang = nullptr;
    for (const Question& q : provider.questions()) {
        if (q.id == QStringLiteral("tech-programming-language")) {
            lang = &q;
        }
    }
    QVERIFY(lang);
    QCOMPARE(AnswerEvaluator::evaluate(*lang, QStringLiteral("c")).matchedCanonical, QStringLiteral("C"));
    QCOMPARE(AnswerEvaluator::evaluate(*lang, QStringLiteral("C++")).matchedCanonical, QStringLiteral("C++"));
    QCOMPARE(AnswerEvaluator::evaluate(*lang, QStringLiteral("c#")).matchedCanonical, QStringLiteral("C#"));
    QVERIFY(!AnswerEvaluator::evaluate(*lang, QStringLiteral("HTML")).isCorrect());
}

void TestTrivia::providerAvoidsRecentQuestions()
{
    TriviaQuestionProvider provider;
    provider.setSeed(1234);
    const auto& all = provider.questions();
    QVERIFY(all.size() > 3);

    QStringList recent;
    for (qsizetype i = 0; i < all.size() - 1; ++i) {
        recent.append(all.at(i).id);
    }
    for (int i = 0; i < 20; ++i) {
        const auto q = provider.nextQuestion(recent);
        QVERIFY(q.has_value());
        QCOMPARE(q->id, all.last().id);
    }
}

void TestTrivia::providerFallsBackWhenAllRecent()
{
    TriviaQuestionProvider provider;
    QStringList recent;
    for (const Question& q : provider.questions()) {
        recent.append(q.id);
    }
    const auto q = provider.nextQuestion(recent);
    QVERIFY(q.has_value());
    QCOMPARE(q->id, recent.first());
}

void TestTrivia::schedulerDelayStaysWithinJitterBounds()
{
    QuestionScheduler scheduler;
    scheduler.setSeed(7);
    scheduler.setBaseInterval(std::chrono::minutes(10));
    const auto base = std::chrono::milliseconds(std::chrono::minutes(10)).count();
    for (int i = 0; i < 200; ++i) {
        const auto d = scheduler.nextDelay().count();
        QVERIFY(d >= static_cast<long long>(base * QuestionScheduler::kMinJitter));
        QVERIFY(d <= static_cast<long long>(base * QuestionScheduler::kMaxJitter));
    }
}

void TestTrivia::schedulerFiresAndStopCancels()
{
    QuestionScheduler scheduler;
    scheduler.setBaseInterval(std::chrono::milliseconds(40));
    QSignalSpy spy(&scheduler, &QuestionScheduler::questionDue);

    scheduler.start();
    QVERIFY(scheduler.isRunning());
    QVERIFY(spy.wait(1000));

    scheduler.stop();
    QVERIFY(!scheduler.isRunning());
    const auto fired = spy.count();
    QTest::qWait(200);
    QCOMPARE(spy.count(), fired);
}

void TestTrivia::schedulerDefersWhileBusy()
{
    QuestionScheduler scheduler;
    scheduler.setBaseInterval(std::chrono::milliseconds(30));
    scheduler.setBusyRetry(std::chrono::milliseconds(30));
    bool busy = true;
    int checks = 0;
    scheduler.setCanAskPredicate([&]() {
        ++checks;
        return !busy;
    });
    QSignalSpy spy(&scheduler, &QuestionScheduler::questionDue);

    scheduler.start();
    QTRY_VERIFY_WITH_TIMEOUT(checks >= 2, 1000);
    QCOMPARE(spy.count(), 0);
    QVERIFY(scheduler.isRunning());

    busy = false;
    QVERIFY(spy.wait(1000));
    scheduler.stop();
}

void TestTrivia::controllerPersistsProgressAcrossReload()
{
    {
        AppSettings settings;
        QuizController controller(&settings);
        controller.addProvider(std::make_unique<FixedProvider>(QVector<Question>{planetQuestion()}));
        QSignalSpy evaluated(&controller, &QuizController::answerEvaluated);

        QVERIFY(controller.ask());
        QCOMPARE(controller.state(), QuizController::State::Asking);

        // Empty input does not consume the question.
        QVERIFY(controller.submit(QStringLiteral("  ")).empty);
        QCOMPARE(controller.state(), QuizController::State::Asking);

        const AnswerResult r = controller.submit(QStringLiteral("Uranus"));
        QCOMPARE(r.points, 60);
        QCOMPARE(evaluated.count(), 1);
        QCOMPARE(controller.state(), QuizController::State::Answered);
        controller.dismiss();
        QCOMPARE(controller.state(), QuizController::State::Idle);

        QVERIFY(controller.ask());
        QCOMPARE(controller.submit(QStringLiteral("Pluto")).points, 0);
        controller.dismiss();
    }
    AppSettings reloaded;
    QCOMPARE(reloaded.triviaTotalPoints(), 60);
    QCOMPARE(reloaded.triviaAnsweredCount(), 2);
    QCOMPARE(reloaded.triviaRecentIds(), QStringList{QStringLiteral("test-planet")});
}

void TestTrivia::controllerSkipHasNoPenalty()
{
    AppSettings settings;
    settings.setTriviaTotalPoints(40);
    settings.setTriviaAnsweredCount(3);
    QuizController controller(&settings);
    controller.addProvider(std::make_unique<FixedProvider>(QVector<Question>{planetQuestion()}));
    QSignalSpy finished(&controller, &QuizController::finished);

    QVERIFY(controller.ask());
    controller.skip();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(controller.state(), QuizController::State::Idle);
    QCOMPARE(controller.totalPoints(), 40);
    QCOMPARE(controller.answeredCount(), 3);
    // Submitting after a skip is ignored.
    QCOMPARE(controller.submit(QStringLiteral("Mars")).points, 0);
    QCOMPARE(controller.totalPoints(), 40);
}

void TestTrivia::controllerRejectsSecondQuestionWhileActive()
{
    AppSettings settings;
    QuizController controller(&settings);
    QVERIFY(!controller.ask()); // no providers
    controller.addProvider(std::make_unique<FixedProvider>(QVector<Question>{planetQuestion()}));
    QSignalSpy ready(&controller, &QuizController::questionReady);
    QVERIFY(controller.ask());
    QVERIFY(!controller.ask());
    QCOMPARE(ready.count(), 1);
}

void TestTrivia::controllerTracksRecentWindow()
{
    AppSettings settings;
    QuizController controller(&settings);
    controller.addProvider(std::make_unique<TriviaQuestionProvider>());

    QStringList asked;
    for (int i = 0; i < QuizController::kRecentWindow; ++i) {
        QVERIFY(controller.ask());
        const QString id = controller.currentQuestion()->id;
        QVERIFY2(!asked.contains(id), qPrintable(id));
        asked.append(id);
        controller.skip();
    }
    QCOMPARE(settings.triviaRecentIds(), asked);

    QVERIFY(controller.ask());
    QVERIFY(!asked.contains(controller.currentQuestion()->id));
    controller.skip();
    QCOMPARE(settings.triviaRecentIds().size(), QuizController::kRecentWindow);
}

void TestTrivia::resetProgressClearsStats()
{
    AppSettings settings;
    settings.setTriviaTotalPoints(120);
    settings.setTriviaAnsweredCount(4);
    settings.setTriviaRecentIds({QStringLiteral("x")});
    settings.save();

    QuizController controller(&settings);
    QSignalSpy changed(&controller, &QuizController::progressChanged);
    controller.resetProgress();
    QCOMPARE(changed.count(), 1);

    AppSettings reloaded;
    QCOMPARE(reloaded.triviaTotalPoints(), 0);
    QCOMPARE(reloaded.triviaAnsweredCount(), 0);
    QVERIFY(reloaded.triviaRecentIds().isEmpty());
}

void TestTrivia::settingsPersistAndClampTriviaPreferences()
{
    {
        AppSettings settings;
        QCOMPARE(settings.triviaEnabled(), SettingsDefaults::kTriviaEnabled);
        QVERIFY(!settings.triviaEnabled());
        QCOMPARE(settings.triviaIntervalMinutes(), SettingsDefaults::kTriviaIntervalMinutes);
        settings.setTriviaEnabled(true);
        settings.setTriviaIntervalMinutes(28);
        QCOMPARE(settings.triviaIntervalMinutes(), 30);
        settings.setTriviaTotalPoints(-5);
        QCOMPARE(settings.triviaTotalPoints(), 0);
        settings.save();
    }
    {
        AppSettings settings;
        QVERIFY(settings.triviaEnabled());
        QCOMPARE(settings.triviaIntervalMinutes(), 30);
    }
    {
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        raw.setValue(QStringLiteral("trivia/intervalMinutes"), 9999);
        raw.setValue(QStringLiteral("trivia/totalPoints"), -10);
        raw.sync();
    }
    AppSettings settings;
    QCOMPARE(settings.triviaIntervalMinutes(), SettingsDefaults::kMaxTriviaIntervalMinutes);
    QCOMPARE(settings.triviaTotalPoints(), 0);
}

void TestTrivia::restoreDefaultsKeepsTriviaProgress()
{
    AppSettings settings;
    settings.setTriviaEnabled(true);
    settings.setTriviaIntervalMinutes(5);
    settings.setTriviaTotalPoints(90);
    settings.setTriviaAnsweredCount(2);
    settings.restoreDefaults();

    QCOMPARE(settings.triviaEnabled(), SettingsDefaults::kTriviaEnabled);
    QCOMPARE(settings.triviaIntervalMinutes(), SettingsDefaults::kTriviaIntervalMinutes);
    QCOMPARE(settings.triviaTotalPoints(), 90);
    QCOMPARE(settings.triviaAnsweredCount(), 2);
}

void TestTrivia::popupLayoutStaysOnScreen()
{
    const QSize popup(QuizPopup::kWidth, 150);
    const QList<QRect> screens = {
        QRect(0, 0, 1920, 1040),
        QRect(0, 0, 1280, 680),       // 150% scaling of a 1920x1080 panel
        QRect(-1280, 0, 1280, 1024),  // secondary monitor on the left
        QRect(0, 0, 300, 200),        // tiny work area
    };
    for (const QRect& avail : screens) {
        const QList<QRect> pets = {
            QRect(avail.left(), avail.top(), 120, 120),
            QRect(avail.right() - 119, avail.top(), 120, 120),
            QRect(avail.left(), avail.bottom() - 119, 120, 120),
            QRect(avail.right() - 119, avail.bottom() - 119, 120, 120),
            QRect(avail.center().x() - 60, avail.center().y() - 60, 120, 120),
        };
        for (const QRect& pet : pets) {
            const QuizPopup::Layout l = QuizPopup::calculateLayout(pet, popup, avail);
            const QRect placed(l.pos, popup);
            if (popup.width() <= avail.width() && popup.height() <= avail.height()) {
                QVERIFY2(avail.contains(placed), qPrintable(QStringLiteral("%1,%2").arg(l.pos.x()).arg(l.pos.y())));
            } else {
                QCOMPARE(l.pos.x(), avail.left());
                QCOMPARE(l.pos.y(), avail.top());
            }
        }
    }
}

void TestTrivia::popupLayoutPrefersAboveThenSides()
{
    const QRect avail(0, 0, 1920, 1040);
    const QSize popup(QuizPopup::kWidth, 150);

    QuizPopup::Layout l = QuizPopup::calculateLayout(QRect(800, 600, 120, 120), popup, avail);
    QCOMPARE(l.placement, QuizPopup::Placement::Above);
    QCOMPARE(l.pos.y() + popup.height() + QuizPopup::kPetGapPx, 600);

    l = QuizPopup::calculateLayout(QRect(800, 0, 120, 120), popup, avail);
    QCOMPARE(l.placement, QuizPopup::Placement::Right);

    l = QuizPopup::calculateLayout(QRect(1800, 0, 120, 120), popup, avail);
    QCOMPARE(l.placement, QuizPopup::Placement::Left);

    l = QuizPopup::calculateLayout(QRect(100, 0, 120, 120), popup, QRect(0, 0, 360, 600));
    QCOMPARE(l.placement, QuizPopup::Placement::Below);
}

void TestTrivia::popupDismissAndSkipHide()
{
    QuizPopup popup;
    QSignalSpy closed(&popup, &QuizPopup::closed);
    QSignalSpy skipped(&popup, &QuizPopup::skipRequested);
    QSignalSpy submitted(&popup, &QuizPopup::answerSubmitted);

    popup.showQuestion(planetQuestion(), QRect(600, 500, 120, 120));
    QVERIFY(popup.isVisible());

    // Empty submit does nothing.
    popup.submitButton()->click();
    QCOMPARE(submitted.count(), 0);

    popup.answerEdit()->setText(QStringLiteral("Mars"));
    popup.submitButton()->click();
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.at(0).at(0).toString(), QStringLiteral("Mars"));

    popup.showResult(AnswerEvaluator::evaluate(planetQuestion(), QStringLiteral("Mars")), QStringLiteral("Eight planets."));
    QVERIFY(popup.isShowingResult());
    QVERIFY(popup.resultLabel()->text().contains(QStringLiteral("Common")));
    QVERIFY(popup.resultLabel()->text().contains(QStringLiteral("+10")));

    popup.closeButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
    QCOMPARE(closed.count(), 1);
    QCOMPARE(skipped.count(), 0); // closing a shown result is not a skip

    popup.showQuestion(planetQuestion(), QRect(600, 500, 120, 120));
    popup.skipButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
    QCOMPARE(skipped.count(), 1);
    QCOMPARE(closed.count(), 2);
}

void TestTrivia::popupEscapeSkips()
{
    QuizPopup popup;
    QSignalSpy skipped(&popup, &QuizPopup::skipRequested);
    popup.showQuestion(planetQuestion(), QRect(600, 500, 120, 120));
    QTest::keyClick(popup.answerEdit(), Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
    QCOMPARE(skipped.count(), 1);
}

void TestTrivia::popupAutoDismissesAfterResult()
{
    QuizPopup popup;
    QSignalSpy closed(&popup, &QuizPopup::closed);
    popup.showQuestion(planetQuestion(), QRect(600, 500, 120, 120));
    // A question waits for the user; it never auto-dismisses on its own.
    QCOMPARE(popup.remainingDismissMs(), 0);

    popup.showResult(AnswerEvaluator::evaluate(planetQuestion(), QStringLiteral("Pluto")), QString());
    QVERIFY(popup.resultLabel()->text().contains(QStringLiteral("+0")));
    const int remaining = popup.remainingDismissMs();
    QVERIFY(remaining > 0 && remaining <= QuizPopup::kResultDisplayMs);
    QTRY_VERIFY_WITH_TIMEOUT(closed.count() == 1, QuizPopup::kResultDisplayMs + 2000);
    QVERIFY(!popup.isVisible());
}

void TestTrivia::popupDoesNotStealFocusOnShow()
{
    QWidget host;
    host.resize(200, 100);
    host.show();
    host.activateWindow();
    if (!QTest::qWaitForWindowActive(&host)) {
        QSKIP("Window activation is unavailable on this platform plugin");
    }

    QuizPopup popup;
    popup.showQuestion(planetQuestion(), QRect(600, 500, 120, 120));
    QTest::qWait(100);
    QCOMPARE(QApplication::activeWindow(), &host);
    QVERIFY(!popup.answerEdit()->hasFocus());

    // A deliberate click on the answer field brings focus into the popup.
    QTest::mouseClick(popup.answerEdit(), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(popup.answerEdit()->hasFocus(), 1000);
}

void TestTrivia::popupDestroyedWhileVisibleIsSafe()
{
    AppSettings settings;
    auto controller = std::make_unique<QuizController>(&settings);
    controller->addProvider(std::make_unique<FixedProvider>(QVector<Question>{planetQuestion()}));
    auto popup = std::make_unique<QuizPopup>();
    QObject::connect(controller.get(), &QuizController::questionReady, popup.get(),
                     [&popup](const Question& q) { popup->showQuestion(q, QRect(600, 500, 120, 120)); });
    QVERIFY(controller->ask());
    QVERIFY(popup->isVisible());
    popup->dismiss(); // mid-fade
    popup.reset();
    controller.reset();
    QTest::qWait(300);
}

void TestTrivia::petSchedulesOnlyWhileEnabledAndStudying()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.save();
    }
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();
    QuestionScheduler* scheduler = pet.questionScheduler();
    QVERIFY(scheduler);

    // Opt-in: a running timer alone does not schedule check-ins.
    pet.studyTimer()->configure(std::chrono::minutes(25));
    pet.studyTimer()->start();
    QVERIFY(!scheduler->isRunning());

    pet.appSettings()->setTriviaEnabled(true);
    pet.appSettings()->setTriviaIntervalMinutes(10);
    pet.applySettings();
    QVERIFY(scheduler->isRunning());
    QCOMPARE(scheduler->baseInterval(), std::chrono::milliseconds(std::chrono::minutes(10)));

    pet.studyTimer()->pause();
    QVERIFY(!scheduler->isRunning());
    pet.studyTimer()->resume();
    QVERIFY(scheduler->isRunning());

    pet.appSettings()->setTriviaEnabled(false);
    pet.applySettings();
    QVERIFY(!scheduler->isRunning());

    pet.appSettings()->setTriviaEnabled(true);
    pet.applySettings();
    QVERIFY(scheduler->isRunning());
    pet.studyTimer()->cancel();
    QVERIFY(!scheduler->isRunning());
}

void TestTrivia::petBlocksCheckInsWhileBusy()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.setStudyMode(SettingsDefaults::kStudyModeTrivia);   // general trivia questions
        settings.save();
    }
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();
    QVERIFY(pet.isVisible());
    QVERIFY(pet.canShowTriviaCheckIn());

    pet.openSettings();
    QApplication::processEvents();
    QVERIFY(!pet.canShowTriviaCheckIn());
    SettingsPanel* panel = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* p = qobject_cast<SettingsPanel*>(w)) {
            panel = p;
        }
    }
    QVERIFY(panel);
    panel->hide();
    QApplication::processEvents();
    QVERIFY(pet.canShowTriviaCheckIn());

    pet.askTriviaNow();
    QVERIFY(pet.quizPopup()->isVisible());
    QVERIFY(!pet.canShowTriviaCheckIn());

    pet.hidePet();
    QTRY_VERIFY_WITH_TIMEOUT(!pet.quizPopup()->isVisible(), 1000);
    QVERIFY(!pet.canShowTriviaCheckIn());
}

void TestTrivia::petAskNowShowsPopupAndShutsDownSafely()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.setStudyMode(SettingsDefaults::kStudyModeTrivia);   // general trivia questions
        settings.save();
    }
    auto pet = std::make_unique<PetWindow>();
    pet->showPet();
    QApplication::processEvents();

    pet->askTriviaNow();
    QuizPopup* popup = pet->quizPopup();
    QVERIFY(popup->isVisible());
    QCOMPARE(pet->quizController()->state(), QuizController::State::Asking);

    // A second request while one is open is ignored.
    const QString firstId = pet->quizController()->currentQuestion()->id;
    pet->askTriviaNow();
    QCOMPARE(pet->quizController()->currentQuestion()->id, firstId);

    // Answer through the popup and confirm progress reaches settings.
    const Question q = *pet->quizController()->currentQuestion();
    popup->answerEdit()->setText(q.answers.first().canonical);
    popup->submitButton()->click();
    QVERIFY(popup->isShowingResult());
    QCOMPARE(pet->appSettings()->triviaAnsweredCount(), 1);
    QCOMPARE(pet->appSettings()->triviaTotalPoints(), Trivia::pointsFor(q.answers.first().rarity));

    popup->closeButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(pet->quizController()->state(), QuizController::State::Idle, 1000);

    // Destroying the composition root while a question is open must not crash.
    pet->askTriviaNow();
    QVERIFY(pet->quizPopup()->isVisible());
    pet.reset();
    QTest::qWait(250);
}

QTEST_MAIN(TestTrivia)
#include "test_trivia.moc"
