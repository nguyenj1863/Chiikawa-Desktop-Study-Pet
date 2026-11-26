#include <QtTest/QtTest>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QSlider>

#include "AnimatedQuickActionButton.h"
#include "AnimationManager.h"
#include "AppSettings.h"
#include "CollapsibleSection.h"
#include "ExternalAppLauncher.h"
#include "InstalledAppDiscovery.h"
#include "MovementController.h"
#include "PetWindow.h"
#include "QuickActionItem.h"
#include "QuickActionMenu.h"
#include "QuickActionsEditor.h"
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

        QCOMPARE(menu.actionCount(), 6);

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
                    for (int i = 0; i < layout.count(); ++i) {
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
        for (int i = 0; i < layout.count(); ++i) {
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

        menu.setHoveredIndex(1);
        QCOMPARE(menu.hoveredIndex(), 1);
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
        menu.setHoveredIndex(2);
        QCOMPARE(menu.hoveredIndex(), 2);
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
        for (int i = 0; i < menu.actionCount(); ++i) {
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
        for (int i = 0; i < menu.actionCount(); ++i) {
            QCOMPARE(menu.progressAt(i), 1.0);
        }

        // Hover Cursor while open, then toggle closed: hover must resolve cleanly while dismissing
        menu.setHoveredIndex(3);
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

    void dynamicShortcutCountsAndReorderingAdaptRadialArc()
    {
        const QRect screen(0, 0, 1920, 1080);
        const QRect petRect(600, 420, 120, 120);
        const QRect spriteLocal(18, 18, 84, 84);

        for (int count : {1, 2, 4, 6, 10}) {
            const QuickActionMenu::Layout layout =
                QuickActionMenu::computeLayout(petRect, spriteLocal, screen, 110, count);
            QCOMPARE(layout.count(), count);
            QVERIFY(screen.contains(layout.overlayRect));
            const int widgetSize = AnimatedQuickActionButton::widgetSizeForScale(110);
            for (int i = 0; i < count; ++i) {
                const QPointF pt = QuickActionMenu::evaluateArc(layout.arcAt(i), 1.0);
                QVERIFY(pt.x() >= widgetSize * 0.5);
                QVERIFY(pt.y() >= widgetSize * 0.5);
                QVERIFY(pt.x() <= layout.overlayRect.width() - widgetSize * 0.5);
                QVERIFY(pt.y() <= layout.overlayRect.height() - widgetSize * 0.5);
                if (i > 0) {
                    const QPointF prev = QuickActionMenu::evaluateArc(layout.arcAt(i - 1), 1.0);
                    QVERIFY(pt.y() > prev.y());
                }
            }
        }

        // Disable Discord & League, reorder Cursor to top, add custom VS Code
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(1);
        QVector<QuickActionItem> items = QuickActionItem::defaultShortcuts();
        items[1].enabled = false; // Discord
        items[2].enabled = false; // League
        QuickActionItem cursor = items.takeAt(3);
        items.prepend(cursor);
        items.push_back({QStringLiteral("custom:vscode"), QStringLiteral("VS Code"),
                         QuickActionType::CustomApp, QStringLiteral("C:/Apps/Code.exe"), {}, true});

        menu.setShortcuts(items);
        QCOMPARE(menu.actionCount(), 5);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:cursor")), 0);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:spotify")), 1);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:discord")), -1);
        QCOMPARE(menu.indexOfId(QStringLiteral("custom:vscode")), 4);

        menu.showMenu(petRect, spriteLocal);
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);
        // Cursor is now visually above Spotify
        QVERIFY(menu.restingVisualRect(0).center().y() < menu.restingVisualRect(1).center().y());
    }

    void quickActionsEditorAndCustomLauncherIntegration()
    {
        QuickActionsEditor editor;
        editor.setShortcuts(QuickActionItem::defaultShortcuts());
        QCOMPARE(editor.shortcuts().size(), 6);

        QSignalSpy changedSpy(&editor, &QuickActionsEditor::shortcutsChanged);

        // Toggle, reorder, add, edit, and remove custom app
        editor.setShortcutEnabled(QStringLiteral("builtin:discord"), false);
        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(editor.shortcuts().at(1).enabled, false);

        editor.moveShortcut(3, 0); // Move Cursor to top
        QCOMPARE(editor.shortcuts().at(0).id, QStringLiteral("builtin:cursor"));

        QuickActionItem custom{QStringLiteral("custom:obsidian"), QStringLiteral("Obsidian"),
                               QuickActionType::CustomApp, QStringLiteral("C:/Apps/Obsidian.exe"), {}, true};
        editor.addShortcut(custom);
        QCOMPARE(editor.shortcuts().size(), 7);

        custom.displayName = QStringLiteral("Obsidian Notes");
        editor.updateShortcut(custom);
        QCOMPARE(editor.shortcuts().last().displayName, QStringLiteral("Obsidian Notes"));

        editor.removeShortcut(QStringLiteral("custom:obsidian"));
        QCOMPARE(editor.shortcuts().size(), 6);

        // Built-in items cannot be removed (only disabled)
        editor.removeShortcut(QStringLiteral("builtin:settings"));
        QCOMPARE(editor.shortcuts().size(), 6);

        // Mocked custom application launcher (running, launched, missing, failed)
        ExternalAppLauncher launcher;
        QString launchedPath;
        launcher.setCustomBackend({
            [](const QString& p) { return p.contains(QStringLiteral("running")); },
            [](const QString& p) { return !p.contains(QStringLiteral("missing")); },
            [&](const QString& p) {
                launchedPath = p;
                return !p.contains(QStringLiteral("fail"));
            }
        });
        QCOMPARE(launcher.openOrActivateCustom(QStringLiteral("App"), QStringLiteral("C:/running.exe")).status,
                 ExternalAppLauncher::Status::ActivatedRunning);
        QCOMPARE(launcher.openOrActivateCustom(QStringLiteral("App"), QStringLiteral("C:/ok.exe")).status,
                 ExternalAppLauncher::Status::LaunchedApp);
        QCOMPARE(launchedPath, QStringLiteral("C:/ok.exe"));
        QCOMPARE(launcher.openOrActivateCustom(QStringLiteral("App"), QStringLiteral("C:/missing.exe")).status,
                 ExternalAppLauncher::Status::NotInstalled);
        QCOMPARE(launcher.openOrActivateCustom(QStringLiteral("App"), QStringLiteral("C:/fail.exe")).status,
                 ExternalAppLauncher::Status::LaunchFailed);
    }

    void testInstalledAppDiscoveryAndFiltering()
    {
        int providerCalls = 0;
        InstalledAppDiscovery::setTestProvider([&providerCalls]() {
            ++providerCalls;
            return QVector<InstalledAppDiscovery::DetectedApp>{
                {QStringLiteral("Visual Studio Code"), QStringLiteral("C:/Apps/Code.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("Google Chrome"), QStringLiteral("C:/Apps/chrome.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("visual studio code"), QStringLiteral("C:/Apps/Code.exe"), QStringLiteral("Installed App")},
                {QStringLiteral("Notion"), QStringLiteral("C:/Apps/Notion.exe"), QStringLiteral("Installed App")},
            };
        });

        QVector<InstalledAppDiscovery::DetectedApp> first;
        bool firstReady = false;
        InstalledAppDiscovery::discoverAsync(this, [&](const auto& apps) {
            first = apps;
            firstReady = true;
        }, true);
        QTRY_VERIFY_WITH_TIMEOUT(firstReady, 2000);
        QCOMPARE(providerCalls, 1);
        QCOMPARE(first.size(), 3); // Deduplicated & alphabetically sorted
        QCOMPARE(first[0].displayName, QStringLiteral("Google Chrome"));
        QCOMPARE(first[1].displayName, QStringLiteral("Notion"));
        QCOMPARE(first[2].displayName, QStringLiteral("Visual Studio Code"));

        // Second call without forceRefresh uses cache
        QVector<InstalledAppDiscovery::DetectedApp> cached;
        bool cachedReady = false;
        InstalledAppDiscovery::discoverAsync(this, [&](const auto& apps) {
            cached = apps;
            cachedReady = true;
        }, false);
        QTRY_VERIFY_WITH_TIMEOUT(cachedReady, 2000);
        QCOMPARE(providerCalls, 1);
        QCOMPARE(cached.size(), 3);

        // Search filtering matches display name or subtitle
        const auto codeMatches = InstalledAppDiscovery::filter(first, QStringLiteral("code"));
        QCOMPARE(codeMatches.size(), 1);
        QCOMPARE(codeMatches[0].displayName, QStringLiteral("Visual Studio Code"));

        const auto emptyMatches = InstalledAppDiscovery::filter(first, QStringLiteral("nonexistent"));
        QVERIFY(emptyMatches.isEmpty());

        InstalledAppDiscovery::setTestProvider({});
    }

    void testQuickActionDialogDragOverlapSearchAndEdit()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString fakeAppPath = tempDir.filePath(QStringLiteral("FakeStudio.exe"));
        const QString otherAppPath = tempDir.filePath(QStringLiteral("AnotherApp.exe"));
        for (const QString& p : {fakeAppPath, otherAppPath}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("stub");
            f.close();
        }

        InstalledAppDiscovery::setTestProvider([fakeAppPath, otherAppPath]() {
            return QVector<InstalledAppDiscovery::DetectedApp>{
                {QStringLiteral("Fake Studio"), fakeAppPath, QStringLiteral("Start Menu")},
                {QStringLiteral("Another App"), otherAppPath, QStringLiteral("Installed App")},
            };
        });

        QWidget mockSettings(nullptr, Qt::Tool | Qt::FramelessWindowHint);
        mockSettings.setGeometry(120, 120, 342, 420);
        mockSettings.show();
        QTest::qWait(20);

        QuickActionDialog addDlg(nullptr, &mockSettings);
        addDlg.show();
        QTest::qWait(20);

        // Initial placement beside Settings should not overlap it
        QVERIFY(addDlg.overlapRectLocal().isEmpty());

        // Detected apps loaded asynchronously and searchable
        QVERIFY(addDlg.detectedAppsList() != nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(addDlg.detectedAppsList()->count(), 2, 2000);
        addDlg.searchEdit()->setText(QStringLiteral("Fake"));
        QCOMPARE(addDlg.detectedAppsList()->count(), 1);

        // Selecting a detected app populates Name and Launch Reference
        addDlg.selectDetectedAppRow(0);
        QCOMPARE(addDlg.nameEdit()->text(), QStringLiteral("Fake Studio"));
        QCOMPARE(addDlg.pathEdit()->text(), fakeAppPath);

        // Dragging via header moves the dialog over Settings and computes overlap rect
        const QPoint startPos = addDlg.pos();
        const QPoint headerLocal = addDlg.headerWidget()->geometry().center();
        const QPoint pressGlobal = addDlg.mapToGlobal(headerLocal);
        const QPoint targetGlobal = mockSettings.frameGeometry().center();

        QMouseEvent press(QEvent::MouseButtonPress, headerLocal, pressGlobal,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&addDlg, &press);

        QMouseEvent moveEv(QEvent::MouseMove, headerLocal, targetGlobal,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&addDlg, &moveEv);

        QMouseEvent release(QEvent::MouseButtonRelease, headerLocal, targetGlobal,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&addDlg, &release);

        QVERIFY(addDlg.pos() != startPos);
        QVERIFY(!addDlg.overlapRectLocal().isEmpty());

        // Edit mode preserves stable id, enabled state, and order; Cancel makes no changes
        QuickActionItem existing;
        existing.id = QStringLiteral("custom:stable-123");
        existing.type = QuickActionType::CustomApp;
        existing.displayName = QStringLiteral("Original Name");
        existing.applicationReference = fakeAppPath;
        existing.enabled = false;

        QuickActionDialog editDlg(&existing, &mockSettings);
        QCOMPARE(editDlg.nameEdit()->text(), QStringLiteral("Original Name"));
        QCOMPARE(editDlg.pathEdit()->text(), fakeAppPath);
        editDlg.nameEdit()->setText(QStringLiteral("Edited Name"));
        editDlg.reject(); // Cancel discards changes
        QCOMPARE(editDlg.result(), static_cast<int>(QDialog::Rejected));
        QCOMPARE(editDlg.resultItem().id, QStringLiteral("custom:stable-123"));
        QCOMPARE(editDlg.resultItem().enabled, false);

        InstalledAppDiscovery::setTestProvider({});
    }

    void testSettingsHasDedicatedQuickActionsSection()
    {
        AppSettings settings;
        settings.load();
        SettingsPanel panel(&settings, nullptr, nullptr, nullptr, nullptr);

        auto* qaSection = panel.findChild<CollapsibleSection*>(QStringLiteral("quickActionsSection"));
        QVERIFY(qaSection != nullptr);
        QVERIFY(!qaSection->isExpanded()); // Collapsed by default to keep Settings compact

        // Both the icon-size slider and shortcut editor live inside the Quick Actions section
        auto* slider = qaSection->findChild<QSlider*>(QStringLiteral("quickActionIconSizeSlider"));
        auto* editor = qaSection->findChild<QuickActionsEditor*>(QStringLiteral("quickActionsEditor"));
        QVERIFY(slider != nullptr);
        QVERIFY(editor != nullptr);

        // Adjusting icon-size slider inside Quick Actions still updates settings
        slider->setValue(130);
        QCOMPARE(settings.quickActionIconScale(), 130);
    }
};

QTEST_MAIN(TestQuickActions)
#include "test_quick_actions.moc"
