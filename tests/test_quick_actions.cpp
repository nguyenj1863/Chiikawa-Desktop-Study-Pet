#include "AnimatedQuickActionButton.h"
#include "AppSettings.h"
#include "PetWindow.h"
#include "PixelTheme.h"
#include "QuickActionMenu.h"
#include "SpotifyLauncher.h"

#include <QApplication>
#include <QEnterEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

namespace {

void sendEnter(QWidget* w)
{
    const QPointF local = QRectF(w->rect()).center();
    const QPointF global = w->mapToGlobal(local.toPoint());
    QEnterEvent evt(local, local, global);
    QApplication::sendEvent(w, &evt);
}

void sendLeave(QWidget* w)
{
    QEvent evt(QEvent::Leave);
    QApplication::sendEvent(w, &evt);
}

} // namespace

class TestQuickActions : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void bezierCurveIsCurvedAndMatchesEndpoints();
    void layoutPositionsScreenEdgesAndPetSizes();
    void revealAndDismissCurvedAnimationsAndStateReset();
    void hoverScaleTwistAndStableHitbox();
    void hoverCoexistsWithRevealDismissAndAutoHide();
    void reducedMotionDisablesScaleTwistAndCurvedTravel();
    void dragAnchorUpdatePreservesActiveAnimation();
    void spotifyIconAndTranslucentButtonRendering();
    void spotifyLauncherStates();
    void petWindowDoubleClickAndSpotifyFallbackNotice();
};

void TestQuickActions::init()
{
    QSettings s(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    s.clear();
    s.sync();
}

void TestQuickActions::cleanup()
{
    QSettings s(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPet"));
    s.clear();
    s.sync();
}

void TestQuickActions::bezierCurveIsCurvedAndMatchesEndpoints()
{
    const QPointF p0(20.0, 60.0);
    const QPointF p1(30.0, 28.0);
    const QPointF p2(65.0, 18.0);
    const QPointF p3(80.0, 32.0);

    const QPointF start = QuickActionMenu::cubicBezier(p0, p1, p2, p3, 0.0);
    const QPointF end = QuickActionMenu::cubicBezier(p0, p1, p2, p3, 1.0);
    const QPointF mid = QuickActionMenu::cubicBezier(p0, p1, p2, p3, 0.5);
    const QPointF linearMid = (p0 + p3) * 0.5;

    QVERIFY(std::abs(start.x() - p0.x()) < 1e-6);
    QVERIFY(std::abs(start.y() - p0.y()) < 1e-6);
    QVERIFY(std::abs(end.x() - p3.x()) < 1e-6);
    QVERIFY(std::abs(end.y() - p3.y()) < 1e-6);

    // Midpoint must arch clearly above the straight line between p0 and p3.
    QVERIFY(mid.y() < linearMid.y() - 10.0);
}

void TestQuickActions::layoutPositionsScreenEdgesAndPetSizes()
{
    const QRect screen(0, 0, 1920, 1080);

    // Default center-screen placement: upper-right arc, no mirroring, non-overlapping buttons
    const QRect normalPet(600, 500, 120, 120);
    const QuickActionMenu::Layout normal = QuickActionMenu::computeLayout(normalPet, screen);
    QVERIFY(!normal.mirroredLeft);
    QVERIFY(!normal.sideAdapted);
    QVERIFY(normal.overlayRect.isValid());

    const QPointF sEnd = normal.settingsCurve.p3;
    const QPointF spEnd = normal.spotifyCurve.p3;
    const qreal centerDist = std::hypot(sEnd.x() - spEnd.x(), sEnd.y() - spEnd.y());
    QVERIFY2(centerDist >= AnimatedQuickActionButton::kWidgetSize,
             "Buttons must not overlap at resting positions");

    // Near right edge: mirrors horizontally to upper-left
    const QRect rightEdgePet(1800, 500, 120, 120);
    const QuickActionMenu::Layout mirrored = QuickActionMenu::computeLayout(rightEdgePet, screen);
    QVERIFY(mirrored.mirroredLeft);
    const QPointF mirroredSettingsGlobal = QPointF(mirrored.overlayRect.topLeft()) + mirrored.settingsCurve.p3;
    const QPointF mirroredSpotifyGlobal = QPointF(mirrored.overlayRect.topLeft()) + mirrored.spotifyCurve.p3;
    QVERIFY(screen.contains(mirroredSettingsGlobal.toPoint()));
    QVERIFY(screen.contains(mirroredSpotifyGlobal.toPoint()));

    // Near top edge: adapts along the side so icons never clip above screen top
    const QRect topEdgePet(600, 2, 120, 120);
    const QuickActionMenu::Layout topAdapted = QuickActionMenu::computeLayout(topEdgePet, screen);
    QVERIFY(topAdapted.sideAdapted);
    const QPointF topSpotifyGlobal = QPointF(topAdapted.overlayRect.topLeft()) + topAdapted.spotifyCurve.p3;
    QVERIFY(topSpotifyGlobal.y() - AnimatedQuickActionButton::kWidgetSize / 2 >= screen.top());

    // Works cleanly for both small (60px) and large (220px) pet sizes
    for (int sz : {60, 120, 220}) {
        const QuickActionMenu::Layout l = QuickActionMenu::computeLayout(QRect(400, 400, sz, sz), screen);
        const qreal d = std::hypot(l.settingsCurve.p3.x() - l.spotifyCurve.p3.x(),
                                   l.settingsCurve.p3.y() - l.spotifyCurve.p3.y());
        QVERIFY(d >= AnimatedQuickActionButton::kWidgetSize);
    }
}

void TestQuickActions::revealAndDismissCurvedAnimationsAndStateReset()
{
    QuickActionMenu menu;
    menu.setReducedMotionOverride(0);
    menu.setAutoHideDurationMs(5000);

    const QRect petRect(400, 400, 120, 120);
    QSignalSpy revealedSpy(&menu, &QuickActionMenu::revealed);
    QSignalSpy dismissedSpy(&menu, &QuickActionMenu::dismissed);

    menu.showMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Revealing);
    QVERIFY(menu.isVisible());

    // Early in reveal: Settings has started moving while Spotify is staggered behind
    QTest::qWait(35);
    QVERIFY(menu.settingsProgress() >= menu.spotifyProgress());

    // Wait for reveal to finish
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Visible, 1200);
    QCOMPARE(revealedSpy.count(), 1);
    QVERIFY(std::abs(menu.settingsProgress() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.spotifyProgress() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.settingsButton()->menuOpacity() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.spotifyButton()->menuOpacity() - 1.0) < 1e-3);

    // Apply hover before dismissing to verify dismissal resets hover state completely
    sendEnter(menu.spotifyButton());
    QTest::qWait(100);
    QVERIFY(menu.spotifyButton()->hoverScale() > 1.01);

    menu.hideMenu();
    QCOMPARE(menu.state(), QuickActionMenu::State::Dismissing);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Hidden, 1000);
    QCOMPARE(dismissedSpy.count(), 1);
    QVERIFY(!menu.isVisible());

    // Both buttons must be cleanly reset to resting scale=1.00, rotation=0.0, opacity=0.0
    QVERIFY(std::abs(menu.settingsButton()->hoverScale() - 1.0) < 1e-4);
    QVERIFY(std::abs(menu.settingsButton()->hoverRotation()) < 1e-4);
    QVERIFY(std::abs(menu.settingsButton()->menuOpacity()) < 1e-4);
    QVERIFY(std::abs(menu.spotifyButton()->hoverScale() - 1.0) < 1e-4);
    QVERIFY(std::abs(menu.spotifyButton()->hoverRotation()) < 1e-4);
    QVERIFY(std::abs(menu.spotifyButton()->menuOpacity()) < 1e-4);
}

void TestQuickActions::hoverScaleTwistAndStableHitbox()
{
    QuickActionMenu menu;
    menu.setReducedMotionOverride(0);
    menu.showMenu(QRect(400, 400, 120, 120));
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Visible, 1200);

    AnimatedQuickActionButton* settings = menu.settingsButton();
    AnimatedQuickActionButton* spotify = menu.spotifyButton();

    const QRect settingsGeoBefore = settings->geometry();
    const QRect spotifyGeoBefore = spotify->geometry();

    // Hover Settings: grows (~1.11) and tilts positively (+6 deg)
    sendEnter(settings);
    QTRY_VERIFY_WITH_TIMEOUT(settings->hoverScale() > 1.09 && settings->hoverRotation() > 5.0, 600);
    QCOMPARE(settings->geometry(), settingsGeoBefore); // Hitbox stays completely stable
    QCOMPARE(spotify->geometry(), spotifyGeoBefore);   // Neighboring icon does not shift

    // Press feedback scales gently toward 1.04 and still triggers click immediately
    QSignalSpy settingsClicked(settings, &QAbstractButton::clicked);
    QMouseEvent pressEvt(QEvent::MouseButtonPress, settings->rect().center(),
                         settings->rect().center(), settings->mapToGlobal(settings->rect().center()),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(settings, &pressEvt);
    QTest::qWait(110);
    QVERIFY(settings->hoverScale() < AnimatedQuickActionButton::kHoverScale);
    QVERIFY(settings->pressProgress() > 0.5);

    QMouseEvent releaseEvt(QEvent::MouseButtonRelease, settings->rect().center(),
                           settings->rect().center(), settings->mapToGlobal(settings->rect().center()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(settings, &releaseEvt);
    QCOMPARE(settingsClicked.count(), 1);

    // Re-show for rapid hover switching and leave tests
    menu.showMenu(QRect(400, 400, 120, 120));
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Visible, 1200);

    // Rapid hover/leave cycles must not accumulate rotation and must settle precisely at 1.00 / 0.0
    for (int i = 0; i < 4; ++i) {
        sendEnter(spotify);
        QTest::qWait(40);
        sendLeave(spotify);
        sendEnter(settings);
        QTest::qWait(40);
        sendLeave(settings);
    }

    // Hover Spotify to verify opposite tilt direction (-6 deg)
    sendEnter(spotify);
    QTRY_VERIFY_WITH_TIMEOUT(spotify->hoverScale() > 1.09 && spotify->hoverRotation() < -5.0, 600);
    QCOMPARE(spotify->geometry(), spotifyGeoBefore);

    // Leave Spotify: must return precisely to scale 1.00 and rotation 0.0
    sendLeave(spotify);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(spotify->hoverScale() - 1.0) < 1e-3 && std::abs(spotify->hoverRotation()) < 1e-2,
        600);
    QVERIFY(std::abs(settings->hoverScale() - 1.0) < 1e-3);
    QVERIFY(std::abs(settings->hoverRotation()) < 1e-2);
}

void TestQuickActions::hoverCoexistsWithRevealDismissAndAutoHide()
{
    QuickActionMenu menu;
    menu.setReducedMotionOverride(0);
    menu.setAutoHideDurationMs(180);
    menu.setLeaveHideDelayMs(160);

    const QRect petRect(400, 400, 120, 120);
    menu.showMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Revealing);

    // Hovering during reveal must not interrupt reveal and must pause auto-hide
    sendEnter(menu.settingsButton());
    QVERIFY(!menu.isAutoHideActive());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Visible, 1200);

    // Waiting longer than auto-hide duration while hovered must keep menu visible
    QTest::qWait(260);
    QCOMPARE(menu.state(), QuickActionMenu::State::Visible);
    QVERIFY(menu.isVisible());

    // Moving quickly from Settings to Spotify must not dismiss
    sendLeave(menu.settingsButton());
    QTest::qWait(40);
    sendEnter(menu.spotifyButton());
    QVERIFY(!menu.isAutoHideActive());
    QTest::qWait(240);
    QCOMPARE(menu.state(), QuickActionMenu::State::Visible);

    // Leaving Spotify restarts timer and begins dismissal
    sendLeave(menu.spotifyButton());
    QVERIFY(menu.isAutoHideActive());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Dismissing, 600);

    // Re-entering while dismissing smoothly restores menu
    sendEnter(menu.spotifyButton());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Visible, 1000);
    QVERIFY(menu.spotifyButton()->isHovered());

    // Repeated showMenu() calls while visible do not flicker or reset progress to 0
    sendLeave(menu.spotifyButton());
    menu.showMenu(petRect);
    menu.showMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Visible);
    QVERIFY(menu.settingsProgress() > 0.99);
    QVERIFY(menu.spotifyProgress() > 0.99);
}

void TestQuickActions::reducedMotionDisablesScaleTwistAndCurvedTravel()
{
    QuickActionMenu menu;
    menu.setReducedMotionOverride(1);

    menu.showMenu(QRect(400, 400, 120, 120));
    QCOMPARE(menu.state(), QuickActionMenu::State::Visible);
    QCOMPARE(menu.settingsProgress(), 1.0);
    QCOMPARE(menu.spotifyProgress(), 1.0);

    // Hovering preserves highlight (hoverProgress == 1) while disabling scale & rotation
    sendEnter(menu.settingsButton());
    QCOMPARE(menu.settingsButton()->hoverScale(), 1.0);
    QCOMPARE(menu.settingsButton()->hoverRotation(), 0.0);
    QCOMPARE(menu.settingsButton()->hoverProgress(), 1.0);

    sendLeave(menu.settingsButton());
    QCOMPARE(menu.settingsButton()->hoverProgress(), 0.0);

    menu.hideMenu();
    QCOMPARE(menu.state(), QuickActionMenu::State::Hidden);
    QVERIFY(!menu.isVisible());
}

void TestQuickActions::dragAnchorUpdatePreservesActiveAnimation()
{
    QuickActionMenu menu;
    menu.setReducedMotionOverride(0);

    const QRect startPet(400, 400, 120, 120);
    menu.showMenu(startPet);
    QCOMPARE(menu.state(), QuickActionMenu::State::Revealing);
    const QPoint overlayStart = menu.pos();

    // Drag Chiikawa mid-animation: overlay translates by the same delta without cancelling reveal
    QTest::qWait(50);
    const QPoint delta(75, -40);
    menu.updateAnchor(startPet.translated(delta));
    QCOMPARE(menu.pos(), overlayStart + delta);
    QCOMPARE(menu.state(), QuickActionMenu::State::Revealing);

    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Visible, 1200);
}

void TestQuickActions::spotifyIconAndTranslucentButtonRendering()
{
    const QIcon spotifyIcon = PixelTheme::icon(PixelIcon::Spotify, QColor(0x1D, 0xB9, 0x54), 18);
    QVERIFY(!spotifyIcon.isNull());
    const QPixmap pm = spotifyIcon.pixmap(QSize(18, 18), 2.0);
    QVERIFY(!pm.isNull());
    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    // Corners must stay transparent; circular body must have green pixels
    QCOMPARE(qAlpha(img.pixel(0, 0)), 0);
    bool hasGreen = false;
    for (int y = 0; y < img.height() && !hasGreen; ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() > 200 && c.green() > c.red() + 40) {
                hasGreen = true;
                break;
            }
        }
    }
    QVERIFY(hasGreen);

    // Button background must remain translucent (alpha < 255)
    AnimatedQuickActionButton btn(PixelIcon::Spotify, QColor(0x1D, 0xB9, 0x54),
                                  QColor(0x24, 0xD8, 0x64), -6.0);
    btn.setMenuOpacity(1.0);
    QImage rendered(btn.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    btn.render(&rendered);
    // Outer padding corner is transparent; glass surface near button corner is translucent
    QCOMPARE(qAlpha(rendered.pixel(0, 0)), 0);
    const int glassAlpha = qAlpha(rendered.pixel(10, 10));
    QVERIFY(glassAlpha > 80 && glassAlpha < 240);
}

void TestQuickActions::spotifyLauncherStates()
{
    // 1. Running Spotify -> activates existing window without launching duplicate
    {
        int launchCalls = 0;
        SpotifyLauncher launcher({
            []() { return true; },
            []() { return true; },
            [&launchCalls]() { ++launchCalls; return true; }
        });
        const auto res = launcher.openOrActivate();
        QCOMPARE(res.status, SpotifyLauncher::Status::ActivatedRunning);
        QCOMPARE(launchCalls, 0);
    }

    // 2. Installed but closed -> launches app
    {
        int launchCalls = 0;
        SpotifyLauncher launcher({
            []() { return false; },
            []() { return true; },
            [&launchCalls]() { ++launchCalls; return true; }
        });
        const auto res = launcher.openOrActivate();
        QCOMPARE(res.status, SpotifyLauncher::Status::LaunchedApp);
        QCOMPARE(launchCalls, 1);
    }

    // 3. Not installed -> reports NotInstalled without launching
    {
        int launchCalls = 0;
        SpotifyLauncher launcher({
            []() { return false; },
            []() { return false; },
            [&launchCalls]() { ++launchCalls; return true; }
        });
        const auto res = launcher.openOrActivate();
        QCOMPARE(res.status, SpotifyLauncher::Status::NotInstalled);
        QCOMPARE(launchCalls, 0);
    }

    // 4. Installed, launch fails -> reports LaunchFailed
    {
        SpotifyLauncher launcher({
            []() { return false; },
            []() { return true; },
            []() { return false; }
        });
        const auto res = launcher.openOrActivate();
        QCOMPARE(res.status, SpotifyLauncher::Status::LaunchFailed);
    }
}

void TestQuickActions::petWindowDoubleClickAndSpotifyFallbackNotice()
{
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();

    QVERIFY(pet.quickActionMenu() != nullptr);
    QVERIFY(pet.spotifyNotice() != nullptr);
    pet.quickActionMenu()->setReducedMotionOverride(1);

    // Inject mock launcher reporting NotInstalled so no real Spotify app is launched
    pet.setSpotifyLauncher(std::make_unique<SpotifyLauncher>(SpotifyLauncher::Backend{
        []() { return false; },
        []() { return false; },
        []() { return false; }
    }));

    // Double-clicking Chiikawa reveals the quick-action menu
    QMouseEvent dbl(QEvent::MouseButtonDblClick, pet.rect().center(),
                    pet.rect().center(), pet.mapToGlobal(pet.rect().center()),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&pet, &dbl);
    QApplication::processEvents();

    QVERIFY(pet.quickActionMenu()->isVisible());
    QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Visible);
    QCOMPARE(pet.quickActionMenu()->spotifyButton()->toolTip(), QStringLiteral("Open Spotify"));
    QCOMPARE(pet.quickActionMenu()->spotifyButton()->accessibleName(), QStringLiteral("Open Spotify"));

    // Clicking Spotify when not installed shows the unobtrusive notice popup and dismisses menu
    pet.quickActionMenu()->spotifyButton()->click();
    QApplication::processEvents();
    QVERIFY(pet.spotifyNotice()->isVisible());
    QVERIFY(!pet.quickActionMenu()->isVisible());

    pet.spotifyNotice()->dismiss();
    pet.hidePet();
}

QTEST_MAIN(TestQuickActions)
#include "test_quick_actions.moc"
