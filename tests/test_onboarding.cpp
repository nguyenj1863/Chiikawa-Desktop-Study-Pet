#include "AlarmScheduler.h"
#include "AppSettings.h"
#include "OnboardingPreview.h"
#include "OnboardingWindow.h"
#include "PetWindow.h"
#include "PixelTheme.h"
#include "SettingsPanel.h"
#include "StudyTimer.h"
#include "UISoundManager.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

class TestOnboarding : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void firstLaunchShowsOnboardingAndHidesPet();
    void tutorialNavigationProgressAndInactivePreviewPause();
    void returningUserBypassesOnboarding();
    void replayPreservesTimersAlarmsPositionAndCompletionState();
    void restoreDefaultsPreservesOnboardingCompleted();
};

void TestOnboarding::init()
{
    QSettings s(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    s.clear();
    s.sync();
}

void TestOnboarding::cleanup()
{
    QSettings s(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    s.clear();
    s.sync();
}

void TestOnboarding::firstLaunchShowsOnboardingAndHidesPet()
{
    PetWindow pet;
    QVERIFY(!pet.isVisible());
    QCOMPARE(pet.appSettings()->onboardingCompleted(), false);

    pet.startApplication();
    QApplication::processEvents();

    // Pet must stay hidden while first-launch onboarding is open
    QVERIFY(!pet.isVisible());
    QVERIFY(pet.onboardingWindow() != nullptr);
    QVERIFY(pet.onboardingWindow()->isVisible());
    QCOMPARE(pet.onboardingWindow()->currentPage(), static_cast<int>(OnboardingWindow::Welcome));
    QCOMPARE(pet.onboardingWindow()->isReplay(), false);

    // Skipping onboarding closes tutorial, persists completion, and reveals pet
    auto* skipBtn = pet.onboardingWindow()->findChild<QPushButton*>(QStringLiteral("onboardingSkip"));
    QVERIFY(skipBtn != nullptr);
    skipBtn->click();
    QApplication::processEvents();

    QVERIFY(!pet.onboardingWindow()->isVisible());
    QVERIFY(pet.isVisible());
    QCOMPARE(pet.appSettings()->onboardingCompleted(), true);

    AppSettings reloaded;
    QCOMPARE(reloaded.onboardingCompleted(), true);
}

void TestOnboarding::tutorialNavigationProgressAndInactivePreviewPause()
{
    UISoundManager uiSound;
    uiSound.setVolume(0.0);
    uiSound.resetPlayCount();

    OnboardingWindow win(&uiSound);
    win.openOnboarding(false);
    QApplication::processEvents();

    // Bundled SIL OFL fonts should load cleanly
    QVERIFY(!PixelTheme::displayFontFamily().isEmpty());
    QVERIFY(!PixelTheme::bodyFontFamily().isEmpty());

    QVERIFY(win.width() >= 340 && win.width() <= 520);
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::Welcome));

    // Step 1 click/double-click demonstration loop is slowed to 10-14 seconds
    const double interactDuration = win.previewForPage(OnboardingWindow::Interact)->cycleDurationMs();
    QVERIFY(interactDuration >= 10000.0 && interactDuration <= 14000.0);

    // Programmatic navigation must not emit click sounds
    win.goToPage(OnboardingWindow::Interact, false);
    QCOMPARE(uiSound.playCount(), 0);
    QCOMPARE(win.progressText(), QStringLiteral("1 / 4"));

    // Only the active step's preview should be active; all other previews must be stopped
    for (int i = 0; i < OnboardingWindow::PageCount; ++i) {
        OnboardingPreview* p = win.previewForPage(i);
        QVERIFY(p != nullptr);
        if (i == OnboardingWindow::Interact) {
            QVERIFY(p->isActive());
        } else {
            QVERIFY(!p->isActive());
            QVERIFY(!p->isAnimationTimerRunning());
        }
    }

    auto* primaryBtn = win.findChild<QPushButton*>(QStringLiteral("onboardingPrimary"));
    auto* backBtn = win.findChild<QPushButton*>(QStringLiteral("onboardingSecondary"));
    auto* skipBtn = win.findChild<QPushButton*>(QStringLiteral("onboardingSkip"));
    QVERIFY(primaryBtn != nullptr);
    QVERIFY(backBtn != nullptr);
    QVERIFY(skipBtn != nullptr);
    QCOMPARE(primaryBtn->height(), backBtn->height());
    QCOMPARE(primaryBtn->height(), skipBtn->height());

    // Advance through steps 2, 3, 4, Complete via user button clicks
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    primaryBtn->click();
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::Drag));
    QCOMPARE(win.progressText(), QStringLiteral("2 / 4"));
    QCOMPARE(uiSound.playCount(), 1);
    QVERIFY(!win.previewForPage(OnboardingWindow::Interact)->isActive());
    QVERIFY(win.previewForPage(OnboardingWindow::Drag)->isActive());

    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    primaryBtn->click();
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::StudyTimer));
    QCOMPARE(win.progressText(), QStringLiteral("3 / 4"));

    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    primaryBtn->click();
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::Alarm));
    QCOMPARE(win.progressText(), QStringLiteral("4 / 4"));

    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    primaryBtn->click();
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::Complete));
    QCOMPARE(primaryBtn->text(), QStringLiteral("Meet Chiikawa"));

    // Back returns to step 4
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    backBtn->click();
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::Alarm));

    // Advance back to Complete and click Meet Chiikawa -> emits onboardingCompleted
    QSignalSpy completedSpy(&win, &OnboardingWindow::onboardingCompleted);
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    primaryBtn->click();
    QTest::qWait(static_cast<int>(UISoundManager::kMinClickIntervalMs) + 15);
    primaryBtn->click();
    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.at(0).at(0).toBool(), false);
    QVERIFY(!win.isVisible());

    // After closing, all previews must be inactive
    for (int i = 0; i < OnboardingWindow::PageCount; ++i) {
        QVERIFY(!win.previewForPage(i)->isActive());
        QVERIFY(!win.previewForPage(i)->isAnimationTimerRunning());
    }

    // Escape triggers skip safely
    QSignalSpy skippedSpy(&win, &OnboardingWindow::onboardingSkipped);
    win.openOnboarding(true);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&win, &esc);
    QCOMPARE(skippedSpy.count(), 1);
    QCOMPARE(skippedSpy.at(0).at(0).toBool(), true);
}

void TestOnboarding::returningUserBypassesOnboarding()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.setPetSize(148);
        settings.save();
    }

    PetWindow pet;
    pet.startApplication();
    QApplication::processEvents();

    QVERIFY(pet.isVisible());
    QVERIFY(!pet.onboardingWindow()->isVisible());
    QCOMPARE(pet.appSettings()->petSize(), 148);
}

void TestOnboarding::replayPreservesTimersAlarmsPositionAndCompletionState()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.save();
    }

    PetWindow pet;
    pet.startApplication();
    QApplication::processEvents();

    // Start an active study timer and schedule an alarm before replaying tutorial
    pet.studyTimer()->configure(std::chrono::seconds{25 * 60});
    pet.studyTimer()->start();
    pet.alarmScheduler()->configureAlarm(QTime(23, 55, 0), true);
    const QPoint posBefore = pet.pos();

    QCOMPARE(pet.studyTimer()->state(), StudyTimer::State::Running);
    QCOMPARE(pet.alarmScheduler()->state(), AlarmScheduler::State::Scheduled);

    // Open replay (calling twice must not create duplicate windows)
    pet.showTutorialReplay();
    pet.showTutorialReplay();
    QApplication::processEvents();

    QVERIFY(pet.onboardingWindow()->isVisible());
    QCOMPARE(pet.onboardingWindow()->isReplay(), true);
    QCOMPARE(pet.studyTimer()->state(), StudyTimer::State::Running);
    QCOMPARE(pet.alarmScheduler()->state(), AlarmScheduler::State::Scheduled);

    // Close replay via Escape
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(pet.onboardingWindow(), &esc);
    QApplication::processEvents();

    QVERIFY(!pet.onboardingWindow()->isVisible());
    QCOMPARE(pet.appSettings()->onboardingCompleted(), true);
    QCOMPARE(pet.studyTimer()->state(), StudyTimer::State::Running);
    QCOMPARE(pet.alarmScheduler()->state(), AlarmScheduler::State::Scheduled);
    QCOMPARE(pet.pos(), posBefore);
}

void TestOnboarding::restoreDefaultsPreservesOnboardingCompleted()
{
    AppSettings settings;
    settings.setOnboardingCompleted(true);
    settings.setPetSize(180);
    settings.save();

    settings.restoreDefaults();
    QCOMPARE(settings.petSize(), SettingsDefaults::kPetSize);
    QCOMPARE(settings.onboardingCompleted(), true);

    AppSettings reloaded;
    QCOMPARE(reloaded.onboardingCompleted(), true);
}

QTEST_MAIN(TestOnboarding)
#include "test_onboarding.moc"
