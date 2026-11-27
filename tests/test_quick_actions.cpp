#include <QtTest/QtTest>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QSlider>

#include "quickactions/AnimatedQuickActionButton.h"
#include "pet/AnimationManager.h"
#include "app/AppSettings.h"
#include "app/WindowCoordinator.h"
#include "ui/CollapsibleSection.h"
#include "quickactions/ExternalAppLauncher.h"
#include "quickactions/InstalledAppDiscovery.h"
#include "pet/MovementController.h"
#include "pet/PetWindow.h"
#include "quickactions/QuickActionItem.h"
#include "quickactions/QuickActionMenu.h"
#include "quickactions/QuickActionsEditor.h"
#include "ui/PixelTheme.h"
#include "ui/SettingsPanel.h"

#include <cmath>
#include <memory>

namespace {
[[maybe_unused]] const bool kIsolatedSettingsStore = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetTests"));
    return true;
}();
} // namespace

class TestQuickActions : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ChiikawaStudyPetTest"));
        QCoreApplication::setApplicationName(QStringLiteral("TestQuickActions"));
        clearSettingsStore();
    }

    void cleanup()
    {
        clearSettingsStore();
    }

private:
    static void clearSettingsStore()
    {
        QSettings settings(AppSettings::storageOrganization(), AppSettings::storageApplication());
        settings.clear();
        settings.sync();
    }

private slots:

    void defaultIconsOrderTooltipsAndAccessibleNames()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);

        QCOMPARE(menu.actionCount(), 7);

        QVERIFY(menu.spotifyButton() != nullptr);
        QVERIFY(menu.discordButton() != nullptr);
        QVERIFY(menu.leagueButton() != nullptr);
        QVERIFY(menu.vscodeButton() != nullptr);
        QVERIFY(menu.studyButton() != nullptr);
        QVERIFY(menu.settingsButton() != nullptr);
        QVERIFY(menu.exitButton() != nullptr);

        QCOMPARE(menu.spotifyButton()->toolTip(), QStringLiteral("Open Spotify"));
        QCOMPARE(menu.discordButton()->toolTip(), QStringLiteral("Open Discord"));
        QCOMPARE(menu.leagueButton()->toolTip(), QStringLiteral("Open League of Legends"));
        QCOMPARE(menu.vscodeButton()->toolTip(), QStringLiteral("Open Visual Studio Code"));
        QCOMPARE(menu.studyButton()->toolTip(), QStringLiteral("Open Study"));
        QCOMPARE(menu.settingsButton()->toolTip(), QStringLiteral("Open Settings"));
        QCOMPARE(menu.exitButton()->toolTip(), QStringLiteral("Exit Chiikawa"));

        QCOMPARE(menu.spotifyButton()->accessibleName(), QStringLiteral("Open Spotify"));
        QCOMPARE(menu.discordButton()->accessibleName(), QStringLiteral("Open Discord"));
        QCOMPARE(menu.leagueButton()->accessibleName(), QStringLiteral("Open League of Legends"));
        QCOMPARE(menu.vscodeButton()->accessibleName(), QStringLiteral("Open Visual Studio Code"));
        QCOMPARE(menu.studyButton()->accessibleName(), QStringLiteral("Open Study"));

        // Study sits directly above Settings; VS Code takes the old Cursor slot.
        const QVector<QuickActionItem> defaults = QuickActionItem::defaultShortcuts();
        QStringList ids;
        for (const QuickActionItem& d : defaults) {
            ids.append(d.id);
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("builtin:spotify"), QStringLiteral("builtin:discord"),
                                   QStringLiteral("builtin:league"), QStringLiteral("builtin:vscode"),
                                   QStringLiteral("builtin:study"), QStringLiteral("builtin:settings"),
                                   QStringLiteral("builtin:exit")}));
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

    void hoveringAnIconShowsItsNameInsideTheOverlay()
    {
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(1);
        menu.setAutoHideDurationMs(60000);
        menu.showMenu(QRect(400, 300, 120, 120), QRect(15, 15, 90, 90));
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);
        QVERIFY(menu.nameLabelText().isEmpty());

        const QStringList expected = {QStringLiteral("Spotify"), QStringLiteral("Discord"),
                                      QStringLiteral("League of Legends"), QStringLiteral("Visual Studio Code"),
                                      QStringLiteral("Study"), QStringLiteral("Settings"), QStringLiteral("Exit")};
        QCOMPARE(menu.actionCount(), static_cast<int>(expected.size()));
        for (int i = 0; i < menu.actionCount(); ++i) {
            menu.setHoveredIndex(i);
            QCOMPARE(menu.nameLabelText(), expected.at(i));
            const QRect label = menu.nameLabelGeometry();
            QVERIFY(!label.isEmpty());
            QVERIFY2(QRect(QPoint(0, 0), menu.size()).contains(label), qPrintable(expected.at(i)));
        }
        menu.setHoveredIndex(-1);
        QVERIFY(menu.nameLabelText().isEmpty());

        // Leaving the menu or closing it never leaves a stale name behind.
        menu.setHoveredIndex(4);
        QVERIFY(!menu.nameLabelText().isEmpty());
        menu.hideImmediately();
        QVERIFY(menu.nameLabelText().isEmpty());
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

        // Hover VS Code while open, then toggle closed: hover must resolve cleanly while dismissing
        menu.setHoveredIndex(3);
        QTRY_VERIFY_WITH_TIMEOUT(menu.vscodeButton()->hoverOffset().y() < -5.0, 300);

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

    void vsCodeIsABuiltInAppAndCursorIsNot()
    {
        ExternalAppLauncher::AppId id = ExternalAppLauncher::AppId::Spotify;
        QVERIFY(ExternalAppLauncher::tryParseBuiltInAppId(QStringLiteral("vscode"), id));
        QCOMPARE(id, ExternalAppLauncher::AppId::VSCode);
        QVERIFY(!ExternalAppLauncher::tryParseBuiltInAppId(QStringLiteral("cursor"), id));

        const ExternalAppLauncher::AppMetadata meta = ExternalAppLauncher::metadata(ExternalAppLauncher::AppId::VSCode);
        QCOMPARE(meta.displayName, QStringLiteral("Visual Studio Code"));
        QCOMPARE(meta.fallbackUrl, QUrl(QStringLiteral("https://code.visualstudio.com")));

        // A missing installation is reported with a download link instead of failing silently.
        ExternalAppLauncher launcher;
        launcher.setBackend(ExternalAppLauncher::AppId::VSCode, {
            []() { return false; }, []() { return false; }, []() { return false; }});
        const ExternalAppLauncher::Result result = launcher.openOrActivate(ExternalAppLauncher::AppId::VSCode);
        QCOMPARE(result.status, ExternalAppLauncher::Status::NotInstalled);
        QCOMPARE(result.fallbackUrl, meta.fallbackUrl);

        // Only VS Code resolves an installed path; looking it up never throws or blocks.
        QVERIFY(ExternalAppLauncher::installedAppPath(ExternalAppLauncher::AppId::Spotify).isEmpty());
        const QString path = ExternalAppLauncher::installedAppPath(ExternalAppLauncher::AppId::VSCode);
        QVERIFY(path.isEmpty() || QFileInfo::exists(path));

        // The menu button always has an icon: the installed app's own, or the drawn fallback.
        QuickActionItem item{QStringLiteral("builtin:vscode"), QStringLiteral("Visual Studio Code"),
                             QuickActionType::BuiltInApp, QStringLiteral("vscode"), {}, true};
        QVERIFY(!QuickActionItem::resolveIcon(item, 24).isNull());
    }

    void externalAppLauncherMockedAllFourApps()
    {
        const std::array<ExternalAppLauncher::AppId, 4> apps = {
            ExternalAppLauncher::AppId::Spotify,
            ExternalAppLauncher::AppId::Discord,
            ExternalAppLauncher::AppId::LeagueOfLegends,
            ExternalAppLauncher::AppId::VSCode
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
        int vscodeCalls = 0;

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
        launcher->setBackend(ExternalAppLauncher::AppId::VSCode, {
            [&]() { ++vscodeCalls; return true; },
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
        pet.quickActionMenu()->vscodeButton()->click();
        QCOMPARE(vscodeCalls, 1);

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

        // Disable Discord & League, reorder the built-in VS Code to the top, add a custom app
        QuickActionMenu menu;
        menu.setNativeOutsideObserverEnabled(false);
        menu.setReducedMotionOverride(1);
        QVector<QuickActionItem> items = QuickActionItem::defaultShortcuts();
        items[1].enabled = false; // Discord
        items[2].enabled = false; // League
        QuickActionItem vscode = items.takeAt(3);
        items.prepend(vscode);
        items.push_back({QStringLiteral("custom:editor"), QStringLiteral("My Editor"),
                         QuickActionType::CustomApp, QStringLiteral("C:/Apps/Code.exe"), {}, true});

        menu.setShortcuts(items);
        QCOMPARE(menu.actionCount(), 6);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:vscode")), 0);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:spotify")), 1);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:discord")), -1);
        QCOMPARE(menu.indexOfId(QStringLiteral("builtin:study")), 2);
        QCOMPARE(menu.indexOfId(QStringLiteral("custom:editor")), 5);

        menu.showMenu(petRect, spriteLocal);
        QCOMPARE(menu.state(), QuickActionMenu::State::Open);
        // VS Code is now visually above Spotify
        QVERIFY(menu.restingVisualRect(0).center().y() < menu.restingVisualRect(1).center().y());
    }

    void quickActionsEditorAndCustomLauncherIntegration()
    {
        QuickActionsEditor editor;
        editor.setShortcuts(QuickActionItem::defaultShortcuts());
        QCOMPARE(editor.shortcuts().size(), 7);

        QSignalSpy changedSpy(&editor, &QuickActionsEditor::shortcutsChanged);

        // Toggle, reorder, add, edit, and remove custom app
        editor.setShortcutEnabled(QStringLiteral("builtin:discord"), false);
        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(editor.shortcuts().at(1).enabled, false);

        editor.moveShortcut(3, 0); // Move VS Code to top
        QCOMPARE(editor.shortcuts().at(0).id, QStringLiteral("builtin:vscode"));

        QuickActionItem custom{QStringLiteral("custom:obsidian"), QStringLiteral("Obsidian"),
                               QuickActionType::CustomApp, QStringLiteral("C:/Apps/Obsidian.exe"), {}, true};
        editor.addShortcut(custom);
        QCOMPARE(editor.shortcuts().size(), 8);

        custom.displayName = QStringLiteral("Obsidian Notes");
        editor.updateShortcut(custom);
        QCOMPARE(editor.shortcuts().last().displayName, QStringLiteral("Obsidian Notes"));

        editor.removeShortcut(QStringLiteral("custom:obsidian"));
        QCOMPARE(editor.shortcuts().size(), 7);

        // Built-in items cannot be removed (only disabled)
        editor.removeShortcut(QStringLiteral("builtin:settings"));
        editor.removeShortcut(QStringLiteral("builtin:study"));
        QCOMPARE(editor.shortcuts().size(), 7);

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

    void testDiscoveryPrioritizesUserFacingApps()
    {
        using App = InstalledAppDiscovery::DetectedApp;
        InstalledAppDiscovery::setTestProvider([]() {
            return QVector<App>{
                {QStringLiteral("Steam"), QStringLiteral("C:/Program Files (x86)/Steam/steam.exe"), QStringLiteral("Steam")},
                {QStringLiteral("Uninstall Steam"), QStringLiteral("C:/Program Files (x86)/Steam/uninstall.exe"), QStringLiteral("Steam")},
                {QStringLiteral("Google Chrome"), QStringLiteral("C:/Program Files/Google/Chrome/chrome.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("Google Docs"), QStringLiteral("C:/Program Files/Google/Docs/docs.exe"), QStringLiteral("Chrome Apps")},
                {QStringLiteral("Discord"), QStringLiteral("C:/Users/me/AppData/Local/Discord/Update.exe"), QStringLiteral("Discord Inc")},
                {QStringLiteral("Visual Studio Code"), QStringLiteral("C:/Apps/Code/Code.exe"), QStringLiteral("Visual Studio Code")},
                {QStringLiteral("Event Viewer"), QStringLiteral("C:/Apps/eventvwr.exe"), QStringLiteral("Windows Tools")},
                {QStringLiteral("Command Prompt"), QStringLiteral("C:/Apps/cmd.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("Notepad"), QStringLiteral("C:/Windows/System32/notepad.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("Contoso Readme"), QStringLiteral("C:/Apps/Contoso/readme.txt"), QStringLiteral("Contoso")},
                {QStringLiteral("Contoso Website"), QStringLiteral("C:/Apps/Contoso/site.url"), QStringLiteral("Contoso")},
                {QStringLiteral("Contoso Help"), QStringLiteral("C:/Apps/Contoso/help.chm"), QStringLiteral("Contoso")},
                {QStringLiteral("Disk Utility"), QStringLiteral("/System/Applications/Utilities/Disk Utility.app"), QStringLiteral("Utilities")},
                // Duplicate registrations of the same executable collapse into one entry
                {QStringLiteral("Code"), QStringLiteral("C:/apps/code/code.exe"), QStringLiteral("Registered App")},
            };
        });

        const QVector<App> apps = InstalledAppDiscovery::discover(true);
        QCOMPARE(apps.size(), 13);

        QStringList suggested;
        QStringList other;
        for (const App& app : apps) {
            (app.suggested ? suggested : other).push_back(app.displayName);
        }
        QCOMPARE(suggested, (QStringList{
            QStringLiteral("Discord"), QStringLiteral("Google Chrome"), QStringLiteral("Google Docs"),
            QStringLiteral("Steam"), QStringLiteral("Visual Studio Code")}));
        QCOMPARE(other, (QStringList{
            QStringLiteral("Command Prompt"), QStringLiteral("Contoso Help"), QStringLiteral("Contoso Readme"),
            QStringLiteral("Contoso Website"), QStringLiteral("Disk Utility"), QStringLiteral("Event Viewer"),
            QStringLiteral("Notepad"), QStringLiteral("Uninstall Steam")}));

        // Default list shows suggestions only; Show All lists everything
        QCOMPARE(InstalledAppDiscovery::filter(apps, QString(), false).size(), 5);
        QCOMPARE(InstalledAppDiscovery::filter(apps, QString(), true).size(), 13);

        // Search still finds uncommon apps even when Show All is off
        const auto eventMatches = InstalledAppDiscovery::filter(apps, QStringLiteral("event"), false);
        QCOMPARE(eventMatches.size(), 1);
        QCOMPARE(eventMatches[0].displayName, QStringLiteral("Event Viewer"));

        // Suggested matches are listed before system or maintenance matches
        const auto steamMatches = InstalledAppDiscovery::filter(apps, QStringLiteral("steam"), false);
        QCOMPARE(steamMatches.size(), 2);
        QCOMPARE(steamMatches[0].displayName, QStringLiteral("Steam"));
        QCOMPARE(steamMatches[1].displayName, QStringLiteral("Uninstall Steam"));

        InstalledAppDiscovery::setTestProvider({});
    }

    void testDiscoveryDropsBrokenShortcuts()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString targetPath = tempDir.filePath(QStringLiteral("Target.exe"));
        QFile target(targetPath);
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write("stub");
        target.close();
        const QString brokenTarget = tempDir.filePath(QStringLiteral("Removed.exe"));
        const QString workingLink = tempDir.filePath(QStringLiteral("Working.lnk"));
        const QString brokenLink = tempDir.filePath(QStringLiteral("Broken.lnk"));

        using App = InstalledAppDiscovery::DetectedApp;
        InstalledAppDiscovery::setTestProvider([=]() {
            return QVector<App>{
                {QStringLiteral("Working"), workingLink, QStringLiteral("Start Menu"), true, targetPath},
                {QStringLiteral("Broken"), brokenLink, QStringLiteral("Start Menu"), true, brokenTarget},
            };
        });
        const QVector<App> apps = InstalledAppDiscovery::discover(true);
        QCOMPARE(apps.size(), 1);
        QCOMPARE(apps[0].displayName, QStringLiteral("Working"));
        InstalledAppDiscovery::setTestProvider({});
    }

    void testDialogShowAllApplicationsToggle()
    {
        using App = InstalledAppDiscovery::DetectedApp;
        InstalledAppDiscovery::setTestProvider([]() {
            return QVector<App>{
                {QStringLiteral("Obsidian"), QStringLiteral("C:/Apps/Obsidian/Obsidian.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("Obsidian Uninstaller"), QStringLiteral("C:/Apps/Obsidian/Uninstall Obsidian.exe"), QStringLiteral("Start Menu")},
                {QStringLiteral("Registry Editor"), QStringLiteral("C:/Apps/regedit.exe"), QStringLiteral("Windows Tools")},
            };
        });

        QuickActionDialog dlg;
        QVERIFY(dlg.showAllAppsCheck() != nullptr);
        QVERIFY(qobject_cast<PixelCheckBox*>(dlg.showAllAppsCheck()) != nullptr);
        QVERIFY(!dlg.showAllAppsCheck()->isChecked());
        QTRY_COMPARE_WITH_TIMEOUT(dlg.detectedAppsList()->count(), 1, 2000);
        QVERIFY(dlg.detectedAppsList()->item(0)->text().startsWith(QStringLiteral("Obsidian")));

        dlg.showAllAppsCheck()->setChecked(true);
        QCOMPARE(dlg.detectedAppsList()->count(), 3);

        dlg.showAllAppsCheck()->setChecked(false);
        dlg.searchEdit()->setText(QStringLiteral("registry"));
        QCOMPARE(dlg.detectedAppsList()->count(), 1);
        dlg.selectDetectedAppRow(0);
        QCOMPARE(dlg.nameEdit()->text(), QStringLiteral("Registry Editor"));

        InstalledAppDiscovery::setTestProvider({});
    }

    void testQuickActionDialogFloatsIndependentlyWithNaturalTranslucency()
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
        const QPoint settingsPos = mockSettings.pos();

        QuickActionDialog addDlg(nullptr, &mockSettings);
        addDlg.placeBeside(mockSettings.frameGeometry());
        addDlg.show();
        QTest::qWait(20);

        // Floating, non-modal window: Settings stays interactive while it is open
        QVERIFY(!addDlg.isModal());
        QCOMPARE(addDlg.windowModality(), Qt::NonModal);
        QVERIFY(QApplication::activeModalWidget() == nullptr);

        // Initial placement sits beside Settings when the screen has room on either side
        // (small virtual screens, e.g. the 800x600 offscreen platform, fall back to clamping)
        const QRect settingsRect = mockSettings.frameGeometry();
        const QRect avail = WindowCoordinator::availableGeometryFor(settingsRect.center());
        const bool roomRight = settingsRect.right() + 1 + WindowCoordinator::kGap + addDlg.width() <= avail.right() + 1;
        const bool roomLeft = settingsRect.left() - WindowCoordinator::kGap - addDlg.width() >= avail.left();
        if (roomRight || roomLeft) {
            QVERIFY(!addDlg.frameGeometry().intersects(settingsRect));
        } else {
            QVERIFY(avail.contains(addDlg.frameGeometry().topLeft()));
        }

        // Detected apps loaded asynchronously and searchable
        QVERIFY(addDlg.detectedAppsList() != nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(addDlg.detectedAppsList()->count(), 2, 2000);
        addDlg.searchEdit()->setText(QStringLiteral("Fake"));
        QCOMPARE(addDlg.detectedAppsList()->count(), 1);

        // Selecting a detected app populates Name and Launch Reference
        addDlg.selectDetectedAppRow(0);
        QCOMPARE(addDlg.nameEdit()->text(), QStringLiteral("Fake Studio"));
        QCOMPARE(addDlg.pathEdit()->text(), fakeAppPath);

        // Per-pixel translucent glass with transparent rounded corners and opaque window opacity
        QVERIFY(addDlg.testAttribute(Qt::WA_TranslucentBackground));
        QCOMPARE(addDlg.windowOpacity(), 1.0);
        const QImage beforeOverlap = addDlg.grab().toImage();
        QCOMPARE(beforeOverlap.pixelColor(0, 0).alpha(), 0);
        const int glassAlpha = beforeOverlap.pixelColor(beforeOverlap.width() / 2, 9).alpha();
        QVERIFY2(glassAlpha > 0 && glassAlpha < 255,
                 qPrintable(QStringLiteral("Expected translucent dialog glass, got alpha %1").arg(glassAlpha)));

        // Dragging via header moves only the dialog, even across Settings
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
        QVERIFY(addDlg.frameGeometry().intersects(mockSettings.frameGeometry()));
        QCOMPARE(mockSettings.pos(), settingsPos);

        // Overlap is left to the compositor: the dialog paints identically over Settings
        QCOMPARE(addDlg.grab().toImage(), beforeOverlap);

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

    void testEditorDialogsAreNonModalAndClosedWithSettings()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString appPath = tempDir.filePath(QStringLiteral("Notes.exe"));
        {
            QFile f(appPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("stub");
        }
        InstalledAppDiscovery::setTestProvider([]() { return QVector<InstalledAppDiscovery::DetectedApp>{}; });

        AppSettings settings;
        settings.load();
        SettingsPanel panel(&settings, nullptr, nullptr, nullptr, nullptr);
        panel.move(200, 160);
        panel.show();
        QTest::qWait(20);

        QuickActionsEditor* editor = panel.quickActionsEditor();
        QVERIFY(editor != nullptr);

        QuickActionItem custom;
        custom.id = QStringLiteral("custom:notes");
        custom.type = QuickActionType::CustomApp;
        custom.displayName = QStringLiteral("Notes");
        custom.applicationReference = appPath;
        custom.enabled = true;
        editor->addShortcut(custom);
        const QVector<QuickActionItem> before = editor->shortcuts();
        const int notesIndex = static_cast<int>(before.size()) - 1;
        QCOMPARE(before[notesIndex].id, QStringLiteral("custom:notes"));

        // Add opens a non-blocking dialog; a second request raises the same one
        editor->openAddDialog();
        QPointer<QuickActionDialog> addDlg = editor->activeDialog();
        QVERIFY(addDlg);
        QVERIFY(addDlg->isVisible());
        QVERIFY(!addDlg->isModal());
        editor->openEditDialog(QStringLiteral("custom:notes"));
        QCOMPARE(editor->activeDialog(), addDlg.data());

        // Settings can still be dragged while the dialog is open, without moving the dialog
        const QPoint dialogPos = addDlg->pos();
        panel.move(panel.pos() + QPoint(40, 20));
        QCOMPARE(addDlg->pos(), dialogPos);

        // Cancel discards the pending addition
        editor->closeActiveDialog();
        QTRY_VERIFY(addDlg.isNull());
        QCOMPARE(editor->shortcuts(), before);

        // Edit + Save keeps id, enabled state, and position in the list
        editor->openEditDialog(QStringLiteral("custom:notes"));
        QPointer<QuickActionDialog> editDlg = editor->activeDialog();
        QVERIFY(editDlg);
        QVERIFY(editDlg->isEditing());
        QCOMPARE(editDlg->nameEdit()->text(), QStringLiteral("Notes"));
        editDlg->nameEdit()->setText(QStringLiteral("Study Notes"));
        editDlg->submit();
        QTRY_VERIFY(editDlg.isNull());
        const QVector<QuickActionItem> afterEdit = editor->shortcuts();
        QCOMPARE(afterEdit.size(), before.size());
        QCOMPARE(afterEdit[notesIndex].id, QStringLiteral("custom:notes"));
        QCOMPARE(afterEdit[notesIndex].displayName, QStringLiteral("Study Notes"));
        QCOMPARE(afterEdit[notesIndex].enabled, true);
        QCOMPARE(settings.quickActions(), afterEdit);

        // Closing Settings closes its transient dialog without applying unsaved edits
        editor->openEditDialog(QStringLiteral("custom:notes"));
        QPointer<QuickActionDialog> staleDlg = editor->activeDialog();
        QVERIFY(staleDlg);
        staleDlg->nameEdit()->setText(QStringLiteral("Unsaved Rename"));
        panel.hide();
        QTRY_VERIFY(staleDlg.isNull());
        QCOMPARE(editor->shortcuts(), afterEdit);

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

    void testQuickActionTogglesMatchPetPreferenceCheckboxes()
    {
        AppSettings settings;
        settings.load();
        SettingsPanel panel(&settings, nullptr, nullptr, nullptr, nullptr);

        QuickActionsEditor* editor = panel.quickActionsEditor();
        QVERIFY(editor != nullptr);
        const QList<QCheckBox*> toggles = editor->findChildren<QCheckBox*>(QStringLiteral("quickActionEnabledCheck"));
        QCOMPARE(toggles.size(), editor->shortcuts().size());
        for (QCheckBox* toggle : toggles) {
            QVERIFY(qobject_cast<PixelCheckBox*>(toggle) != nullptr);
        }

        PixelCheckBox* petCheck = nullptr;
        for (QCheckBox* cb : panel.findChildren<QCheckBox*>()) {
            if (cb->text() == QStringLiteral("Autonomous movement")) {
                petCheck = qobject_cast<PixelCheckBox*>(cb);
            }
        }
        QVERIFY(petCheck != nullptr);

        auto* qaToggle = qobject_cast<PixelCheckBox*>(toggles.first());
        // Indicator square (14px box drawn 4px from the left edge, vertically centered)
        auto indicator = [](PixelCheckBox* cb, bool checked) {
            constexpr int kIndicator = 14;
            QSignalBlocker blocker(cb);
            cb->setChecked(checked);
            cb->resize(cb->sizeHint());
            QImage img(cb->size(), QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            cb->render(&img, QPoint(), QRegion(), QWidget::DrawChildren);
            const int boxY = (cb->height() - kIndicator) / 2;
            return img.copy(QRect(0, boxY, kIndicator + 6, kIndicator));
        };
        // Pet Preferences is collapsed (zero height), so render an equivalent standalone reference
        PixelCheckBox petReference(petCheck->text());
        petReference.setFont(petCheck->font());
        QCOMPARE(indicator(qaToggle, true), indicator(&petReference, true));
        QCOMPARE(indicator(qaToggle, false), indicator(&petReference, false));
    }
};

QTEST_MAIN(TestQuickActions)
#include "test_quick_actions.moc"
