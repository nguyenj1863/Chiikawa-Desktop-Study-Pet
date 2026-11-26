#include <QtTest/QtTest>
#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QSlider>

#include "AnimatedQuickActionButton.h"
#include "AnimationManager.h"
#include "AppSettings.h"
#include "ExternalAppLauncher.h"
#include "MovementController.h"
#include "PetWindow.h"
#include "QuickActionMenu.h"
#include "SettingsPanel.h"

#include <cmath>
#include <memory>

class TestQuickActions : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ChiikawaStudyPetTest"));
        QCoreApplication::setApplicationName(QStringLiteral("TestQuickActions"));
    }

    void cleanup()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    void sixIconsOrderTooltipsAndAccessibleNames()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);

        QCOMPARE(static_cast<int>(QuickActionMenu::ActionCount), 6);

        QVERIFY(menu.spotifyButton() != nullptr);
        QVERIFY(menu.discordButton() != nullptr);
        QVERIFY(menu.leagueButton() != nullptr);
        QVERIFY(menu.cursorButton() != nullptr);
        QVERIFY(menu.settingsButton() != nullptr);
        QVERIFY(menu.exitButton() != nullptr);

        QCOMPARE(menu.spotifyButton()->toolTip(), QStringLiteral("Open Spotify"));
        QCOMPARE(menu.discordButton()->toolTip(), QStringLiteral("Open Discord"));
        QCOMPARE(menu.leagueButton()->toolTip(), QStringLiteral("Open League of Legends"));
        QCOMPARE(menu.cursorButton()->toolTip(), QStringLiteral("Open Cursor"));
        QCOMPARE(menu.settingsButton()->toolTip(), QStringLiteral("Open Settings"));
        QCOMPARE(menu.exitButton()->toolTip(), QStringLiteral("Exit Chiikawa"));

        QCOMPARE(menu.spotifyButton()->accessibleName(), QStringLiteral("Open Spotify"));
        QCOMPARE(menu.discordButton()->accessibleName(), QStringLiteral("Open Discord"));
        QCOMPARE(menu.leagueButton()->accessibleName(), QStringLiteral("Open League of Legends"));
        QCOMPARE(menu.cursorButton()->accessibleName(), QStringLiteral("Open Cursor"));
        QCOMPARE(menu.settingsButton()->accessibleName(), QStringLiteral("Open Settings"));
        QCOMPARE(menu.exitButton()->accessibleName(), QStringLiteral("Exit application"));
    }

    void overlappingCurvedStackFartherRightAcrossSpritesAndSizes()
    {
        AnimationManager anims(nullptr);
        QVERIFY(anims.loadAll());

        const QRect screen(0, 0, 1920, 1080);
        for (int scalePct : {80, 110, 150}) {
            const int visSize = AnimatedQuickActionButton::visualSizeForScale(scalePct);
            const int widgetSize = AnimatedQuickActionButton::widgetSizeForScale(scalePct);
            for (int petSize : {90, 120, 180, 240}) {
                const QRect petRect(600, 420, petSize, petSize);
                for (AnimationId id : {AnimationId::Skip, AnimationId::Stomp, AnimationId::Twirl, AnimationId::Fire}) {
                    const QRect spriteLocal = anims.visibleBoundsInWindow(id, petRect.size());
                    const QRect spriteGlobal = spriteLocal.translated(petRect.topLeft());
                    const QuickActionMenu::Layout layout =
                        QuickActionMenu::computeLayout(petRect, spriteLocal, screen, scalePct);

                    QVERIFY(!layout.mirroredLeft);
                    QVERIFY(screen.contains(layout.overlayRect));

                    QPointF prevGlobal;
                    qreal minX = 1e9;
                    qreal maxX = -1e9;
                    for (int i = 0; i < QuickActionMenu::ActionCount; ++i) {
                        const QPointF localEnd = QuickActionMenu::evaluateArc(layout.arcAt(i), 1.0);
                        const QPointF globalEnd = QPointF(layout.overlayRect.topLeft()) + localEnd;
                        minX = std::min(minX, globalEnd.x());
                        maxX = std::max(maxX, globalEnd.x());

                        // Clear gap from visible sprite silhouette even for top/bottom cards
                        const qreal visualLeft = globalEnd.x() - visSize * 0.5;
                        QVERIFY2(visualLeft >= spriteGlobal.right() + 14.0,
                                 qPrintable(QStringLiteral("Icon %1 too close to sprite: visualLeft=%2 spriteRight=%3")
                                                .arg(i).arg(visualLeft).arg(spriteGlobal.right())));

                        // Zero clipping inside overlay at any scale 80%..150%
                        QVERIFY(localEnd.x() >= widgetSize * 0.5);
                        QVERIFY(localEnd.y() >= widgetSize * 0.5);
                        QVERIFY(localEnd.x() <= layout.overlayRect.width() - widgetSize * 0.5);
                        QVERIFY(localEnd.y() <= layout.overlayRect.height() - widgetSize * 0.5);

                        if (i > 0) {
                            // Strictly ordered top-to-bottom
                            QVERIFY(globalEnd.y() > prevGlobal.y() + visSize * 0.40);
                            // Modest layered-card overlap (~10–25% of visualSize)
                            const qreal dist = std::hypot(globalEnd.x() - prevGlobal.x(),
                                                          globalEnd.y() - prevGlobal.y());
                            QVERIFY2(dist >= visSize * 0.72 && dist <= visSize * 0.96,
                                     qPrintable(QStringLiteral("Expected chord in [0.72..0.96]*vis, got %1 (vis=%2)")
                                                    .arg(dist).arg(visSize)));
                        }
                        prevGlobal = globalEnd;
                    }

                    // Visibly circular C-shaped horizontal bow (not a straight vertical column)
                    const qreal curveSpanX = maxX - minX;
                    QVERIFY2(curveSpanX >= visSize * 0.75,
                             qPrintable(QStringLiteral("Expected pronounced circular bow >= %1, got %2")
                                            .arg(visSize * 0.75).arg(curveSpanX)));
                }
            }
        }
    }

    void mirrorsToLeftNearRightScreenEdge()
    {
        const QRect screen(0, 0, 1280, 720);
        const QRect petNearRight(1140, 320, 120, 120);
        const QRect spriteLocal(18, 18, 84, 84);
        const QuickActionMenu::Layout layout = QuickActionMenu::computeLayout(petNearRight, spriteLocal, screen);

        QVERIFY(layout.mirroredLeft);
        for (int i = 0; i < QuickActionMenu::ActionCount; ++i) {
            const QPointF globalEnd = QPointF(layout.overlayRect.topLeft())
                                    + QuickActionMenu::evaluateArc(layout.arcAt(i), 1.0);
            QVERIFY(globalEnd.x() < petNearRight.left());
        }
    }

    void pullOutHoverAnimationTwoPhaseScaleAndZOrder()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(0);
        menu.setAutoHideDurationMs(60000);

        const QRect petRect(400, 300, 120, 120);
        menu.showMenu(petRect, QRect(15, 15, 90, 90));
        QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

        // Hover over Discord (index 1, tucked between Spotify=0 and League=2)
        AnimatedQuickActionButton* discord = menu.discordButton();
        AnimatedQuickActionButton* spotify = menu.spotifyButton();
        AnimatedQuickActionButton* league = menu.leagueButton();

        menu.setHoveredIndex(QuickActionMenu::DiscordIndex);
        QCOMPARE(menu.hoveredIndex(), static_cast<int>(QuickActionMenu::DiscordIndex));
        QVERIFY(discord->isHovered());
        QVERIFY(!spotify->isHovered());
        QVERIFY(!league->isHovered());

        // Dynamic Z-order: hovered Discord must be the top-most child widget in the stack
        const QList<QObject*> children = menu.children();
        QWidget* topButton = nullptr;
        for (QObject* obj : children) {
            if (auto* btn = qobject_cast<AnimatedQuickActionButton*>(obj)) {
                topButton = btn;
            }
        }
        QCOMPARE(topButton, static_cast<QWidget*>(discord));

        // Phase 1 check: pull-out offset moves upward and outward right away
        QTRY_VERIFY_WITH_TIMEOUT(discord->hoverOffset().y() < -3.0 && discord->hoverOffset().x() > 2.0, 250);

        // Phase 2 check: enlarges to ~1.32x and twists subtly
        QTRY_VERIFY_WITH_TIMEOUT(discord->hoverScale() >= 1.28, 700);
        QVERIFY(std::abs(discord->hoverRotation()) >= 4.0);
        QVERIFY(discord->hoverOffset().y() <= -9.0);
        QVERIFY(discord->hoverOffset().x() >= 8.0);

        // Neighbouring icons stayed completely still
        QCOMPARE(spotify->hoverScale(), AnimatedQuickActionButton::kRestScale);
        QCOMPARE(spotify->hoverOffset(), QPointF(0.0, 0.0));
        QCOMPARE(league->hoverScale(), AnimatedQuickActionButton::kRestScale);
        QCOMPARE(league->hoverOffset(), QPointF(0.0, 0.0));

        // Rapid transition to League: Discord returns cleanly, League becomes elevated top
        menu.setHoveredIndex(QuickActionMenu::LeagueIndex);
        QCOMPARE(menu.hoveredIndex(), static_cast<int>(QuickActionMenu::LeagueIndex));
        QVERIFY(!discord->isHovered());
        QVERIFY(league->isHovered());

        QTRY_VERIFY_WITH_TIMEOUT(discord->hoverScale() <= 1.02
                                 && std::abs(discord->hoverRotation()) <= 0.3
                                 && std::abs(discord->hoverOffset().y()) <= 0.6, 700);
        QTRY_VERIFY_WITH_TIMEOUT(league->hoverScale() >= 1.28, 700);

        // Leave all icons: returns smoothly into stack and restores default Z-order (Exit on top)
        menu.setHoveredIndex(-1);
        QTRY_VERIFY_WITH_TIMEOUT(league->hoverScale() <= 1.02
                                 && std::abs(league->hoverOffset().x()) <= 0.5, 700);

        QWidget* restoredTop = nullptr;
        for (QObject* obj : menu.children()) {
            if (auto* btn = qobject_cast<AnimatedQuickActionButton*>(obj)) {
                restoredTop = btn;
            }
        }
        QCOMPARE(restoredTop, static_cast<QWidget*>(menu.exitButton()));
    }

    void everyOverlappingIconHasExposedClickableHitRegion()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(1);
        menu.setAutoHideDurationMs(60000);

        const QRect petRect(400, 300, 120, 120);
        menu.showMenu(petRect, QRect(15, 15, 90, 90));
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);

        // For every card 0..5, its top exposed band must hit-test to THAT exact card
        for (int i = 0; i < QuickActionMenu::ActionCount; ++i) {
            menu.setHoveredIndex(-1);
            const QRectF r = menu.restingVisualRect(i);
            const QPoint probe(qRound(r.center().x()), qRound(r.top() + 6.0));
            const int hit = menu.hitTestButton(probe);
            QCOMPARE(hit, i);
        }
    }

    void circularEntranceReverseDismissalAndHoverCancel()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(0);
        menu.setAutoHideDurationMs(60000);

        QSignalSpy revealedSpy(&menu, &QuickActionMenu::revealed);
        QSignalSpy dismissedSpy(&menu, &QuickActionMenu::dismissed);

        const QRect petRect(400, 300, 120, 120);
        menu.toggleMenu(petRect, QRect(15, 15, 90, 90));
        QCOMPARE(menu.state(), QuickActionMenu::State::Opening);

        QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);
        QCOMPARE(revealedSpy.count(), 1);
        for (int i = 0; i < QuickActionMenu::ActionCount; ++i) {
            QCOMPARE(menu.progressAt(i), 1.0);
        }

        // Hover Cursor while open, then toggle closed: hover must resolve cleanly while dismissing
        menu.setHoveredIndex(QuickActionMenu::CursorIndex);
        QTRY_VERIFY_WITH_TIMEOUT(menu.cursorButton()->hoverOffset().y() < -5.0, 300);

        menu.toggleMenu(petRect, QRect(15, 15, 90, 90));
        QCOMPARE(menu.state(), QuickActionMenu::State::Closing);
        QCOMPARE(menu.hoveredIndex(), -1);

        QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Closed, 1500);
        QCOMPARE(dismissedSpy.count(), 1);
        QVERIFY(!menu.isVisible());
    }

    void spriteChangeRetargetsSmoothlyWithoutRestartingMenu()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(0);
        menu.setAutoHideDurationMs(60000);

        const QRect petRect(400, 300, 150, 150);
        menu.showMenu(petRect, QRect(35, 30, 80, 90));
        QTRY_COMPARE_WITH_TIMEOUT(menu.state(), QuickActionMenu::State::Open, 1500);

        menu.updateSpriteBounds(petRect, QRect(6, 10, 138, 126));
        QVERIFY(menu.isRetargeting());
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);
        QTRY_VERIFY_WITH_TIMEOUT(!menu.isRetargeting(), 600);
    }

    void externalAppLauncherMockedAllFourApps()
    {
        const std::array<ExternalAppLauncher::AppId, 4> apps = {
            ExternalAppLauncher::AppId::Spotify,
            ExternalAppLauncher::AppId::Discord,
            ExternalAppLauncher::AppId::LeagueOfLegends,
            ExternalAppLauncher::AppId::Cursor
        };

        for (ExternalAppLauncher::AppId app : apps) {
            // 1. Installed & running -> ActivatedRunning
            {
                ExternalAppLauncher launcher;
                bool launched = false;
                launcher.setBackend(app, {
                    []() { return true; },
                    []() { return true; },
                    [&]() { launched = true; return true; }
                });
                const auto res = launcher.openOrActivate(app);
                QCOMPARE(res.status, ExternalAppLauncher::Status::ActivatedRunning);
                QVERIFY(!launched);
            }
            // 2. Installed & closed -> LaunchedApp
            {
                ExternalAppLauncher launcher;
                bool launched = false;
                launcher.setBackend(app, {
                    []() { return false; },
                    []() { return true; },
                    [&]() { launched = true; return true; }
                });
                const auto res = launcher.openOrActivate(app);
                QCOMPARE(res.status, ExternalAppLauncher::Status::LaunchedApp);
                QVERIFY(launched);
            }
            // 3. Not installed -> NotInstalled with valid fallback URL
            {
                ExternalAppLauncher launcher;
                launcher.setBackend(app, {
                    []() { return false; },
                    []() { return false; },
                    []() { return false; }
                });
                const auto res = launcher.openOrActivate(app);
                QCOMPARE(res.status, ExternalAppLauncher::Status::NotInstalled);
                QVERIFY(res.fallbackUrl.isValid());
                QVERIFY(!res.fallbackActionLabel.isEmpty());
            }
            // 4. Launch failed -> LaunchFailed
            {
                ExternalAppLauncher launcher;
                launcher.setBackend(app, {
                    []() { return false; },
                    []() { return true; },
                    []() { return false; }
                });
                const auto res = launcher.openOrActivate(app);
                QCOMPARE(res.status, ExternalAppLauncher::Status::LaunchFailed);
            }
        }
    }

    void petWindowDoubleClickTriggersAllExternalAppsAndFallbackNotice()
    {
        PetWindow pet;
        pet.quickActionMenu()->setNativeOutsideObserverEnabled(false);
        pet.quickActionMenu()->setReducedMotionOverride(1);
        pet.appSettings()->setOnboardingCompleted(true);
        pet.startApplication();

        auto launcher = std::make_unique<ExternalAppLauncher>();
        int spotifyCalls = 0;
        int discordCalls = 0;
        int leagueCalls = 0;
        int cursorCalls = 0;

        launcher->setBackend(ExternalAppLauncher::AppId::Spotify, {
            [&]() { ++spotifyCalls; return true; },
            []() { return true; },
            []() { return true; }
        });
        launcher->setBackend(ExternalAppLauncher::AppId::Discord, {
            []() { return false; },
            []() { return true; },
            [&]() { ++discordCalls; return true; }
        });
        launcher->setBackend(ExternalAppLauncher::AppId::LeagueOfLegends, {
            []() { return false; },
            []() { return false; }, // triggers AppLaunchNoticePopup
            [&]() { ++leagueCalls; return false; }
        });
        launcher->setBackend(ExternalAppLauncher::AppId::Cursor, {
            [&]() { ++cursorCalls; return true; },
            []() { return true; },
            []() { return true; }
        });
        pet.setExternalAppLauncher(std::move(launcher));

        auto openByDoubleClick = [&]() {
            QMouseEvent dbl(QEvent::MouseButtonDblClick,
                            QPointF(pet.rect().center()),
                            QPointF(pet.mapToGlobal(pet.rect().center())),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&pet, &dbl);
        };

        openByDoubleClick();
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Open);
        pet.quickActionMenu()->spotifyButton()->click();
        QCOMPARE(spotifyCalls, 1);

        openByDoubleClick();
        pet.quickActionMenu()->discordButton()->click();
        QCOMPARE(discordCalls, 1);

        openByDoubleClick();
        pet.quickActionMenu()->cursorButton()->click();
        QCOMPARE(cursorCalls, 1);

        openByDoubleClick();
        pet.quickActionMenu()->leagueButton()->click();
        QCOMPARE(leagueCalls, 0);
        QVERIFY(pet.appNotice()->isVisible());

        // Settings button still opens SettingsPanel
        openByDoubleClick();
        pet.quickActionMenu()->settingsButton()->click();
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Closed);

        // Exit button still emits exitRequested and dismisses cleanly
        QSignalSpy exitSpy(pet.quickActionMenu(), &QuickActionMenu::exitRequested);
        openByDoubleClick();
        pet.quickActionMenu()->exitButton()->click();
        QCOMPARE(exitSpy.count(), 1);
    }

    void iconScaleLiveUpdatesWithoutCompounding()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(1);

        QCOMPARE(menu.iconScalePercent(), 110);
        const int defaultVis = menu.spotifyButton()->visualSize();
        QCOMPARE(defaultVis, AnimatedQuickActionButton::visualSizeForScale(110));
        QVERIFY(defaultVis > 34); // noticeably larger than 100% base (34px -> 37px)

        const QRect petRect(400, 300, 120, 120);
        menu.showMenu(petRect, QRect(15, 15, 90, 90));
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);

        // Repeatedly changing scale never compounds
        menu.setIconScalePercent(150);
        QCOMPARE(menu.spotifyButton()->visualSize(), AnimatedQuickActionButton::visualSizeForScale(150));
        QCOMPARE(menu.exitButton()->visualSize(), AnimatedQuickActionButton::visualSizeForScale(150));
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);

        menu.setIconScalePercent(80);
        QCOMPARE(menu.spotifyButton()->visualSize(), AnimatedQuickActionButton::visualSizeForScale(80));
        menu.setIconScalePercent(110);
        QCOMPARE(menu.spotifyButton()->visualSize(), defaultVis);
    }

    void suspendsMovementAndAutoSpriteWhileOpenAndAllowsManualClick()
    {
        PetWindow pet;
        pet.quickActionMenu()->setNativeOutsideObserverEnabled(false);
        pet.quickActionMenu()->setReducedMotionOverride(1);
        pet.appSettings()->setOnboardingCompleted(true);
        pet.appSettings()->setMovementEnabled(true);
        pet.appSettings()->setAutoAnimationEnabled(true);
        pet.showPet();
        QVERIFY(QTest::qWaitForWindowExposed(&pet));

        // Before opening menu: movement and auto-sprite switching are active
        QVERIFY(!pet.movementController()->isPaused());
        QVERIFY(pet.animationManager()->effectiveAutoSwitchEnabled());

        auto doubleClickPet = [&]() {
            QMouseEvent dbl(QEvent::MouseButtonDblClick,
                            QPointF(pet.rect().center()),
                            QPointF(pet.mapToGlobal(pet.rect().center())),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&pet, &dbl);
        };

        // Open quick actions -> movement & auto-sprite switching immediately suspended
        doubleClickPet();
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Open);
        QVERIFY(pet.movementController()->isPausedFor(MovementController::PauseReason::QuickActionMenuOpen));
        QVERIFY(pet.movementController()->isPaused());
        QVERIFY(!pet.movementController()->isTimerActive());
        QVERIFY(pet.animationManager()->isAutoSwitchSuspendedFor(
            AnimationManager::AutoSwitchSuspendReason::QuickActionMenuOpen));
        QVERIFY(!pet.animationManager()->effectiveAutoSwitchEnabled());
        QVERIFY(!pet.animationManager()->isAutoSwitchTimerActive());
        // Persisted user preferences must NOT have been overwritten
        QVERIFY(pet.appSettings()->movementEnabled());
        QVERIFY(pet.appSettings()->autoAnimationEnabled());

        // Single-click Chiikawa while menu is open -> changes sprite, menu stays open, locks stay active!
        const AnimationId beforeAnim = pet.animationManager()->currentAnimation();
        const QPoint localCenter = pet.rect().center();
        const QPoint globalCenter = pet.mapToGlobal(localCenter);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(localCenter), QPointF(globalCenter),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localCenter), QPointF(globalCenter),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&pet, &press);
        QApplication::sendEvent(&pet, &release);

        // Wait past doubleClickInterval so single-click fires
        QTRY_VERIFY_WITH_TIMEOUT(pet.animationManager()->currentAnimation() != beforeAnim,
                                 QApplication::doubleClickInterval() + 400);
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Open);
        QVERIFY(pet.movementController()->isPausedFor(MovementController::PauseReason::QuickActionMenuOpen));
        QVERIFY(!pet.movementController()->isTimerActive());
        QVERIFY(!pet.animationManager()->effectiveAutoSwitchEnabled());

        // Double-click to close -> locks cleanly released and normal behaviour restored
        doubleClickPet();
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Closed);
        QVERIFY(!pet.movementController()->isPausedFor(MovementController::PauseReason::QuickActionMenuOpen));
        QVERIFY(!pet.movementController()->isPaused());
        QVERIFY(pet.animationManager()->effectiveAutoSwitchEnabled());
    }

    void respectsDisabledMovementAndNestedSettingsPause()
    {
        PetWindow pet;
        pet.quickActionMenu()->setNativeOutsideObserverEnabled(false);
        pet.quickActionMenu()->setReducedMotionOverride(1);
        pet.appSettings()->setOnboardingCompleted(true);
        pet.appSettings()->setMovementEnabled(false);
        pet.showPet();
        QVERIFY(QTest::qWaitForWindowExposed(&pet));

        // Scenario B: Movement disabled in Settings stays disabled after menu closes
        pet.movementController()->setEnabled(false);
        pet.quickActionMenu()->showMenu(pet.frameGeometry());
        QVERIFY(pet.movementController()->isPausedFor(MovementController::PauseReason::QuickActionMenuOpen));
        pet.quickActionMenu()->hideMenu();
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Closed);
        QVERIFY(!pet.movementController()->isEnabled());
        QVERIFY(!pet.movementController()->isTimerActive());

        // Scenario F: Opening Settings from quick-action menu preserves SettingsOpen pause after menu closes
        pet.appSettings()->setMovementEnabled(true);
        pet.movementController()->setEnabled(true);
        pet.quickActionMenu()->showMenu(pet.frameGeometry());
        pet.quickActionMenu()->settingsButton()->click();
        QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Closed);
        QVERIFY(!pet.movementController()->isPausedFor(MovementController::PauseReason::QuickActionMenuOpen));
        QVERIFY(pet.movementController()->isPausedFor(MovementController::PauseReason::SettingsOpen));
        QVERIFY(pet.movementController()->isPaused());
    }
};

QTEST_MAIN(TestQuickActions)
#include "test_quick_actions.moc"
