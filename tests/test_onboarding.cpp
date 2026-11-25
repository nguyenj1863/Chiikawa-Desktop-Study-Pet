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
#include <QImage>
#include <QPainter>
#include <QKeyEvent>
#include <QLabel>
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
    void returningUserStillShowsOnboardingOnLaunch();
    void replayPreservesTimersAlarmsPositionAndCompletionState();
    void restoreDefaultsPreservesOnboardingCompleted();
    void fireAndSkipInteractiveHitRegions();
    void petWindowDisablesNativeDropShadow();
    void typographyPreservedAfterDarkGlassRedesign();
    void glassMaterialAlphaRanges();
    void glassCornersStayTransparent();
    void glassWindowsAreTranslucent();
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

    // Bundled SIL OFL fonts should load cleanly, keeping Pixelify Sans as display font
    QCOMPARE(PixelTheme::displayFontFamily(), QStringLiteral("Pixelify Sans"));
    QVERIFY(!PixelTheme::bodyFontFamily().isEmpty());
    QVERIFY(!PixelTheme::icon(PixelIcon::Gear, PixelTheme::kPrimaryText, 18).isNull());

    // Welcome screen includes "Study Pet" app title and exact Welcome-only creator footer
    auto* appTitle = win.findChild<QLabel*>(QStringLiteral("onboardingAppTitle"));
    QVERIFY(appTitle != nullptr);
    QCOMPARE(appTitle->text(), QStringLiteral("Study Pet"));
    QVERIFY(appTitle->isVisible());

    auto* footer = win.findChild<QLabel*>(QStringLiteral("onboardingFooter"));
    QVERIFY(footer != nullptr);
    QCOMPARE(footer->text(), QStringLiteral("\u00A9 2026 John Nguyen \u2661"));
    QVERIFY(footer->isVisible());

    QVERIFY(win.width() >= 340 && win.width() <= 520);
    QCOMPARE(win.currentPage(), static_cast<int>(OnboardingWindow::Welcome));

    // Welcome twinkling star animation runs when active (unless OS reduced motion is enabled)
    if (!OnboardingPreview::systemPrefersReducedMotion()) {
        QVERIFY(win.previewForPage(OnboardingWindow::Welcome)->isAnimationTimerRunning());
    }

    // Step 1 3-stage click/double-click/gear-click loop is 13-17 seconds
    const double interactDuration = win.previewForPage(OnboardingWindow::Interact)->cycleDurationMs();
    QVERIFY(interactDuration >= 13000.0 && interactDuration <= 17000.0);

    // Programmatic navigation must not emit click sounds and hides Welcome-only footer
    win.goToPage(OnboardingWindow::Interact, false);
    QCOMPARE(uiSound.playCount(), 0);
    QCOMPARE(win.progressText(), QStringLiteral("1 / 4"));
    QVERIFY(!footer->isVisible());

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

void TestOnboarding::returningUserStillShowsOnboardingOnLaunch()
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

    QVERIFY(!pet.isVisible());
    QVERIFY(pet.onboardingWindow()->isVisible());
    QCOMPARE(pet.onboardingWindow()->isReplay(), false);
    QCOMPARE(pet.appSettings()->petSize(), 148);

    auto* skipBtn = pet.onboardingWindow()->findChild<QPushButton*>(QStringLiteral("onboardingSkip"));
    QVERIFY(skipBtn != nullptr);
    skipBtn->click();
    QApplication::processEvents();

    QVERIFY(!pet.onboardingWindow()->isVisible());
    QVERIFY(pet.isVisible());
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

    // Dismiss launch onboarding so the pet is visible before testing replay
    auto* skipBtn = pet.onboardingWindow()->findChild<QPushButton*>(QStringLiteral("onboardingSkip"));
    QVERIFY(skipBtn != nullptr);
    skipBtn->click();
    QApplication::processEvents();
    QVERIFY(pet.isVisible());

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

void TestOnboarding::fireAndSkipInteractiveHitRegions()
{
    const QSize winSize(120, 120);

    // Fire: small centered opaque sprite -> hit rect expands into surrounding negative space
    QImage fireFrame(120, 120, QImage::Format_ARGB32);
    fireFrame.fill(Qt::transparent);
    for (int y = 45; y < 75; ++y) {
        for (int x = 45; x < 75; ++x) {
            fireFrame.setPixelColor(x, y, QColor(255, 120, 80, 255));
        }
    }
    const QRect fireHit = PetWindow::computeInteractiveHitRect(fireFrame, AnimationId::Fire, winSize);
    QVERIFY(!fireHit.isEmpty());
    QVERIFY(fireHit.contains(QPoint(35, 60))); // negative space around small fire sprite
    QVERIFY(fireHit.contains(QPoint(85, 60)));
    QVERIFY(QRect(QPoint(0, 0), winSize).contains(fireHit));

    // Skip: two separated positive regions -> gap between them is included, outer margin is excluded
    QImage skipFrame(120, 120, QImage::Format_ARGB32);
    skipFrame.fill(Qt::transparent);
    for (int y = 30; y < 90; ++y) {
        for (int x = 20; x < 45; ++x) {
            skipFrame.setPixelColor(x, y, QColor(255, 255, 255, 255));
        }
        for (int x = 75; x < 100; ++x) {
            skipFrame.setPixelColor(x, y, QColor(255, 255, 255, 255));
        }
    }
    const QRect skipHit = PetWindow::computeInteractiveHitRect(skipFrame, AnimationId::Skip, winSize);
    QVERIFY(!skipHit.isEmpty());
    QVERIFY(skipHit.contains(QPoint(60, 60)));  // gap between the two positive spaces
    QVERIFY(!skipHit.contains(QPoint(5, 5)));   // outer background remains click-through

    // Stomp and Twirl keep default per-pixel alpha behavior (no extra hit rect)
    QVERIFY(PetWindow::computeInteractiveHitRect(skipFrame, AnimationId::Stomp, winSize).isEmpty());
    QVERIFY(PetWindow::computeInteractiveHitRect(skipFrame, AnimationId::Twirl, winSize).isEmpty());
}

void TestOnboarding::petWindowDisablesNativeDropShadow()
{
    AppSettings settings;
    settings.setOnboardingCompleted(true);
    settings.save();

    PetWindow pet;
    QVERIFY(pet.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(pet.windowFlags().testFlag(Qt::NoDropShadowWindowHint));
}

void TestOnboarding::typographyPreservedAfterDarkGlassRedesign()
{
    // Font families must remain identical to original typography
    QCOMPARE(PixelTheme::displayFontFamily(), QStringLiteral("Pixelify Sans"));
    QCOMPARE(PixelTheme::bodyFontFamily(), QStringLiteral("Nunito"));

    const QString qss = PixelTheme::styleSheet();

    // Preserve original typography sizes, weights, and font families
    QVERIFY(qss.contains(QStringLiteral("font-family: 'Pixelify Sans', 'Segoe UI', sans-serif;")));
    QVERIFY(qss.contains(QStringLiteral("font-family: 'Nunito', 'Segoe UI', 'Yu Gothic UI', sans-serif;")));
    QVERIFY(qss.contains(QStringLiteral("font-family: 'Cascadia Mono', 'Consolas', 'Segoe UI', monospace;")));
    QVERIFY(qss.contains(QStringLiteral("font-size: 44px;")));
    QVERIFY(qss.contains(QStringLiteral("font-size: 24px;")));
    QVERIFY(qss.contains(QStringLiteral("font-size: 30px;")));
    QVERIFY(qss.contains(QStringLiteral("font-size: 28px;")));
    QVERIFY(qss.contains(QStringLiteral("font-size: 16px;")));

    // Zero leftover pastel cream/pink or dark-brown colors in shared QSS
    QVERIFY(!qss.contains(QStringLiteral("#FFF9F2"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#FFF0E6"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#F3A6A0"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#F7C2B5"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#654C43"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#593E35"), Qt::CaseInsensitive));

    // Smoked-black glass palette tokens are active (text colours remain opaque)
    QCOMPARE(PixelTheme::kBgPrimary, QColor(0x05, 0x06, 0x08));
    QCOMPARE(PixelTheme::kBackground, QColor(0x0A, 0x0C, 0x10));
    QCOMPARE(PixelTheme::kPrimaryAccent, QColor(0x6E, 0xA8, 0xFF));
    QCOMPARE(PixelTheme::kPrimaryAccentHover, QColor(0x8A, 0xB9, 0xFF));
    QCOMPARE(PixelTheme::kPrimaryText, QColor(0xF3, 0xF6, 0xFC));
    QCOMPARE(PixelTheme::kSecondaryText, QColor(0xBC, 0xC7, 0xD9));
    QCOMPARE(PixelTheme::kMutedText, QColor(0x8E, 0x9A, 0xAD));
    QCOMPARE(PixelTheme::kPrimaryText.alpha(), 255);
    QCOMPARE(PixelTheme::kSecondaryText.alpha(), 255);
}

void TestOnboarding::glassMaterialAlphaRanges()
{
    // Primary glass sits around 75-80% effective opacity: visibly tinted, not opaque.
    QVERIFY(PixelTheme::kGlassPrimaryAlpha >= 184 && PixelTheme::kGlassPrimaryAlpha <= 209);
    QVERIFY(PixelTheme::kGlassPrimaryAlpha <= 230); // never more than ~90%
    // Secondary is more transparent than primary; compact overlays are denser for contrast.
    QVERIFY(PixelTheme::kGlassSecondaryAlpha < PixelTheme::kGlassPrimaryAlpha);
    QVERIFY(PixelTheme::kGlassCompactAlpha >= PixelTheme::kGlassPrimaryAlpha);
    QVERIFY(PixelTheme::kGlassCompactAlpha <= 235);

    // Nested card washes must stay near-transparent so they do not cancel the glass.
    QVERIFY(PixelTheme::kCardBg.alpha() >= 26 && PixelTheme::kCardBg.alpha() <= 51);

    // The stylesheet must not reintroduce the old opaque navy fills.
    const QString qss = PixelTheme::styleSheet();
    QVERIFY(!qss.contains(QStringLiteral("rgba(11, 20, 36, 2"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#0B1424"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("#568FE5"), Qt::CaseInsensitive));
}

void TestOnboarding::glassCornersStayTransparent()
{
    // Rounded corners must composite to real alpha = 0 (no square artifacts),
    // while the centre of the surface is the translucent smoked tint.
    constexpr int kSize = 220;
    QImage img(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter painter(&img);
        PixelTheme::paintGlassSurface(painter, QRect(0, 0, kSize, kSize),
                                      PixelTheme::GlassLevel::Primary, PixelTheme::kRadiusWindow, 5);
    }
    QCOMPARE(qAlpha(img.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(img.pixel(kSize - 1, kSize - 1)), 0);
    QCOMPARE(qAlpha(img.pixel(kSize / 2, kSize / 2)) >= 180, true);
    QCOMPARE(qAlpha(img.pixel(kSize / 2, kSize / 2)) <= 225, true);
}

void TestOnboarding::glassWindowsAreTranslucent()
{
    OnboardingWindow onboarding;
    QVERIFY(onboarding.testAttribute(Qt::WA_TranslucentBackground));

}

QTEST_MAIN(TestOnboarding)
#include "test_onboarding.moc"
