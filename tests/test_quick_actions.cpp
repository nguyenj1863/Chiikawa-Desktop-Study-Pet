#include "AnimatedQuickActionButton.h"
#include "AnimationManager.h"
#include "AppSettings.h"
#include "PetWindow.h"
#include "PixelTheme.h"
#include "QuickActionMenu.h"
#include "SpotifyLauncher.h"

#include <QApplication>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
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

    void cachedVisibleSpriteBoundsAcrossAllFourAnimations();
    void threeIconRadialOrderCurveSpacingAndScreenEdges();
    void circularArcGeometrySweepsRightOrLeftWhenConstrained();
    void synchronizedOpacityProgression();
    void revealAndReverseDismissRetraceExactArcAndResetState();
    void spriteChangeRetargetsSmoothlyWithoutRestartingOrJitter();
    void doubleClickTogglesAndReversesMidFlight();
    void outsideClickDismissesWithoutBlockingAndSingleClickPreservesMenu();
    void hoverEnlargesTo122WithTwistAndStableUnclippedHitbox();
    void exitButtonMetadataIconAndSignal();
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

void TestQuickActions::cachedVisibleSpriteBoundsAcrossAllFourAnimations()
{
    QLabel label;
    label.setFixedSize(120, 120);
    AnimationManager anims(&label);
    QVERIFY(anims.loadAll());

    const QSize winSize(120, 120);
    for (int i = 0; i < static_cast<int>(AnimationId::Count); ++i) {
        const auto id = static_cast<AnimationId>(i);
        const QRectF nb = anims.normalizedBounds(id);
        QVERIFY(nb.isValid() && nb.width() > 0.1 && nb.height() > 0.1);
        const QRect px = anims.visibleBoundsInWindow(id, winSize, false);
        QVERIFY(!px.isEmpty());
        QVERIFY(QRect(QPoint(0, 0), winSize).contains(px));
    }

    // Core opaque bounds must trim thin outlier pixels (e.g. a 1px accessory/spark)
    QImage synthetic(100, 100, QImage::Format_ARGB32);
    synthetic.fill(Qt::transparent);
    for (int y = 30; y < 70; ++y) {
        for (int x = 30; x < 70; ++x) {
            synthetic.setPixelColor(x, y, QColor(255, 255, 255, 255));
        }
    }
    synthetic.setPixelColor(98, 50, QColor(255, 255, 255, 255)); // thin outlier far right
    const QRect core = AnimationManager::computeCoreOpaqueBounds(synthetic);
    QVERIFY2(core.right() < 80, "Single outlier pixel must not pull core bounds far right");

    // Different sprite shapes produce appropriately adapted radial distances
    const QRect screen(0, 0, 1920, 1080);
    const QRect petRect(600, 500, 120, 120);
    const QRect wideSprite(8, 22, 104, 76);
    const QRect compactSprite(36, 36, 48, 48);
    const QuickActionMenu::Layout wideLayout = QuickActionMenu::computeLayout(petRect, wideSprite, screen);
    const QuickActionMenu::Layout compactLayout = QuickActionMenu::computeLayout(petRect, compactSprite, screen);
    QVERIFY(wideLayout.settingsArc.radiusX > compactLayout.settingsArc.radiusX);
}

void TestQuickActions::threeIconRadialOrderCurveSpacingAndScreenEdges()
{
    const QRect screen(0, 0, 1920, 1080);
    const QRect petRect(600, 500, 120, 120);
    const QRect spriteLocal(18, 18, 84, 84);
    const QuickActionMenu::Layout layout = QuickActionMenu::computeLayout(petRect, spriteLocal, screen);

    QVERIFY(!layout.mirroredLeft);
    const QPointF spEnd = QuickActionMenu::evaluateArc(layout.spotifyArc, 1.0);
    const QPointF setEnd = QuickActionMenu::evaluateArc(layout.settingsArc, 1.0);
    const QPointF exEnd = QuickActionMenu::evaluateArc(layout.exitArc, 1.0);

    // Order must be strictly Spotify (top) -> Settings (middle) -> Exit (bottom)
    QVERIFY2(spEnd.y() < setEnd.y(), "Spotify must be above Settings");
    QVERIFY2(setEnd.y() < exEnd.y(), "Settings must be above Exit");

    // Must form a curve around the right side (middle Settings curves further right than top/bottom)
    QVERIFY2(setEnd.x() > spEnd.x() + 5.0, "Icons must curve around Chiikawa, not form a straight column");
    QVERIFY2(setEnd.x() > exEnd.x() + 5.0, "Exit must curve inward below Settings");

    // Spacing >= kWidgetSize (48 px) so 1.22x hover enlargement never causes overlap
    const qreal dSpSet = std::hypot(spEnd.x() - setEnd.x(), spEnd.y() - setEnd.y());
    const qreal dSetEx = std::hypot(setEnd.x() - exEnd.x(), setEnd.y() - exEnd.y());
    QVERIFY(dSpSet >= AnimatedQuickActionButton::kWidgetSize);
    QVERIFY(dSetEx >= AnimatedQuickActionButton::kWidgetSize);

    // Near right edge: mirrors left while preserving Spotify (top) -> Settings (middle) -> Exit (bottom)
    const QRect rightEdgePet(1790, 500, 120, 120);
    const QuickActionMenu::Layout mirrored = QuickActionMenu::computeLayout(rightEdgePet, spriteLocal, screen);
    QVERIFY(mirrored.mirroredLeft);
    const QPointF mSp = QPointF(mirrored.overlayRect.topLeft()) + QuickActionMenu::evaluateArc(mirrored.spotifyArc, 1.0);
    const QPointF mSet = QPointF(mirrored.overlayRect.topLeft()) + QuickActionMenu::evaluateArc(mirrored.settingsArc, 1.0);
    const QPointF mEx = QPointF(mirrored.overlayRect.topLeft()) + QuickActionMenu::evaluateArc(mirrored.exitArc, 1.0);
    QVERIFY(mSp.y() < mSet.y() && mSet.y() < mEx.y());
    QVERIFY(screen.contains(mSp.toPoint()));
    QVERIFY(screen.contains(mSet.toPoint()));
    QVERIFY(screen.contains(mEx.toPoint()));

    // Near top and bottom edges: all 3 icons remain inside screen bounds without clipping
    const QuickActionMenu::Layout topL = QuickActionMenu::computeLayout(QRect(600, 0, 120, 120), spriteLocal, screen);
    const QPointF topSp = QPointF(topL.overlayRect.topLeft()) + QuickActionMenu::evaluateArc(topL.spotifyArc, 1.0);
    QVERIFY(topSp.y() - AnimatedQuickActionButton::kWidgetSize / 2 >= screen.top());

    const QuickActionMenu::Layout botL = QuickActionMenu::computeLayout(QRect(600, 960, 120, 120), spriteLocal, screen);
    const QPointF botEx = QPointF(botL.overlayRect.topLeft()) + QuickActionMenu::evaluateArc(botL.exitArc, 1.0);
    QVERIFY(botEx.y() + AnimatedQuickActionButton::kWidgetSize / 2 <= screen.bottom() + 1);

    // Small (60px), default (120px), and large (220px) pet sizes maintain >= 48px separation
    for (int sz : {60, 120, 220}) {
        const QuickActionMenu::Layout l = QuickActionMenu::computeLayout(
            QRect(400, 400, sz, sz), QRect(sz / 6, sz / 6, (sz * 2) / 3, (sz * 2) / 3), screen);
        const QPointF a = QuickActionMenu::evaluateArc(l.spotifyArc, 1.0);
        const QPointF b = QuickActionMenu::evaluateArc(l.settingsArc, 1.0);
        const QPointF c = QuickActionMenu::evaluateArc(l.exitArc, 1.0);
        QVERIFY(std::hypot(a.x() - b.x(), a.y() - b.y()) >= AnimatedQuickActionButton::kWidgetSize);
        QVERIFY(std::hypot(b.x() - c.x(), b.y() - c.y()) >= AnimatedQuickActionButton::kWidgetSize);
    }
}

void TestQuickActions::circularArcGeometrySweepsRightOrLeftWhenConstrained()
{
    const QRect screen(0, 0, 1920, 1080);
    const QRect petRect(600, 500, 120, 120);
    const QRect spriteLocal(18, 18, 84, 84);
    const QuickActionMenu::Layout layout = QuickActionMenu::computeLayout(petRect, spriteLocal, screen);

    const QPointF spriteCenterGlobal = QPointF(petRect.topLeft()) + QPointF(60.0, 60.0);
    const QPointF offset(layout.overlayRect.topLeft());

    // Default (room on right): all 3 icons start near lower-right and sweep upward around right side
    for (int i = 0; i < QuickActionMenu::ActionCount; ++i) {
        const auto& arc = layout.arcAt(i);
        const QPointF start = offset + QuickActionMenu::evaluateArc(arc, 0.0);
        const QPointF mid = offset + QuickActionMenu::evaluateArc(arc, 0.5);
        QVERIFY2(start.y() > spriteCenterGlobal.y() + 20.0, "All icons must start near lower portion of sprite");
        QVERIFY2(mid.x() > spriteCenterGlobal.x() + 20.0, "Default arcs must sweep around the right side");
    }

    // Near right edge (no space on right): all 3 icons mirror to lower-left and sweep up left side
    const QRect rightEdgePet(1790, 500, 120, 120);
    const QuickActionMenu::Layout leftLayout = QuickActionMenu::computeLayout(rightEdgePet, spriteLocal, screen);
    QVERIFY(leftLayout.mirroredLeft);
    const QPointF rightCenter = QPointF(rightEdgePet.topLeft()) + QPointF(60.0, 60.0);
    const QPointF leftOffset(leftLayout.overlayRect.topLeft());
    for (int i = 0; i < QuickActionMenu::ActionCount; ++i) {
        const QPointF mid = leftOffset + QuickActionMenu::evaluateArc(leftLayout.arcAt(i), 0.5);
        QVERIFY2(mid.x() < rightCenter.x() - 20.0, "Mirrored arcs must sweep around the left side");
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

    // Stagger order: Spotify starts first, then Settings, then Exit
    QTest::qWait(35);
    QVERIFY(menu.spotifyProgress() >= menu.settingsProgress());
    QVERIFY(menu.settingsProgress() >= menu.exitProgress());

    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
    QCOMPARE(revealedSpy.count(), 1);
    QVERIFY(std::abs(menu.spotifyProgress() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.settingsProgress() - 1.0) < 1e-3);
    QVERIFY(std::abs(menu.exitProgress() - 1.0) < 1e-3);

    // Apply hover to Exit before dismissing to verify dismissal resets all 3 buttons
    sendEnter(menu.exitButton());
    QTest::qWait(120);
    QVERIFY(menu.exitButton()->hoverScale() > 1.05);

    const QuickActionMenu::ArcPath openExitArc = menu.currentLayout().exitArc;
    menu.hideMenu();
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QTest::qWait(120);
    const QPointF expectedCenter = QuickActionMenu::evaluateArc(openExitArc, menu.exitProgress());
    const QPointF actualCenter = QRectF(menu.exitButton()->geometry()).center();
    QVERIFY(std::hypot(expectedCenter.x() - actualCenter.x(),
                       expectedCenter.y() - actualCenter.y()) <= 2.0);

    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);
    QCOMPARE(dismissedSpy.count(), 1);
    QVERIFY(!menu.isVisible());

    for (auto* btn : {menu.spotifyButton(), menu.settingsButton(), menu.exitButton()}) {
        QVERIFY(std::abs(btn->hoverScale() - 1.0) < 1e-4);
        QVERIFY(std::abs(btn->hoverRotation()) < 1e-4);
        QVERIFY(std::abs(btn->menuOpacity()) < 1e-4);
    }
}

void TestQuickActions::spriteChangeRetargetsSmoothlyWithoutRestartingOrJitter()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);

    const QRect petRect(400, 400, 120, 120);
    const QRect wideSprite(8, 20, 104, 80);
    const QRect compactSprite(36, 36, 48, 48);

    menu.showMenu(petRect, wideSprite);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    // Sub-threshold shift (1px) must not trigger retargeting (prevents frame-to-frame jitter)
    menu.updateSpriteBounds(petRect, wideSprite.translated(1, 0));
    QVERIFY(!menu.isRetargeting());

    // Meaningful animation change while Open triggers smooth 180ms retargeting
    const QPointF settingsBefore = QPointF(menu.pos()) + QRectF(menu.settingsButton()->geometry()).center();
    menu.updateSpriteBounds(petRect, compactSprite);
    QVERIFY(menu.isRetargeting());
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);
    QTRY_VERIFY_WITH_TIMEOUT(!menu.isRetargeting(), 800);
    const QPointF settingsAfter = QPointF(menu.pos()) + QRectF(menu.settingsButton()->geometry()).center();
    QVERIFY2(settingsAfter.x() < settingsBefore.x() - 8.0,
             "Compact sprite should pull the right-side radial menu closer to the silhouette");

    // Sprite change mid-Opening preserves animation progress instead of restarting from 0
    menu.hideImmediately();
    menu.showMenu(petRect, wideSprite);
    QTest::qWait(160);
    const qreal progBeforeSwitch = menu.spotifyProgress();
    QVERIFY(progBeforeSwitch > 0.2);
    menu.updateSpriteBounds(petRect, compactSprite);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QVERIFY(menu.spotifyProgress() >= progBeforeSwitch - 0.02);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
}

void TestQuickActions::doubleClickTogglesAndReversesMidFlight()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(0);
    const QRect petRect(400, 400, 120, 120);

    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);

    // Interrupt Opening mid-flight -> smoothly reverses to Closing without snapping
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QTest::qWait(140);
    const qreal progressBeforeReverse = menu.spotifyProgress();
    QVERIFY(progressBeforeReverse > 0.15 && progressBeforeReverse < 0.98);
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QVERIFY(std::abs(menu.spotifyProgress() - progressBeforeReverse) < 0.08);

    // Interrupt Closing mid-flight -> smoothly reverses back to Opening -> Open
    QTest::qWait(80);
    const qreal progressBeforeReopen = menu.spotifyProgress();
    menu.toggleMenu(petRect);
    QCOMPARE(menu.state(), QuickActionMenu::State::Opening);
    QVERIFY(std::abs(menu.spotifyProgress() - progressBeforeReopen) < 0.08);
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

    // Click outside dismisses smoothly AND does NOT block the target widget's click
    const QPoint outLocal = outsideWindow.rect().center();
    QMouseEvent outClick(QEvent::MouseButtonPress, outLocal, outLocal,
                         outsideWindow.mapToGlobal(outLocal),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&outsideWindow, &outClick);
    QCOMPARE(outsideWindow.pressCount, 1);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1200);

    // ApplicationDeactivate also triggers reverse close
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

    AnimatedQuickActionButton* spotify = menu.spotifyButton();
    AnimatedQuickActionButton* settings = menu.settingsButton();
    AnimatedQuickActionButton* exitBtn = menu.exitButton();

    const QRect spGeo = spotify->geometry();
    const QRect setGeo = settings->geometry();
    const QRect exGeo = exitBtn->geometry();

    // Settings: ~1.22 scale, +6 deg twist, stable unclipped 48x48 hitbox
    sendEnter(settings);
    QTRY_VERIFY_WITH_TIMEOUT(settings->hoverScale() >= 1.20 && settings->hoverRotation() > 5.0, 700);
    QCOMPARE(settings->geometry(), setGeo);
    QCOMPARE(spotify->geometry(), spGeo);
    QCOMPARE(exitBtn->geometry(), exGeo);

    QImage enlarged(settings->size(), QImage::Format_ARGB32_Premultiplied);
    enlarged.fill(Qt::transparent);
    settings->render(&enlarged);
    QCOMPARE(qAlpha(enlarged.pixel(0, enlarged.height() / 2)), 0);
    QCOMPARE(qAlpha(enlarged.pixel(enlarged.width() - 1, enlarged.height() / 2)), 0);
    sendLeave(settings);

    // Spotify: ~1.22 scale, -6 deg twist
    sendEnter(spotify);
    QTRY_VERIFY_WITH_TIMEOUT(spotify->hoverScale() >= 1.20 && spotify->hoverRotation() < -5.0, 700);
    sendLeave(spotify);

    // Exit: ~1.22 scale, -5 deg twist
    sendEnter(exitBtn);
    QTRY_VERIFY_WITH_TIMEOUT(exitBtn->hoverScale() >= 1.20 && exitBtn->hoverRotation() < -4.0, 700);
    QCOMPARE(exitBtn->geometry(), exGeo);
    sendLeave(exitBtn);

    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(exitBtn->hoverScale() - 1.0) < 1e-3 && std::abs(exitBtn->hoverRotation()) < 1e-2,
        700);
}

void TestQuickActions::exitButtonMetadataIconAndSignal()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(1);
    menu.showMenu(QRect(400, 400, 120, 120));

    AnimatedQuickActionButton* exitBtn = menu.exitButton();
    QVERIFY(exitBtn != nullptr);
    QCOMPARE(exitBtn->toolTip(), QStringLiteral("Exit Chiikawa"));
    QCOMPARE(exitBtn->accessibleName(), QStringLiteral("Exit application"));

    const QIcon exitIcon = PixelTheme::icon(PixelIcon::Exit, PixelTheme::kPrimaryText, 18);
    QVERIFY(!exitIcon.isNull());
    QVERIFY(!exitIcon.pixmap(QSize(18, 18), 2.0).isNull());

    // Clicking Exit emits exitRequested() and closes the menu (without terminating test process)
    QSignalSpy exitSpy(&menu, &QuickActionMenu::exitRequested);
    exitBtn->click();
    QCOMPARE(exitSpy.count(), 1);
    QCOMPARE(menu.state(), QuickActionMenu::State::Closed);
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

    // Hovering Exit during reveal pauses auto-hide without interrupting reveal
    sendEnter(menu.exitButton());
    QVERIFY(!menu.isAutoHideActive());
    QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

    QTest::qWait(260);
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);

    // Gliding from Exit to Spotify does not dismiss
    sendLeave(menu.exitButton());
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
}

void TestQuickActions::reducedMotionDisablesScaleTwistAndArcTravel()
{
    QuickActionMenu menu;
    menu.setNativeOutsideObserverEnabled(false);
    menu.setReducedMotionOverride(1);

    menu.showMenu(QRect(400, 400, 120, 120));
    QCOMPARE(menu.state(), QuickActionMenu::State::Open);
    QCOMPARE(menu.spotifyProgress(), 1.0);
    QCOMPARE(menu.settingsProgress(), 1.0);
    QCOMPARE(menu.exitProgress(), 1.0);

    sendEnter(menu.exitButton());
    QCOMPARE(menu.exitButton()->hoverScale(), 1.0);
    QCOMPARE(menu.exitButton()->hoverRotation(), 0.0);
    QCOMPARE(menu.exitButton()->hoverProgress(), 1.0);

    sendLeave(menu.exitButton());
    menu.toggleMenu(QRect(400, 400, 120, 120));
    QCOMPARE(menu.state(), QuickActionMenu::State::Closed);
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
        QCOMPARE(launcher.openOrActivate().status, SpotifyLauncher::Status::ActivatedRunning);
        QCOMPARE(launchCalls, 0);
    }
    {
        int launchCalls = 0;
        SpotifyLauncher launcher({
            []() { return false; },
            []() { return true; },
            [&launchCalls]() { ++launchCalls; return true; }
        });
        QCOMPARE(launcher.openOrActivate().status, SpotifyLauncher::Status::LaunchedApp);
        QCOMPARE(launchCalls, 1);
    }
    {
        int launchCalls = 0;
        SpotifyLauncher launcher({
            []() { return false; },
            []() { return false; },
            [&launchCalls]() { ++launchCalls; return true; }
        });
        QCOMPARE(launcher.openOrActivate().status, SpotifyLauncher::Status::NotInstalled);
        QCOMPARE(launchCalls, 0);
    }
    {
        SpotifyLauncher launcher({
            []() { return false; },
            []() { return true; },
            []() { return false; }
        });
        QCOMPARE(launcher.openOrActivate().status, SpotifyLauncher::Status::LaunchFailed);
    }
}

void TestQuickActions::petWindowDoubleClickToggleAndSpotifyFallbackNotice()
{
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();

    QVERIFY(pet.quickActionMenu() != nullptr);
    QVERIFY(pet.spotifyNotice() != nullptr);
    pet.quickActionMenu()->setNativeOutsideObserverEnabled(false);
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

    sendDoubleClick();
    QVERIFY(pet.quickActionMenu()->isVisible());
    QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Open);
    QVERIFY(pet.quickActionMenu()->spotifyButton()->isVisible());
    QVERIFY(pet.quickActionMenu()->settingsButton()->isVisible());
    QVERIFY(pet.quickActionMenu()->exitButton()->isVisible());

    sendDoubleClick();
    QVERIFY(!pet.quickActionMenu()->isVisible());
    QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Closed);

    sendDoubleClick();
    pet.quickActionMenu()->spotifyButton()->click();
    QApplication::processEvents();
    QVERIFY(pet.spotifyNotice()->isVisible());
    QVERIFY(!pet.quickActionMenu()->isVisible());

    pet.spotifyNotice()->dismiss();
    pet.hidePet();
}

QTEST_MAIN(TestQuickActions)
#include "test_quick_actions.moc"
