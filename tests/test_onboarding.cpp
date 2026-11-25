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
#include <QIcon>
#include <QImage>
#include <QMovie>
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
    void closeButtonExitsOnStartupOnboardingButClosesOnReplay();
    void onboardingDraggableFromBodyAndPreservesDragPreview();
    void appIconLoadsFromSharedTwirlArtwork();
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
    QCOMPARE(PixelTheme::kBackground, QColor(0x08, 0x0A, 0x0C));
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
    // Primary glass sits in 55-65% opacity so wallpaper is clearly recognizable.
    QVERIFY(PixelTheme::kGlassPrimaryAlpha >= 140 && PixelTheme::kGlassPrimaryAlpha <= 166);
    // Secondary nested glass sits in 15-35% local opacity so it never cancels translucency.
    QVERIFY(PixelTheme::kGlassSecondaryAlpha >= 38 && PixelTheme::kGlassSecondaryAlpha <= 90);
    // Compact floating overlays sit in 60-70% opacity for readability while remaining translucent.
    QVERIFY(PixelTheme::kGlassCompactAlpha >= 153 && PixelTheme::kGlassCompactAlpha <= 179);

    // Every non-text material token (including primary blue buttons & borders) must be translucent.
    QVERIFY(PixelTheme::kCardBg.alpha() < 255);
    QVERIFY(PixelTheme::kControlBg.alpha() < 255);
    QVERIFY(PixelTheme::kInputBg.alpha() < 255);
    QVERIFY(PixelTheme::kPrimaryAccentGlass.alpha() < 255);
    QVERIFY(PixelTheme::kPrimaryAccentGlassHover.alpha() < 255);
    QVERIFY(PixelTheme::kPrimaryAccentGlassPressed.alpha() < 255);
    QVERIFY(PixelTheme::kSecondaryAccent.alpha() < 255);
    QVERIFY(PixelTheme::kBorder.alpha() < 255);
    QVERIFY(PixelTheme::kBorderSubtle.alpha() < 255);
    QVERIFY(PixelTheme::kDangerBg.alpha() < 255);

    // Tighter corner radii hierarchy
    QCOMPARE(PixelTheme::kRadiusOnboarding, 16.0);
    QCOMPARE(PixelTheme::kRadiusWindow, 14.0);
    QCOMPARE(PixelTheme::kRadiusCard, 12.0);
    QCOMPARE(PixelTheme::kRadiusButton, 8.0);
    QCOMPARE(PixelTheme::kRadiusClose, 6.0);

    // The stylesheet must not contain opaque hex fills on backgrounds or borders,
    // and must define the minimal transparent close button.
    const QString qss = PixelTheme::styleSheet();
    QVERIFY(!qss.contains(QStringLiteral("background: #"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("selection-background-color: #"), Qt::CaseInsensitive));
    QVERIFY(!qss.contains(QStringLiteral("border-color: #"), Qt::CaseInsensitive));
    QVERIFY(qss.contains(QStringLiteral("QPushButton#closeBtn")));
    QVERIFY(qss.contains(QStringLiteral("background: transparent;")));
}

void TestOnboarding::glassCornersStayTransparent()
{
    // Rounded corners must composite to real alpha = 0 (no square artifacts),
    // while the centre of the Primary surface sits in the 55-65% translucent band.
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
    const int centerAlpha = qAlpha(img.pixel(kSize / 2, kSize / 2));
    QVERIFY2(centerAlpha >= 140 && centerAlpha <= 170,
             qPrintable(QStringLiteral("Expected 55-65% Primary glass alpha, got %1").arg(centerAlpha)));
}

void TestOnboarding::glassWindowsAreTranslucent()
{
    OnboardingWindow onboarding;
    QVERIFY(onboarding.testAttribute(Qt::WA_TranslucentBackground));

    // Close button uses the minimal floating X style with a comfortable hit target.
    auto* closeBtn = onboarding.findChild<QPushButton*>(QStringLiteral("closeBtn"));
    QVERIFY(closeBtn != nullptr);
    QVERIFY(closeBtn->width() >= 30 && closeBtn->height() >= 30);
}

void TestOnboarding::closeButtonExitsOnStartupOnboardingButClosesOnReplay()
{
    OnboardingWindow onboarding;
    QSignalSpy exitSpy(&onboarding, &OnboardingWindow::onboardingExitRequested);
    QSignalSpy skipSpy(&onboarding, &OnboardingWindow::onboardingSkipped);
    QSignalSpy completeSpy(&onboarding, &OnboardingWindow::onboardingCompleted);

    // Startup onboarding (replay = false): clicking X requests app exit instead of continuing
    onboarding.openOnboarding(false);
    auto* closeBtn = onboarding.findChild<QPushButton*>(QStringLiteral("closeBtn"));
    QVERIFY(closeBtn != nullptr);
    QTest::mouseClick(closeBtn, Qt::LeftButton);
    QCOMPARE(exitSpy.count(), 1);
    QCOMPARE(skipSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 0);

    // Tutorial replay (replay = true): clicking X closes tutorial without exiting app
    exitSpy.clear();
    skipSpy.clear();
    onboarding.openOnboarding(true);
    QTest::mouseClick(closeBtn, Qt::LeftButton);
    QCOMPARE(exitSpy.count(), 0);
    QCOMPARE(skipSpy.count(), 1);
    QCOMPARE(skipSpy.first().first().toBool(), true);
}

void TestOnboarding::onboardingDraggableFromBodyAndPreservesDragPreview()
{
    OnboardingWindow onboarding;
    onboarding.openOnboarding(false);
    QApplication::processEvents();

    // Dragging from the Welcome preview area (non-header body) should move the window
    OnboardingPreview* welcomePreview = onboarding.previewForPage(OnboardingWindow::Welcome);
    QVERIFY(welcomePreview != nullptr);

    const QPoint startPos = onboarding.pos();
    const QPoint localCenter = welcomePreview->rect().center();
    const QPoint globalStart = welcomePreview->mapToGlobal(localCenter);
    const QPoint delta(45, 35);

    QMouseEvent pressEvt(QEvent::MouseButtonPress, localCenter, localCenter, globalStart,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(welcomePreview, &pressEvt);

    QMouseEvent moveEvt(QEvent::MouseMove,
                        onboarding.mapFromGlobal(globalStart + delta),
                        onboarding.mapFromGlobal(globalStart + delta),
                        globalStart + delta,
                        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&onboarding, &moveEvt);

    QMouseEvent releaseEvt(QEvent::MouseButtonRelease,
                           onboarding.mapFromGlobal(globalStart + delta),
                           onboarding.mapFromGlobal(globalStart + delta),
                           globalStart + delta,
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&onboarding, &releaseEvt);

    QCOMPARE(onboarding.pos(), startPos + delta);

    // On the interactive Drag tutorial step, dragging inside the preview stage must NOT move the window
    onboarding.goToPage(OnboardingWindow::Drag, false);
    QApplication::processEvents();
    OnboardingPreview* dragPreview = onboarding.previewForPage(OnboardingWindow::Drag);
    QVERIFY(dragPreview != nullptr);

    const QPoint posBeforeDragStep = onboarding.pos();
    const QPoint stageCenter = dragPreview->rect().center();
    const QPoint stageGlobalStart = dragPreview->mapToGlobal(stageCenter);

    QMouseEvent dragPress(QEvent::MouseButtonPress, stageCenter, stageCenter, stageGlobalStart,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(dragPreview, &dragPress);

    QMouseEvent dragMove(QEvent::MouseMove,
                         dragPreview->mapFromGlobal(stageGlobalStart + delta),
                         dragPreview->mapFromGlobal(stageGlobalStart + delta),
                         stageGlobalStart + delta,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(dragPreview, &dragMove);

    QMouseEvent dragRelease(QEvent::MouseButtonRelease,
                            dragPreview->mapFromGlobal(stageGlobalStart + delta),
                            dragPreview->mapFromGlobal(stageGlobalStart + delta),
                            stageGlobalStart + delta,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(dragPreview, &dragRelease);

    QCOMPARE(onboarding.pos(), posBeforeDragStep);
}

void TestOnboarding::appIconLoadsFromSharedTwirlArtwork()
{
    QIcon icon(QStringLiteral(":/assets/chiikawa.ico"));
    QVERIFY(!icon.isNull());

    const QPixmap iconPm = icon.pixmap(256, 256);
    QVERIFY(!iconPm.isNull());
    QCOMPARE(iconPm.size(), QSize(256, 256));

    // Verify the Windows icon matches frame 0 of chiikawa_twirl.gif (shared with macOS chiikawa.icns)
    QMovie twirl(QStringLiteral(":/assets/chiikawa_twirl.gif"));
    QVERIFY(twirl.isValid());
    QVERIFY(twirl.jumpToFrame(0));
    const QImage gifImg = twirl.currentImage().convertToFormat(QImage::Format_ARGB32);
    const QImage icoImg = iconPm.toImage().convertToFormat(QImage::Format_ARGB32);

    // Both must have transparent corners and an opaque white body at the center
    QCOMPARE(qAlpha(icoImg.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(gifImg.pixel(0, 0)), 0);
    const QColor icoCenter = icoImg.pixelColor(icoImg.width() / 2, icoImg.height() / 2);
    const QColor gifCenter = gifImg.pixelColor(gifImg.width() / 2, gifImg.height() / 2);
    QVERIFY(icoCenter.alpha() > 200);
    QVERIFY(gifCenter.alpha() > 200);
    QVERIFY(std::abs(icoCenter.red() - gifCenter.red()) < 15);
    QVERIFY(std::abs(icoCenter.green() - gifCenter.green()) < 15);
    QVERIFY(std::abs(icoCenter.blue() - gifCenter.blue()) < 15);
}

QTEST_MAIN(TestOnboarding)
#include "test_onboarding.moc"
