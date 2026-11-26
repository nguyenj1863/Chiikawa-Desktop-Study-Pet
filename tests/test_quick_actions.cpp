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

class ClickProbeWidget : public QWidget {
public:
    explicit ClickProbeWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(80, 80);
    }

    int pressCount = 0;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        ++pressCount;
        event->accept();
    }
};

} // namespace

class TestQuickActions : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void circularArcGeometryStartsAtBottomAndSweepsAroundOppositeSides();
    void synchronizedOpacityProgression();
    void layoutPositionsScreenEdgesAndPetSizes();
    void revealAndReverseDismissRetraceExactArcAndResetState();
    void doubleClickTogglesAndReversesMidFlight();
    void outsideClickDismissesWithoutBlockingAndSingleClickPreservesMenu();
    void hoverEnlargesTo122WithTwistAndStableUnclippedHitbox();
    void hoverCoexistsWithRevealDismissAndAutoHide();
    void reducedMotionDisablesScaleTwistAndArcTravel();
    void dragAnchorUpdatePreservesActiveAnimation();
    void spotifyIconAndTranslucentButtonRendering();
    void spotifyLauncherStates();
    void petWindowDoubleClickToggleAndSpotifyFallbackNotice();
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

void TestQuickActions::circularArcGeometryStartsAtBottomAndSweepsAroundOppositeSides()
{
    const QRect screen(0, 0, 1920, 1080);
    const QRect petRect(600, 500, 120, 120);
    const QuickActionMenu::Layout layout = QuickActionMenu::computeLayout(petRect, screen);

    const QPointF petCenterGlobal(petRect.left() + petRect.width() * 0.50,
                                  petRect.top() + petRect.height() * 0.52);
    const QPointF offset(layout.overlayRect.topLeft());

    // Settings: starts near lower-right, sweeps outward around right side, lands at upper-right
    const QPointF sStart = offset + QuickActionMenu::evaluateArc(layout.settingsArc, 0.0);
    const QPointF sMid = offset + QuickActionMenu::evaluateArc(layout.settingsArc, 0.5);
    const QPointF sEnd = offset + QuickActionMenu::evaluateArc(layout.settingsArc, 1.0);

    QVERIFY2(sStart.y() > petCenterGlobal.y() + 20.0, "Settings must start near the bottom of Chiikawa");
    QVERIFY2(sStart.x() > petCenterGlobal.x(), "Settings must start on the lower-right side");
    QVERIFY2(sMid.x() > sStart.x() && sMid.x() > sEnd.x(), "Settings arc must sweep outward around the right side");
    QVERIFY(std::abs(sEnd.x() - (petRect.right() + 1 - 8.0)) < 1.5);
    QVERIFY(std::abs(sEnd.y() - (petRect.top() + 14.0)) < 1.5);

    // Spotify: starts near lower-left, sweeps outward around left side, lands at upper-adjacent position
    const QPointF spStart = offset + QuickActionMenu::evaluateArc(layout.spotifyArc, 0.0);
    const QPointF spMid = offset + QuickActionMenu::evaluateArc(layout.spotifyArc, 0.5);
    const QPointF spEnd = offset + QuickActionMenu::evaluateArc(layout.spotifyArc, 1.0);

    QVERIFY2(spStart.y() > petCenterGlobal.y() + 20.0, "Spotify must start near the bottom of Chiikawa");
    QVERIFY2(spStart.x() < petCenterGlobal.x(), "Spotify must start on the lower-left side");
    QVERIFY2(spMid.x() < petCenterGlobal.x() - 35.0, "Spotify arc must sweep visibly around the left side");
    QVERIFY(std::abs(spEnd.x() - (petRect.right() + 1 - 50.0)) < 1.5);
    QVERIFY(std::abs(spEnd.y() - (petRect.top() - 12.0)) < 1.5);

    // Every sample point along both arcs lies at constant radius from the orbit center
    for (int i = 0; i <= 10; ++i) {
        const qreal t = i / 10.0;
        const QPointF pS = QuickActionMenu::evaluateArc(layout.settingsArc, t) - layout.settingsArc.center;
        const QPointF pSp = QuickActionMenu::evaluateArc(layout.spotifyArc, t) - layout.spotifyArc.center;
        QVERIFY(std::abs(std::hypot(pS.x(), pS.y()) - layout.settingsArc.radiusX) < 1e-4);
        QVERIFY(std::abs(std::hypot(pSp.x(), pSp.y()) - layout.spotifyArc.radiusX) < 1e-4);
    }
}

void TestQuickActions::synchronizedOpacityProgression()
{
    QCOMPARE(QuickActionMenu::opacityForProgress(0.0), 0.0);
    const qreal op25 = QuickActionMenu::opacityForProgress(0.25);
    const qreal op50 = QuickActionMenu::opacityForProgress(0.50);
    const qreal op75 = QuickActionMenu::opacityForProgress(0.75);
    QVERIFY(op25 >= 0.20 && op25 <= 0.32);
    QVERIFY(op50 >= 0.50 && op50 <= 0.65);
    QVERIFY(op75 >= 0.80 && op75 <= 0.95);
    QCOMPARE(QuickActionMenu::opacityForProgress(1.0), 1.0);
}

void TestQuickActions::layoutPositionsScreenEdgesAndPetSizes()
{
    const QRect screen(0, 0, 1920, 1080);

    const QRect normalPet(600, 500, 120, 120);
    const QuickActionMenu::Layout normal = QuickActionMenu::computeLayout(normalPet, screen);
    QVERIFY(!normal.mirroredLeft);
    QVERIFY(!normal.sideAdapted);
    QVERIFY(normal.overlayRect.isValid());

    const QPointF sEnd = QuickActionMenu::evaluateArc(normal.settingsArc, 1.0);
    const QPointF spEnd = QuickActionMenu::evaluateArc(normal.spotifyArc, 1.0);
    const qreal centerDist = std::hypot(sEnd.x() - spEnd.x(), sEnd.y() - spEnd.y());
    QVERIFY2(centerDist >= AnimatedQuickActionButton::kWidgetSize,
             "48x48 buttons must not overlap at resting positions");

    // Near right edge: mirrors horizontally to upper-left
    const QRect rightEdgePet(1800, 500, 120, 120);
    const QuickActionMenu::Layout mirrored = QuickActionMenu::computeLayout(rightEdgePet, screen);
    QVERIFY(mirrored.mirroredLeft);
    const QPointF mSettingsGlobal = QPointF(mirrored.overlayRect.topLeft())
                                  + QuickActionMenu::evaluateArc(mirrored.settingsArc, 1.0);
    const QPointF mSpotifyGlobal = QPointF(mirrored.overlayRect.topLeft())
                                 + QuickActionMenu::evaluateArc(mirrored.spotifyArc, 1.0);
    QVERIFY(screen.contains(mSettingsGlobal.toPoint()));
    QVERIFY(screen.contains(mSpotifyGlobal.toPoint()));

    // Near top edge: adapts along the side so icons never clip above screen top
    const QRect topEdgePet(600, 2, 120, 120);
    const QuickActionMenu::Layout topAdapted = QuickActionMenu::computeLayout(topEdgePet, screen);
    QVERIFY(topAdapted.sideAdapted);
    const QPointF topSpotifyGlobal = QPointF(topAdapted.overlayRect.topLeft())
                                   + QuickActionMenu::evaluateArc(topAdapted.spotifyArc, 1.0);
    QVERIFY(topSpotifyGlobal.y() - AnimatedQuickActionButton::kWidgetSize / 2 >= screen.top());

    // Works cleanly for small (60px), default (120px), and large (220px) pet sizes
    for (int sz : {60, 120, 220}) {
        const QuickActionMenu::Layout l = QuickActionMenu::computeLayout(QRect(400, 400, sz, sz), screen);
        const QPointF a = QuickActionMenu::evaluateArc(l.settingsArc, 1.0);
        const QPointF b = QuickActionMenu::evaluateArc(l.spotifyArc, 1.0);
        QVERIFY(std::hypot(a.x() - b.x(), a.y() - b.y()) >= AnimatedQuickActionButton::kWidgetSize);
    }
}

void TestQuickActions::revealAndReverseDismissRetraceExactArcAndResetState()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);
    menu.setAutoHideDurationMs(5000);

    const QRect petRect(400, 400, 120, 120);
    QSignalSpy revealedSpy(&menu, &QuickActionMenu::revealed);
    QSignalSpy dismissedSpy(&menu, &QuickActionMenu::dismissed);

    menu.showMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QVERIFY(menu.isVisible());

    // Early in reveal: Settings moves slightly ahead of Spotify (40 ms stagger)
    QTest::qWait(30);
    QVERIFY(menu.settingsProgress() >= menu.spotifyProgress());

    // Mid-reveal: opacity is synchronized with progress
    QTest::qWait(140);
    const qreal midProg = menu.settingsProgress();
    QVERIFY(midProg > 0.1 && midProg < 1.0);
    QVERIFY(std::abs(menu.settingsButton()->menuOpacity()
                     - QuickActionMenu::opacityForProgress(midProg)) < 0.05);

    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
    QCOMPARE(revealedSpy.count(), 1);
    QVERIFY(std::abs(menu.settingsProgress() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.spotifyProgress() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.settingsButton()->menuOpacity() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.spotifyButton()->menuOpacity() - 1.0) < 1e-3);

    // Apply hover before dismissing to verify dismissal resets hover state completely
    sendEnter(menu.spotifyButton());
    QTest::qWait(120);
    QVERIFY(menu.spotifyButton()->hoverScale() > 1.05);

    // Close retraces the exact same ArcPath in reverse (t: 1 -> 0)
    const QuickActionMenu::ArcPath openSettingsArc = menu.currentLayout().settingsArc;
    menu.hideMenu();
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QTest::qWait(120);
    const qreal closeT = menu.settingsProgress();
    const QPointF expectedCenter = QuickActionMenu::evaluateArc(openSettingsArc, closeT);
    const QPointF actualCenter = QRectF(menu.settingsButton()->geometry()).center();
    QVERIFY(std::hypot(expectedCenter.x() - actualCenter.x(),
                       expectedCenter.y() - actualCenter.y()) <= 2.0);

    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);
    QCOMPARE(dismissedSpy.count(), 1);
    QVERIFY(!menu.isVisible());

    // Both buttons cleanly reset to resting scale=1.00, rotation=0.0, opacity=0.0
    QVERIFY(std::abs(menu.settingsButton()->hoverScale() - 1.0) < 1e-4);
    QVERIFY(std::abs(menu.settingsButton()->hoverRotation()) < 1e-4);
    QVERIFY(std::abs(menu.settingsButton()->menuOpacity()) < 1e-4);
    QVERIFY(std::abs(menu.spotifyButton()->hoverScale() - 1.0) < 1e-4);
    QVERIFY(std::abs(menu.spotifyButton()->hoverRotation()) < 1e-4);
    QVERIFY(std::abs(menu.spotifyButton()->menuOpacity()) < 1e-4);
}

void TestQuickActions::doubleClickTogglesAndReversesMidFlight()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);
    const QRect petRect(400, 400, 120, 120);

    // Closed -> Opening -> Open
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    // Open -> Closing -> Closed
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);

    // Opening -> interrupted mid-flight -> smoothly reverses to Closing without snapping
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QTest::qWait(140);
    const qreal progressBeforeReverse = menu.settingsProgress();
    QVERIFY(progressBeforeReverse > 0.15 && progressBeforeReverse < 0.98);
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QVERIFY(std::abs(menu.settingsProgress() - progressBeforeReverse) < 0.08);

    // Closing -> interrupted mid-flight -> smoothly reverses back to Opening -> Open
    QTest::qWait(80);
    const qreal progressBeforeReopen = menu.settingsProgress();
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QVERIFY(std::abs(menu.settingsProgress() - progressBeforeReopen) < 0.08);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
}

void TestQuickActions::outsideClickDismissesWithoutBlockingAndSingleClickPreservesMenu()
{
    ClickProbeWidget petProxy;
    petProxy.move(400, 400);
    petProxy.show();

    ClickProbeWidget outsideWindow;
    outsideWindow.move(50, 50);
    outsideWindow.show();
    QApplication::processEvents();

    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);
    menu.setAnchorWidget(&petProxy);
    menu.showMenu(petProxy.frameGeometry());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    // Single-click inside Chiikawa's bounds must NOT dismiss the menu
    const QPoint petClickLocal = petProxy.rect().center();
    QMouseEvent petClick(QEvent::MouseButtonPress, petClickLocal, petClickLocal,
                         petProxy.mapToGlobal(petClickLocal),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&petProxy, &petClick);
    QCOMPARE(petProxy.pressCount, 1);
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);

    // Click on another Study Pet-owned window outside pet & buttons dismisses smoothly
    // AND does NOT block or consume the click.
    const QPoint outLocal = outsideWindow.rect().center();
    QMouseEvent outClick(QEvent::MouseButtonPress, outLocal, outLocal,
                         outsideWindow.mapToGlobal(outLocal),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&outsideWindow, &outClick);
    QCOMPARE(outsideWindow.pressCount, 1); // Click was NOT blocked
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);

    // Another outside click while already Closing does not restart or break dismissal
    const qreal closingProg = menu.settingsProgress();
    QApplication::sendEvent(&outsideWindow, &outClick);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QVERIFY(menu.settingsProgress() <= closingProg + 0.05);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);

    // ApplicationDeactivate (switching to another app / desktop) also triggers reverse close
    menu.showMenu(petProxy.frameGeometry());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
    QEvent deactivateEvt(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(qApp, &deactivateEvt);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);
}

void TestQuickActions::hoverEnlargesTo122WithTwistAndStableUnclippedHitbox()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);
    menu.showMenu(QRect(400, 400, 120, 120));
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    AnimatedQuickActionButton* settings = menu.settingsButton();
    AnimatedQuickActionButton* spotify = menu.spotifyButton();

    const QRect settingsGeoBefore = settings->geometry();
    const QRect spotifyGeoBefore = spotify->geometry();

    // Hover Settings: grows to ~1.22 and tilts positively (+6 deg)
    sendEnter(settings);
    QTRY_VERIFY_WITH_TIMEOUT(settings->hoverScale() >= 1.20 && settings->hoverRotation() > 5.0, 700);
    QCOMPARE(settings->geometry(), settingsGeoBefore); // Hitbox stays completely stable
    QCOMPARE(spotify->geometry(), spotifyGeoBefore);   // Neighboring icon does not shift

    // Enlarged rotated button must not clip at the 48x48 widget edges
    QImage enlarged(settings->size(), QImage::Format_ARGB32_Premultiplied);
    enlarged.fill(Qt::transparent);
    settings->render(&enlarged);
    QCOMPARE(qAlpha(enlarged.pixel(0, enlarged.height() / 2)), 0);
    QCOMPARE(qAlpha(enlarged.pixel(enlarged.width() - 1, enlarged.height() / 2)), 0);
    QCOMPARE(qAlpha(enlarged.pixel(enlarged.width() / 2, 0)), 0);
    QCOMPARE(qAlpha(enlarged.pixel(enlarged.width() / 2, enlarged.height() - 1)), 0);

    // Press feedback scales gently down from 1.22 and triggers click immediately
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

    // Re-open for rapid hover switching and Spotify -6 deg twist check
    menu.showMenu(QRect(400, 400, 120, 120));
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    for (int i = 0; i < 4; ++i) {
        sendEnter(spotify);
        QTest::qWait(40);
        sendLeave(spotify);
        sendEnter(settings);
        QTest::qWait(40);
        sendLeave(settings);
    }

    sendEnter(spotify);
    QTRY_VERIFY_WITH_TIMEOUT(spotify->hoverScale() >= 1.20 && spotify->hoverRotation() < -5.0, 700);
    QCOMPARE(spotify->geometry(), spotifyGeoBefore);

    // Leave Spotify: returns cleanly to scale 1.00 and rotation 0.0
    sendLeave(spotify);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(spotify->hoverScale() - 1.0) < 1e-3 && std::abs(spotify->hoverRotation()) < 1e-2,
        700);
    QVERIFY(std::abs(settings->hoverScale() - 1.0) < 1e-3);
    QVERIFY(std::abs(settings->hoverRotation()) < 1e-2);
}

void TestQuickActions::hoverCoexistsWithRevealDismissAndAutoHide()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);
    menu.setAutoHideDurationMs(200);
    menu.setLeaveHideDelayMs(180);

    const QRect petRect(400, 400, 120, 120);
    menu.showMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);

    // Hovering during reveal must not interrupt opening and must pause auto-hide
    sendEnter(menu.settingsButton());
    QVERIFY(!menu.isAutoHideActive());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    // Waiting longer than auto-hide duration while hovered keeps menu open
    QTest::qWait(260);
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);
    QVERIFY(menu.isVisible());

    // Moving quickly from Settings to Spotify does not dismiss
    sendLeave(menu.settingsButton());
    QTest::qWait(40);
    sendEnter(menu.spotifyButton());
    QVERIFY(!menu.isAutoHideActive());
    QTest::qWait(240);
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);

    // Leaving Spotify restarts timer and begins reverse arc closing
    sendLeave(menu.spotifyButton());
    QVERIFY(menu.isAutoHideActive());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closing, 600);

    // Re-entering while closing smoothly restores menu to Open
    sendEnter(menu.spotifyButton());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1200);
    QVERIFY(menu.spotifyButton()->isHovered());
}

void TestQuickActions::reducedMotionDisablesScaleTwistAndArcTravel()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(1);

    menu.showMenu(QRect(400, 400, 120, 120));
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);
    QCOMPARE(menu.settingsProgress(), 1.0);
    QCOMPARE(menu.spotifyProgress(), 1.0);

    // Hovering preserves highlight while disabling scale & rotation
    sendEnter(menu.settingsButton());
    QCOMPARE(menu.settingsButton()->hoverScale(), 1.0);
    QCOMPARE(menu.settingsButton()->hoverRotation(), 0.0);
    QCOMPARE(menu.settingsButton()->hoverProgress(), 1.0);

    sendLeave(menu.settingsButton());
    QCOMPARE(menu.settingsButton()->hoverProgress(), 0.0);

    menu.toggleMenu(QRect(400, 400, 120, 120));
    QCOMPARE(menu.state(), QuickActionMenu::State::Closed);
    QVERIFY(!menu.isVisible());
}

void TestQuickActions::dragAnchorUpdatePreservesActiveAnimation()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);

    const QRect startPet(400, 400, 120, 120);
    menu.showMenu(startPet);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    const QPoint overlayStart = menu.pos();

    // Drag Chiikawa mid-animation: overlay translates smoothly without cancelling arc reveal
    QTest::qWait(50);
    const QPoint delta(75, -40);
    menu.updateAnchor(startPet.translated(delta));
    QCOMPARE(menu.pos(), overlayStart + delta);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);

    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
}

void TestQuickActions::spotifyIconAndTranslucentButtonRendering()
{
    const QIcon spotifyIcon = PixelTheme::icon(PixelIcon::Spotify, QColor(0x1D, 0xB9, 0x54), 18);
    QVERIFY(!spotifyIcon.isNull());
    const QPixmap pm = spotifyIcon.pixmap(QSize(18, 18), 2.0);
    QVERIFY(!pm.isNull());
    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
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

    AnimatedQuickActionButton btn(PixelIcon::Spotify, QColor(0x1D, 0xB9, 0x54),
                                  QColor(0x24, 0xD8, 0x64), -6.0);
    btn.setMenuOpacity(1.0);
    QImage rendered(btn.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    btn.render(&rendered);
    QCOMPARE(qAlpha(rendered.pixel(0, 0)), 0);
    const int glassAlpha = qAlpha(rendered.pixel(14, 14));
    QVERIFY(glassAlpha > 80 && glassAlpha < 240);
}

void TestQuickActions::spotifyLauncherStates()
{
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

void TestQuickActions::petWindowDoubleClickToggleAndSpotifyFallbackNotice()
{
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();

    QVERIFY(pet.quickActionMenu() != nullptr);
    QVERIFY(pet.spotifyNotice() != nullptr);
    pet.quickActionMenu()->setReducedMotionOverride(1);

    pet.setSpotifyLauncher(std::make_unique<SpotifyLauncher>(SpotifyLauncher::Backend{
        []() { return false; },
        []() { return false; },
        []() { return false; }
    }));

    auto sendDoubleClick = [&pet]() {
        QMouseEvent dbl(QEvent::MouseButtonDblClick, pet.rect().center(),
                        pet.rect().center(), pet.mapToGlobal(pet.rect().center()),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&pet, &dbl);
        QApplication::processEvents();
    };

    // First double-click opens the quick-action menu
    sendDoubleClick();
    QVERIFY(pet.quickActionMenu()->isVisible());
    QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Open);

    // Second double-click closes it (toggle!)
    sendDoubleClick();
    QVERIFY(!pet.quickActionMenu()->isVisible());
    QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Closed);

    // Third double-click opens again, then clicking Spotify shows fallback notice
    sendDoubleClick();
    QVERIFY(pet.quickActionMenu()->isVisible());
    pet.quickActionMenu()->spotifyButton()->click();
    QApplication::processEvents();
    QVERIFY(pet.spotifyNotice()->isVisible());
    QVERIFY(!pet.quickActionMenu()->isVisible());

    pet.spotifyNotice()->dismiss();
    pet.hidePet();
}

QTEST_MAIN(TestQuickActions)
#include "test_quick_actions.moc"
