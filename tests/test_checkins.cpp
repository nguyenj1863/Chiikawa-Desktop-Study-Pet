#include "alarm/AlarmNotification.h"
#include "alarm/AlarmScheduler.h"
#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "pet/PetWindow.h"
#include "study/practice/QuestionScheduler.h"
#include "study/practice/QuizController.h"
#include "study/StudyDatabase.h"
#include "study/StudyLibrary.h"
#include "timer/StudyTimer.h"
#include "ui/PixelTheme.h"
#include "studyui/QuizPopup.h"
#include "ui/SettingsPanel.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

namespace {
QString studyStorageDir()
{
    return QDir::tempPath() + QStringLiteral("/chiikawa-checkin-tests-%1").arg(QCoreApplication::applicationPid());
}

[[maybe_unused]] const bool kIsolatedStores = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetCheckInTests"));
    // PetWindow may open the Study Library; keep it out of the real app data folder.
    StudyDatabase::setStorageDirectory(studyStorageDir());
    return true;
}();

const QRect kPet(600, 500, 120, 120);

Question joinQuestion(const QString& id = QStringLiteral("q1"))
{
    Question q;
    q.id = id;
    q.type = QuestionType::SingleChoice;
    q.prompt = QStringLiteral("What does a LEFT JOIN return?");
    q.category = QStringLiteral("Databases");
    q.topic = QStringLiteral("LEFT JOIN");
    q.explanation = QStringLiteral("A LEFT JOIN keeps every row from the left table.");
    q.options = {
        {QStringLiteral("a"), QStringLiteral("Only matching rows"), false, QStringLiteral("That is an INNER JOIN.")},
        {QStringLiteral("b"), QStringLiteral("All left rows plus matching right rows"), true, QStringLiteral("Every left row is kept.")},
        {QStringLiteral("c"), QStringLiteral("All rows from both tables"), false, QStringLiteral("Full outer join.")},
        {QStringLiteral("d"), QStringLiteral("Only unmatched rows"), false, QStringLiteral("Matches are kept too.")},
    };
    q.sources.append({QStringLiteral("Lecture 6"), QStringLiteral("slide 3"), QStringLiteral("A LEFT JOIN returns every row from the left table.")});
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

class TestCheckIns : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    void schedulerDelayStaysWithinJitterBounds();
    void schedulerFiresAndStopCancels();
    void schedulerDefersWhileBusy();

    void controllerRejectsSecondQuestionWhileActive();
    void controllerSkipLeavesNoRecordAndIgnoresLateAnswers();
    void controllerTracksRecentWindow();
    void resetProgressClearsRecentQuestions();
    void checkInPreferencesPersistClampAndMigrate();
    void restoreDefaultsResetsCheckIns();

    void popupLayoutStaysOnScreen();
    void popupLayoutPrefersAboveThenSides();
    void popupSkipAndCloseHide();
    void popupEscapeSkips();
    void popupDestroyedWhileVisibleIsSafe();

    void petSchedulesOnlyWhileEnabledAndStudying();
    void petBlocksCheckInsWhileBusy();
    void petAlarmUsesTheCustomPopupAndRespectsTheSetting();
    void petShutsDownSafelyWithOpenQuestion();

private:
    static void clearSettings();
};

void TestCheckIns::clearSettings()
{
    QSettings settings(AppSettings::storageOrganization(), AppSettings::storageApplication());
    settings.clear();
    settings.sync();
}

void TestCheckIns::initTestCase()
{
    clearSettings();
}

void TestCheckIns::cleanup()
{
    clearSettings();
}

void TestCheckIns::cleanupTestCase()
{
    QDir(studyStorageDir()).removeRecursively();
}

void TestCheckIns::schedulerDelayStaysWithinJitterBounds()
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

void TestCheckIns::schedulerFiresAndStopCancels()
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

void TestCheckIns::schedulerDefersWhileBusy()
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

void TestCheckIns::controllerRejectsSecondQuestionWhileActive()
{
    AppSettings settings;
    QuizController controller(&settings);
    QVERIFY(!controller.ask()); // no providers
    controller.addProvider(std::make_unique<FixedProvider>(QVector<Question>{joinQuestion()}));
    QSignalSpy ready(&controller, &QuizController::questionReady);
    QVERIFY(controller.ask());
    QVERIFY(!controller.ask());
    QCOMPARE(ready.count(), 1);
    QCOMPARE(controller.state(), QuizController::State::Asking);
}

void TestCheckIns::controllerSkipLeavesNoRecordAndIgnoresLateAnswers()
{
    AppSettings settings;
    QuizController controller(&settings);
    controller.addProvider(std::make_unique<FixedProvider>(QVector<Question>{joinQuestion()}));
    QSignalSpy finished(&controller, &QuizController::finished);
    QSignalSpy evaluated(&controller, &QuizController::evaluationReady);

    QVERIFY(controller.ask());
    controller.skip();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(controller.state(), QuizController::State::Idle);
    // An answer arriving after a skip is ignored.
    controller.submitChoice(QStringLiteral("b"));
    QCOMPARE(evaluated.count(), 0);
}

void TestCheckIns::controllerTracksRecentWindow()
{
    AppSettings settings;
    QuizController controller(&settings);
    QVector<Question> pool;
    for (int i = 0; i < 14; ++i) {
        pool.append(joinQuestion(QStringLiteral("q%1").arg(i)));
    }
    controller.addProvider(std::make_unique<FixedProvider>(pool));

    QStringList asked;
    for (int i = 0; i < QuizController::kRecentWindow; ++i) {
        QVERIFY(controller.ask());
        const QString id = controller.currentQuestion()->id;
        QVERIFY2(!asked.contains(id), qPrintable(id));
        asked.append(id);
        controller.skip();
    }
    QCOMPARE(settings.recentQuestionIds(), asked);

    QVERIFY(controller.ask());
    QVERIFY(!asked.contains(controller.currentQuestion()->id));
    controller.skip();
    QCOMPARE(settings.recentQuestionIds().size(), QuizController::kRecentWindow);
}

void TestCheckIns::resetProgressClearsRecentQuestions()
{
    AppSettings settings;
    settings.setRecentQuestionIds({QStringLiteral("x")});
    settings.save();

    QuizController controller(&settings);
    QSignalSpy changed(&controller, &QuizController::progressChanged);
    controller.resetProgress();
    QCOMPARE(changed.count(), 1);

    AppSettings reloaded;
    QVERIFY(reloaded.recentQuestionIds().isEmpty());
}

void TestCheckIns::checkInPreferencesPersistClampAndMigrate()
{
    {
        AppSettings settings;
        QCOMPARE(settings.checkInsEnabled(), SettingsDefaults::kCheckInsEnabled);
        QVERIFY(!settings.checkInsEnabled());   // opt-in
        QCOMPARE(settings.checkInMinutes(), SettingsDefaults::kCheckInMinutes);
        settings.setCheckInsEnabled(true);
        settings.setCheckInMinutes(28);
        QCOMPARE(settings.checkInMinutes(), 30);
        settings.save();
    }
    {
        AppSettings settings;
        QVERIFY(settings.checkInsEnabled());
        QCOMPARE(settings.checkInMinutes(), 30);
    }
    {
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        raw.setValue(QStringLiteral("study/checkinMinutes"), 9999);
        raw.sync();
    }
    {
        AppSettings settings;
        QCOMPARE(settings.checkInMinutes(), SettingsDefaults::kMaxCheckInMinutes);
    }

    // Settings saved before the Study App used trivia/* keys; they are read once and then retired.
    clearSettings();
    {
        QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
        raw.setValue(QStringLiteral("trivia/enabled"), true);
        raw.setValue(QStringLiteral("trivia/intervalMinutes"), 10);
        raw.setValue(QStringLiteral("trivia/recentIds"), QStringList{QStringLiteral("a"), QStringLiteral("b")});
        raw.setValue(QStringLiteral("trivia/totalPoints"), 90);
        raw.setValue(QStringLiteral("study/mode"), 1);
        raw.setValue(QStringLiteral("study/courseId"), 5);   // "course" was renamed "study set"
        raw.sync();
    }
    AppSettings migrated;
    QVERIFY(migrated.checkInsEnabled());
    QCOMPARE(migrated.checkInMinutes(), 10);
    QCOMPARE(migrated.studySetId(), 5);
    QCOMPARE(migrated.recentQuestionIds(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    migrated.save();
    QSettings raw(AppSettings::storageOrganization(), AppSettings::storageApplication());
    QVERIFY(!raw.contains(QStringLiteral("trivia/enabled")));
    QVERIFY(!raw.contains(QStringLiteral("trivia/totalPoints")));
    QVERIFY(!raw.contains(QStringLiteral("study/mode")));
    QVERIFY(!raw.contains(QStringLiteral("study/courseId")));
    QCOMPARE(raw.value(QStringLiteral("study/setId")).toInt(), 5);
    QVERIFY(raw.value(QStringLiteral("study/checkinsEnabled")).toBool());
    QCOMPARE(raw.value(QStringLiteral("study/checkinMinutes")).toInt(), 10);
}

void TestCheckIns::restoreDefaultsResetsCheckIns()
{
    AppSettings settings;
    settings.setCheckInsEnabled(true);
    settings.setCheckInMinutes(5);
    settings.setStudySetId(7);
    settings.restoreDefaults();

    QCOMPARE(settings.checkInsEnabled(), SettingsDefaults::kCheckInsEnabled);
    QCOMPARE(settings.checkInMinutes(), SettingsDefaults::kCheckInMinutes);
    QCOMPARE(settings.studySetId(), 7);   // the chosen study set survives Restore Defaults
}

void TestCheckIns::popupLayoutStaysOnScreen()
{
    const QSize popup(QuizPopup::kStudyWidth, 150);
    const QList<QRect> screens = {
        QRect(0, 0, 1920, 1040),
        QRect(0, 0, 1280, 680),       // 150% scaling of a 1920x1080 panel
        QRect(-1280, 0, 1280, 1024),  // secondary monitor on the left
        QRect(0, 0, 340, 200),        // tiny work area
        QRect(0, 0, 300, 200),        // narrower than the popup
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
            // The popup stays inside the work area on every axis where it fits, and is pinned to
            // the top-left edge on an axis where it is larger than the work area.
            if (popup.width() <= avail.width()) {
                QVERIFY2(placed.left() >= avail.left() && placed.right() <= avail.right(),
                         qPrintable(QStringLiteral("x %1").arg(l.pos.x())));
            } else {
                QCOMPARE(l.pos.x(), avail.left());
            }
            if (popup.height() <= avail.height()) {
                QVERIFY2(placed.top() >= avail.top() && placed.bottom() <= avail.bottom(),
                         qPrintable(QStringLiteral("y %1").arg(l.pos.y())));
            } else {
                QCOMPARE(l.pos.y(), avail.top());
            }
        }
    }
}

void TestCheckIns::popupLayoutPrefersAboveThenSides()
{
    const QRect avail(0, 0, 1920, 1040);
    const QSize popup(QuizPopup::kStudyWidth, 150);

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

void TestCheckIns::popupSkipAndCloseHide()
{
    QuizPopup popup;
    QSignalSpy closed(&popup, &QuizPopup::closed);
    QSignalSpy skipped(&popup, &QuizPopup::skipRequested);
    QSignalSpy chosen(&popup, &QuizPopup::choiceSubmitted);

    popup.showQuestion(joinQuestion(), kPet);
    QVERIFY(popup.isVisible());
    QVERIFY(!popup.submitButton()->isEnabled());   // nothing chosen yet

    popup.optionButtons().at(1)->click();
    popup.submitButton()->click();
    QCOMPARE(chosen.count(), 1);

    Evaluation e;
    e.verdict = Verdict::Correct;
    e.score = 1.0;
    popup.showEvaluation(e);
    QVERIFY(popup.isShowingResult());
    popup.closeButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
    QCOMPARE(closed.count(), 1);
    QCOMPARE(skipped.count(), 0);   // closing a shown result is not a skip

    popup.showQuestion(joinQuestion(), kPet);
    popup.skipButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
    QCOMPARE(skipped.count(), 1);
    QCOMPARE(closed.count(), 2);
}

void TestCheckIns::popupEscapeSkips()
{
    Question q = joinQuestion();
    q.type = QuestionType::Written;
    q.expectedAnswer = QStringLiteral("All left rows");
    QuizPopup popup;
    QSignalSpy skipped(&popup, &QuizPopup::skipRequested);
    popup.showQuestion(q, kPet);
    QTest::keyClick(popup.writtenEdit(), Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
    QCOMPARE(skipped.count(), 1);
}

void TestCheckIns::popupDestroyedWhileVisibleIsSafe()
{
    AppSettings settings;
    auto controller = std::make_unique<QuizController>(&settings);
    controller->addProvider(std::make_unique<FixedProvider>(QVector<Question>{joinQuestion()}));
    auto popup = std::make_unique<QuizPopup>();
    QObject::connect(controller.get(), &QuizController::questionReady, popup.get(),
                     [&popup](const Question& q) { popup->showQuestion(q, kPet); });
    QVERIFY(controller->ask());
    QVERIFY(popup->isVisible());
    popup->dismiss(); // mid-fade
    popup.reset();
    controller.reset();
    QTest::qWait(300);
}

void TestCheckIns::petSchedulesOnlyWhileEnabledAndStudying()
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

    pet.appSettings()->setCheckInsEnabled(true);
    pet.appSettings()->setCheckInMinutes(10);
    pet.applySettings();
    QVERIFY(scheduler->isRunning());
    QCOMPARE(scheduler->baseInterval(), std::chrono::milliseconds(std::chrono::minutes(10)));

    pet.studyTimer()->pause();
    QVERIFY(!scheduler->isRunning());
    pet.studyTimer()->resume();
    QVERIFY(scheduler->isRunning());

    pet.appSettings()->setCheckInsEnabled(false);
    pet.applySettings();
    QVERIFY(!scheduler->isRunning());

    pet.appSettings()->setCheckInsEnabled(true);
    pet.applySettings();
    QVERIFY(scheduler->isRunning());
    pet.studyTimer()->cancel();
    QVERIFY(!scheduler->isRunning());
}

void TestCheckIns::petAlarmUsesTheCustomPopupAndRespectsTheSetting()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.setSoundEnabled(false);
        settings.setAlarmNotificationEnabled(true);
        settings.save();
    }
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();
    QVERIFY(pet.alarmNotification());
    QVERIFY(!pet.alarmNotification()->isVisible());

    // An alarm rings with the custom Chiikawa popup (snooze and dismiss are its buttons).
    emit pet.alarmScheduler()->alarmTriggered(QTime(8, 0), false);
    QApplication::processEvents();
    QVERIFY(pet.alarmNotification()->isVisible());
    pet.alarmNotification()->closeNotification();
    QApplication::processEvents();

    // With the popup switched off nothing appears, and no OS toast replaces it.
    pet.appSettings()->setAlarmNotificationEnabled(false);
    emit pet.alarmScheduler()->alarmTriggered(QTime(8, 1), false);
    QApplication::processEvents();
    QVERIFY(!pet.alarmNotification()->isVisible());
}

void TestCheckIns::petBlocksCheckInsWhileBusy()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.save();
    }
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();
    QVERIFY(pet.isVisible());
    QVERIFY(pet.canShowCheckIn());

    pet.openSettings();
    QApplication::processEvents();
    QVERIFY(!pet.canShowCheckIn());
    SettingsPanel* panel = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* p = qobject_cast<SettingsPanel*>(w)) {
            panel = p;
        }
    }
    QVERIFY(panel);
    panel->hide();
    QApplication::processEvents();
    QVERIFY(pet.canShowCheckIn());

    // With an empty library the manual request shows a notice, which also blocks scheduled check-ins.
    pet.askQuestionNow();
    QVERIFY(pet.quizPopup()->isVisible());
    QVERIFY(!pet.canShowCheckIn());

    pet.hidePet();
    QTRY_VERIFY_WITH_TIMEOUT(!pet.quizPopup()->isVisible(), 1000);
    QVERIFY(!pet.canShowCheckIn());
}

void TestCheckIns::petShutsDownSafelyWithOpenQuestion()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.save();
    }
    auto pet = std::make_unique<PetWindow>();
    pet->showPet();
    QApplication::processEvents();

    // Seed one study question directly (generation itself is covered elsewhere).
    pet->askQuestionNow();   // creates the study stack and shows the empty-library notice
    StudyDatabase* db = pet->studyLibrary()->database();
    const qint64 studySet = *db->addStudySet(QStringLiteral("Databases"));
    const qint64 doc = *db->addDocument(studySet, QStringLiteral("Lecture 6"), QStringLiteral("l6.pptx"), DocumentKind::Pptx);
    Question q = joinQuestion();
    q.setId = studySet;
    q.documentId = doc;
    QVERIFY(db->saveQuestion(q, 0, QString()));
    pet->appSettings()->setStudySetId(studySet);
    pet->quizPopup()->dismiss();
    QTRY_VERIFY_WITH_TIMEOUT(!pet->quizPopup()->isVisible(), 1000);
    QTest::qWait(100);

    pet->askQuestionNow();
    QuizPopup* popup = pet->quizPopup();
    QVERIFY(popup->isVisible());
    QCOMPARE(pet->quizController()->state(), QuizController::State::Asking);
    QCOMPARE(popup->phase(), QuizPopup::Phase::Asking);

    // A second request while one is open is ignored.
    const QString firstId = pet->quizController()->currentQuestion()->id;
    pet->askQuestionNow();
    QCOMPARE(pet->quizController()->currentQuestion()->id, firstId);

    // Destroying the composition root while a question is open must not crash.
    pet.reset();
    QTest::qWait(250);
}

QTEST_MAIN(TestCheckIns)
#include "test_checkins.moc"
