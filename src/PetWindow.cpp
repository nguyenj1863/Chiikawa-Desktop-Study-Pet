#include "PetWindow.h"

#include "AlarmNotification.h"
#include "AlarmScheduler.h"
#include "AlarmSoundManager.h"
#include "AnimationManager.h"
#include "AppSettings.h"
#include "CompletionToast.h"
#include "MovementController.h"
#include "OnboardingWindow.h"
#include "PixelTheme.h"
#include "QuickActionMenu.h"
#include "SettingsPanel.h"
#include "ExternalAppLauncher.h"
#include "StudyTimer.h"
#include "SystemTrayManager.h"
#include "TimerBubble.h"
#include "UISoundManager.h"
#include "WindowCoordinator.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QRegion>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include "MacWindowHelper.h"
#endif

PetWindow::PetWindow(QWidget* parent)
    : QWidget(parent)
{
    m_settings = std::make_unique<AppSettings>();
    m_timer = std::make_unique<StudyTimer>(this);
    m_soundManager = std::make_unique<AlarmSoundManager>(this);
    m_uiSound = std::make_unique<UISoundManager>(this);
    m_alarmScheduler = std::make_unique<AlarmScheduler>(
        m_settings.get(), AlarmScheduler::WallClockFn{}, AlarmScheduler::MonotonicClockFn{}, this);

    setupWindowFlags();
    setupUi();

    m_animations = std::make_unique<AnimationManager>(m_petLabel, this);
    m_animations->setTargetSize(m_settings->petSize());
    m_animations->setAutoSwitchEnabled(m_settings->autoAnimationEnabled());
    connect(m_animations.get(), &AnimationManager::frameUpdated,
            this, [this](const QPixmap& frame, AnimationId id) {
                const QRect nextHit = computeInteractiveHitRect(frame.toImage(), id, size());
                if (nextHit != m_interactiveHitRect) {
                    m_interactiveHitRect = nextHit;
                    update();
                }
            });
    if (!m_animations->loadAll()) {
        m_petLabel->setText(QStringLiteral("GIF?"));
    }

    m_movement = std::make_unique<MovementController>(this, this);
    m_movement->setEnabled(m_settings->movementEnabled());
    m_movement->setSpeedMultiplier(m_settings->movementSpeed());

    m_bubble = std::make_unique<TimerBubble>(nullptr);
    m_settingsPanel = std::make_unique<SettingsPanel>(
        m_settings.get(), m_timer.get(), m_alarmScheduler.get(), m_soundManager.get(), m_uiSound.get(), nullptr);
    m_coordinator = std::make_unique<WindowCoordinator>(this, m_settingsPanel.get(), this);
    m_completionToast = std::make_unique<CompletionToast>(nullptr);
    m_alarmNotification = std::make_unique<AlarmNotification>(nullptr);
    m_onboardingWindow = std::make_unique<OnboardingWindow>(m_uiSound.get(), nullptr);
    m_quickActionMenu = std::make_unique<QuickActionMenu>(nullptr);
    m_quickActionMenu->setAnchorWidget(this);
    m_quickActionMenu->setIconScalePercent(m_settings->quickActionIconScale());
    m_quickActionMenu->setShortcuts(m_settings->quickActions());
    m_appNotice = std::make_unique<AppLaunchNoticePopup>(nullptr);
    m_appLauncher = std::make_unique<ExternalAppLauncher>();

    connect(m_quickActionMenu.get(), &QuickActionMenu::activeStateChanged,
            this, [this](bool active) {
                if (m_movement) {
                    m_movement->setPaused(MovementController::PauseReason::QuickActionMenuOpen, active);
                }
                if (m_animations) {
                    m_animations->setAutoSwitchSuspended(
                        AnimationManager::AutoSwitchSuspendReason::QuickActionMenuOpen, active);
                }
            });
    connect(m_quickActionMenu.get(), &QuickActionMenu::shortcutTriggered,
            this, [this](const QuickActionItem& item) {
                if (item.type != QuickActionType::CustomApp) {
                    return;
                }
                if (m_uiSound) {
                    m_uiSound->playClick();
                }
                if (!m_appLauncher) {
                    return;
                }
                const ExternalAppLauncher::Result result =
                    m_appLauncher->openOrActivateCustom(item.displayName, item.applicationReference);
                if ((result.status == ExternalAppLauncher::Status::NotInstalled
                     || result.status == ExternalAppLauncher::Status::LaunchFailed)
                    && m_appNotice) {
                    m_appNotice->showNear(
                        frameGeometry(),
                        result.title,
                        result.message,
                        result.fallbackActionLabel,
                        result.fallbackUrl);
                }
            });
    connect(m_quickActionMenu.get(), &QuickActionMenu::settingsRequested,
            this, &PetWindow::onSettingsQuickAction);
    connect(m_quickActionMenu.get(), &QuickActionMenu::externalAppRequested,
            this, &PetWindow::onExternalAppQuickAction);
    connect(m_quickActionMenu.get(), &QuickActionMenu::exitRequested,
            this, &PetWindow::onExitQuickAction);
    connect(m_animations.get(), &AnimationManager::animationChanged,
            this, [this](AnimationId) {
                if (m_quickActionMenu) {
                    m_quickActionMenu->updateSpriteBounds(frameGeometry(), currentVisibleSpriteBounds());
                }
            });
    connect(m_appNotice.get(), &AppLaunchNoticePopup::buttonActivated, this, [this]() {
        if (m_uiSound) {
            m_uiSound->playClick();
        }
    });
    connect(m_appNotice.get(), &AppLaunchNoticePopup::fallbackUrlRequested, this, [](const QUrl& url) {
        if (url.isValid()) {
            QDesktopServices::openUrl(url);
        }
    });

    connect(m_onboardingWindow.get(), &OnboardingWindow::onboardingCompleted,
            this, &PetWindow::onOnboardingFinished);
    connect(m_onboardingWindow.get(), &OnboardingWindow::onboardingSkipped,
            this, &PetWindow::onOnboardingFinished);
    connect(m_onboardingWindow.get(), &OnboardingWindow::onboardingExitRequested,
            this, &PetWindow::exitApplication);

    connect(m_completionToast.get(), &CompletionToast::buttonActivated, this, [this]() {
        if (m_uiSound) {
            m_uiSound->playClick();
        }
    });
    connect(m_completionToast.get(), &CompletionToast::startAgainRequested, this, [this]() {
        m_completionHandled = false;
        m_timer->configure(std::chrono::seconds{m_settings->lastTimerSeconds()});
        m_timer->start();
    });

    connect(m_alarmScheduler.get(), &AlarmScheduler::alarmTriggered,
            this, &PetWindow::onAlarmTriggered);
    connect(m_alarmNotification.get(), &AlarmNotification::snoozeRequested,
            this, &PetWindow::onAlarmSnoozed);
    connect(m_alarmNotification.get(), &AlarmNotification::dismissRequested,
            this, &PetWindow::onAlarmDismissed);

    m_snoozeBubbleTimer.setSingleShot(true);
    connect(&m_snoozeBubbleTimer, &QTimer::timeout, this, [this]() {
        if (m_bubble && !m_hovering) {
            m_bubble->fadeOut();
        }
    });

    m_tray = std::make_unique<SystemTrayManager>(m_timer.get(), this);
    m_tray->show();

    connect(m_movement.get(), &MovementController::facingChanged,
            this, &PetWindow::onFacingChanged);
    connect(m_movement.get(), &MovementController::moved,
            this, &PetWindow::onPositionMoved);

    connect(m_timer.get(), &StudyTimer::remainingChanged,
            this, &PetWindow::onTimerRemaining);
    connect(m_timer.get(), &StudyTimer::completed,
            this, &PetWindow::onTimerCompleted);
    connect(m_timer.get(), &StudyTimer::stateChanged, this, [this](StudyTimer::State state) {
        if (state == StudyTimer::State::Running) {
            m_completionHandled = false;
        }
    });

    connect(m_settingsPanel.get(), &SettingsPanel::settingsApplied,
            this, &PetWindow::applySettings);
    connect(m_settingsPanel.get(), &SettingsPanel::showTutorialRequested,
            this, &PetWindow::showTutorialReplay);
    connect(m_settingsPanel.get(), &SettingsPanel::exitApplicationRequested,
            this, &PetWindow::exitApplication);
    connect(m_settingsPanel.get(), &SettingsPanel::closedByUser, this, [this]() {
        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::SettingsOpen, false);
        }
    });
    connect(m_settingsPanel.get(), &SettingsPanel::dragRequested,
            m_coordinator.get(), &WindowCoordinator::moveSettingsTo);
    connect(m_settingsPanel.get(), &SettingsPanel::dragFinished, this, [this]() {
        if (m_settings) {
            m_settings->setLastPosition(pos());
            m_settings->save();
        }
    });
    connect(m_settingsPanel.get(), &SettingsPanel::panelResized,
            m_coordinator.get(), &WindowCoordinator::repositionAfterSettingsResize);
    connect(m_coordinator.get(), &WindowCoordinator::petMoved,
            this, &PetWindow::onPositionMoved);

    auto playTrayClick = [this]() {
        if (m_uiSound) {
            m_uiSound->playClick();
        }
    };
    connect(m_tray.get(), &SystemTrayManager::showPetRequested, this, [this, playTrayClick]() {
        playTrayClick();
        showPet();
    });
    connect(m_tray.get(), &SystemTrayManager::hidePetRequested, this, [this, playTrayClick]() {
        playTrayClick();
        hidePet();
    });
    connect(m_tray.get(), &SystemTrayManager::startTimerRequested, this, [this, playTrayClick]() {
        playTrayClick();
        startTimerFromTray();
    });
    connect(m_tray.get(), &SystemTrayManager::pauseResumeTimerRequested, this, [this, playTrayClick]() {
        playTrayClick();
        pauseResumeFromTray();
    });
    connect(m_tray.get(), &SystemTrayManager::openSettingsRequested, this, [this, playTrayClick]() {
        playTrayClick();
        openSettings();
    });
    connect(m_tray.get(), &SystemTrayManager::showTutorialRequested, this, [this, playTrayClick]() {
        playTrayClick();
        showTutorialReplay();
    });
    connect(m_tray.get(), &SystemTrayManager::exitRequested, this, [this, playTrayClick]() {
        playTrayClick();
        exitApplication();
    });

    m_singleClickTimer.setSingleShot(true);
    connect(&m_singleClickTimer, &QTimer::timeout, this, [this]() {
        if (m_animations) {
            m_animations->playRandom(true);
        }
    });

    placeInitialPosition();
    const int initialSize = m_settings->petSize();
    setFixedSize(initialSize, initialSize);
    if (m_petLabel) {
        m_petLabel->setFixedSize(initialSize, initialSize);
    }
    applySettings();
    m_alarmScheduler->restoreFromSettings();
}

PetWindow::~PetWindow()
{
    if (m_settings) {
        m_settings->setLastPosition(pos());
        m_settings->save();
    }
}

void PetWindow::setupWindowFlags()
{
#ifdef Q_OS_MACOS
    // Qt::NoDropShadowWindowHint disables macOS NSWindow's cached alpha shadow, which
    // otherwise freezes the first sprite frame's silhouette behind subsequent frames.
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Window | Qt::NoDropShadowWindowHint;
#else
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint;
#endif
    if (DesktopLayerHelper::isSupported() && m_settings->desktopOnlyMode()) {
        flags |= Qt::WindowStaysOnBottomHint;
    } else {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
}

void PetWindow::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_petLabel = new QLabel(this);
    m_petLabel->setAlignment(Qt::AlignCenter);
    m_petLabel->setAttribute(Qt::WA_TranslucentBackground);
    // Let PetWindow receive clicks/drags; the label is display-only.
    m_petLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_petLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    layout->addWidget(m_petLabel);
}

void PetWindow::setExternalAppLauncher(std::unique_ptr<ExternalAppLauncher> launcher)
{
    m_appLauncher = launcher ? std::move(launcher) : std::make_unique<ExternalAppLauncher>();
}

QPoint PetWindow::defaultBottomRight() const
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return {100, 100};
    }
    const QRect avail = screen->availableGeometry();
    const int size = m_settings ? m_settings->petSize() : 120;
    return {avail.right() - size - 24, avail.bottom() - size - 24};
}

bool PetWindow::isSavedPositionSafe(const QPoint& pos) const
{
    const QRect probe(pos, QSize(m_settings->petSize(), m_settings->petSize()));
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (screen->availableGeometry().intersects(probe)) {
            return true;
        }
    }
    return false;
}

void PetWindow::placeInitialPosition()
{
    if (m_settings->hasSavedPosition() && isSavedPositionSafe(m_settings->lastPosition())) {
        move(m_settings->lastPosition());
    } else {
        move(defaultBottomRight());
    }
}

void PetWindow::applySettings()
{
    const int size = m_settings->petSize();
    const QSize oldSize = this->size();
    const bool sizeChanged = (oldSize.width() != size || oldSize.height() != size);

    if (sizeChanged) {
        const QPoint oldPos = pos();
        const int oldW = std::max(1, oldSize.width());
        const int oldH = std::max(1, oldSize.height());
        int newX = oldPos.x() + (oldW - size) / 2;
        int newY = oldPos.y() + (oldH - size) / 2;

        // When SettingsPanel is open, grow away from it so the pet never collides
        // with or pushes the settings window out from under the user's cursor.
        if (m_settingsPanel && m_settingsPanel->isVisible()) {
            const QRect panelRect = m_settingsPanel->frameGeometry();
            if (panelRect.center().x() >= oldPos.x() + oldW / 2) {
                // Settings is on the right: keep the pet's right edge anchored.
                newX = (oldPos.x() + oldW) - size;
            } else {
                // Settings is on the left: keep the pet's left edge anchored.
                newX = oldPos.x();
            }
        }

        const QRect bounds = WindowCoordinator::availableGeometryFor(
            QPoint(newX + size / 2, newY + size / 2));
        const QPoint clampedPos = WindowCoordinator::clampTopLeft({newX, newY}, QSize(size, size), bounds);

        setFixedSize(size, size);
        if (m_petLabel) {
            m_petLabel->setFixedSize(size, size);
        }
        if (clampedPos != oldPos) {
            move(clampedPos);
        }
    } else {
        setFixedSize(size, size);
        if (m_petLabel) {
            m_petLabel->setFixedSize(size, size);
        }
    }

    if (m_animations) {
        m_animations->setTargetSize(size);
        m_animations->setAutoSwitchEnabled(m_settings->autoAnimationEnabled());
    }
    if (m_quickActionMenu) {
        m_quickActionMenu->setIconScalePercent(m_settings->quickActionIconScale());
        m_quickActionMenu->setShortcuts(m_settings->quickActions());
        m_quickActionMenu->updateSpriteBounds(frameGeometry(), currentVisibleSpriteBounds());
    }
    if (m_movement) {
        m_movement->setSpeedMultiplier(m_settings->movementSpeed());
        m_movement->setEnabled(m_settings->movementEnabled());
        const bool onboardingActive = m_onboardingWindow && m_onboardingWindow->isVisible();
        m_movement->setPaused(MovementController::PauseReason::Hidden, !isVisible());
        m_movement->setPaused(MovementController::PauseReason::SettingsOpen,
                              m_settingsPanel && m_settingsPanel->isVisible());
        m_movement->setPaused(MovementController::PauseReason::Onboarding, onboardingActive);
        m_movement->setPaused(MovementController::PauseReason::Dragging, m_dragging);
    }

    if (m_soundManager) {
        m_soundManager->setEnabled(m_settings->soundEnabled());
        m_soundManager->setSelectedSound(m_settings->alarmSoundId());
        m_soundManager->setVolume(m_settings->alarmVolume());
    }
    if (m_uiSound) {
        m_uiSound->setEnabled(m_settings->buttonSoundsEnabled());
    }
    if (m_alarmNotification) {
        m_alarmNotification->setSnoozeMinutes(m_settings->snoozeMinutes());
    }

    const bool wantDesktopOnly = m_settings->desktopOnlyMode();
    if (!m_desktopLayerInitialized || m_appliedDesktopOnlyMode != wantDesktopOnly) {
        m_desktopLayer.applyMode(this, wantDesktopOnly);
        m_desktopLayerInitialized = true;
        m_appliedDesktopOnlyMode = wantDesktopOnly;
        // Keep settings accessible above normal windows while open even in Desktop-Only Mode
        if (m_settingsPanel) {
            m_settingsPanel->setAlwaysOnTop(true);
        }
    }

    if (m_quickActionMenu) {
        m_quickActionMenu->updateAnchor(frameGeometry(), currentVisibleSpriteBounds());
    }
    if (!sizeChanged) {
        m_movement->constrainToScreen();
    }
    if (m_coordinator) {
        // Keep SettingsPanel stationary while resizing so dragging the size slider
        // never shifts the panel out from under the cursor.
        m_coordinator->syncRelativeOffset();
    }
    updateHitMask();
}

void PetWindow::updateHitMask()
{
    // Approximate opaque hit area: full widget. Fine-grained alpha hit-testing
    // is handled in nativeEvent on Windows when a pixmap is available.
    clearMask();
}

void PetWindow::startApplication()
{
    if (m_onboardingWindow) {
        // Always show onboarding on startup before revealing the desktop pet
        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::Onboarding, true);
        }
        m_onboardingWindow->openOnboarding(false);
        return;
    }
    showPet();
}

void PetWindow::showTutorialReplay()
{
    if (!m_onboardingWindow) {
        return;
    }
    if (m_onboardingWindow->isVisible()) {
        m_onboardingWindow->raise();
        m_onboardingWindow->activateWindow();
        return;
    }
    if (m_settingsPanel && m_settingsPanel->isVisible()) {
        m_settingsPanel->hide();
        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::SettingsOpen, false);
        }
    }
    if (m_movement) {
        m_movement->setPaused(MovementController::PauseReason::Onboarding, true);
    }
    m_onboardingWindow->openOnboarding(true);
}

void PetWindow::onOnboardingFinished(bool wasReplay)
{
    if (m_movement) {
        m_movement->setPaused(MovementController::PauseReason::Onboarding, false);
    }
    if (!wasReplay) {
        if (m_settings) {
            m_settings->setOnboardingCompleted(true);
            m_settings->save();
        }
        showPet();
        return;
    }
}

void PetWindow::showPet()
{
    show();
    raise();
}

void PetWindow::hidePet()
{
    hide();
    if (m_quickActionMenu) {
        m_quickActionMenu->hideImmediately();
    }
    if (m_appNotice) {
        m_appNotice->dismiss();
    }
    if (m_bubble) {
        m_bubble->hide();
    }
    if (m_completionToast) {
        m_completionToast->dismiss();
    }
}

void PetWindow::openSettings()
{
    if (m_movement) {
        m_movement->setPaused(MovementController::PauseReason::SettingsOpen, true);
    }
    hideQuickActions();
    if (!m_settingsPanel) {
        return;
    }

    QScreen* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    m_settingsPanel->setAlwaysOnTop(true);
    m_settingsPanel->prepareToShow(screen);
    if (m_coordinator) {
        m_coordinator->attachSettings();
    }
    m_settingsPanel->show();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(m_settingsPanel.get(), false);
#endif
    m_settingsPanel->raise();
    m_settingsPanel->activateWindow();
}

void PetWindow::startTimerFromTray()
{
    m_completionHandled = false;
    m_timer->configure(std::chrono::seconds{m_settings->lastTimerSeconds()});
    m_timer->start();
}

void PetWindow::pauseResumeFromTray()
{
    if (m_timer->isRunning()) {
        m_timer->pause();
    } else if (m_timer->isPaused()) {
        m_timer->resume();
    }
}

void PetWindow::exitApplication()
{
    m_singleClickTimer.stop();
    m_snoozeBubbleTimer.stop();
    if (m_quickActionMenu) {
        m_quickActionMenu->hideImmediately();
    }
    if (m_appNotice) {
        m_appNotice->dismiss();
    }
    if (m_soundManager) {
        m_soundManager->stop();
    }
    if (m_movement) {
        m_movement->pause();
    }
    if (m_animations) {
        m_animations->setAutoSwitchEnabled(false);
    }
    if (m_timer) {
        m_timer->cancel();
    }
    if (m_settingsPanel) {
        m_settingsPanel->hide();
    }
    if (m_onboardingWindow) {
        m_onboardingWindow->hide();
    }
    if (m_alarmNotification) {
        m_alarmNotification->closeNotification();
    }
    if (m_bubble) {
        m_bubble->hide();
    }
    if (m_completionToast) {
        m_completionToast->hide();
    }
    if (m_settings) {
        m_settings->setLastPosition(pos());
        m_settings->save();
    }
    if (m_tray) {
        m_tray->hide();
    }
    QApplication::quit();
}

void PetWindow::onFacingChanged(bool facingLeft)
{
    if (m_animations) {
        m_animations->setMirrored(!facingLeft);
    }
}

void PetWindow::onPositionMoved(const QPoint& pos)
{
    m_settings->setLastPosition(pos);
    if (m_quickActionMenu) {
        m_quickActionMenu->updateAnchor(frameGeometry(), currentVisibleSpriteBounds());
    }
    if (m_hovering && m_timer->isActive()) {
        updateHoverBubble();
    }
}

void PetWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragMoved = false;
        m_singleClickTimer.stop();
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::Dragging, true);
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint newPos = event->globalPosition().toPoint() - m_dragOffset;
        if (m_coordinator) {
            m_coordinator->movePetTo(newPos);
        } else {
            move(newPos);
            if (m_movement) {
                m_movement->constrainToScreen();
            }
        }
        m_dragMoved = true;
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_settings->setLastPosition(pos());
        m_settings->save();

        if (!m_dragMoved) {
            // Defer single-click so a double-click can cancel it
            m_singleClickTimer.start(QApplication::doubleClickInterval());
        }

        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::Dragging, false);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_singleClickTimer.stop();
        if (m_quickActionMenu) {
            m_quickActionMenu->toggleMenu(frameGeometry(), currentVisibleSpriteBounds());
        }
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

QRect PetWindow::currentVisibleSpriteBounds() const
{
    if (m_animations) {
        return m_animations->currentVisibleBounds(size());
    }
    return rect();
}

void PetWindow::showQuickActions()
{
    if (!m_quickActionMenu) {
        return;
    }
    m_quickActionMenu->showMenu(frameGeometry(), currentVisibleSpriteBounds());
}

void PetWindow::hideQuickActions()
{
    if (!m_quickActionMenu) {
        return;
    }
    m_quickActionMenu->hideMenu();
}

void PetWindow::onSettingsQuickAction()
{
    if (m_uiSound) {
        m_uiSound->playClick();
    }
    openSettings();
}

void PetWindow::onExternalAppQuickAction(ExternalAppLauncher::AppId app)
{
    if (m_uiSound) {
        m_uiSound->playClick();
    }
    if (!m_appLauncher) {
        return;
    }
    const ExternalAppLauncher::Result result = m_appLauncher->openOrActivate(app);
    if ((result.status == ExternalAppLauncher::Status::NotInstalled
         || result.status == ExternalAppLauncher::Status::LaunchFailed)
        && m_appNotice) {
        m_appNotice->showNear(
            frameGeometry(),
            result.title,
            result.message,
            result.fallbackActionLabel,
            result.fallbackUrl);
    }
}

void PetWindow::onExitQuickAction()
{
    if (m_exitPending) {
        return;
    }
    m_exitPending = true;
    if (m_uiSound) {
        m_uiSound->playClick();
    }
    // Brief bounded visual feedback before running the shared clean Qt shutdown path.
    QTimer::singleShot(120, this, &PetWindow::exitApplication);
}

void PetWindow::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    m_hovering = true;
    if (m_quickActionMenu) {
        m_quickActionMenu->resetAutoHideTimer();
    }
    updateHoverBubble();
}

void PetWindow::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    m_hovering = false;
    if (m_bubble) {
        m_bubble->fadeOut();
    }
}

void PetWindow::updateHoverBubble()
{
    if (!m_hovering || !m_timer->isActive()) {
        if (m_bubble && m_bubble->isVisible()) {
            m_bubble->fadeOut();
        }
        return;
    }

    QString text;
    const auto rem = m_timer->remaining();
    if (m_timer->isPaused()) {
        text = QStringLiteral("Paused · %1").arg(StudyTimer::formatRemaining(rem));
    } else {
        text = QStringLiteral("%1 remaining").arg(StudyTimer::formatRemaining(rem));
    }
    m_bubble->updateText(text);
    m_bubble->showNear(frameGeometry());
}

void PetWindow::onTimerRemaining(std::chrono::milliseconds remaining)
{
    Q_UNUSED(remaining);
    if (m_hovering) {
        updateHoverBubble();
    }
}

void PetWindow::playCompletionFeedback()
{
    if (!m_settings->soundEnabled()) {
        return;
    }
    if (m_soundManager) {
        m_soundManager->playOnce();
    }
}

void PetWindow::onAlarmTriggered(const QTime& scheduledTime, bool fromSnooze)
{
    if (m_animations) {
        m_animations->playCelebration();
    }
    if (m_soundManager && m_settings->soundEnabled()) {
        m_soundManager->playAlarmSequence();
    }
    if (m_alarmNotification && m_settings->alarmNotificationEnabled()) {
        QRect anchor = frameGeometry();
        if (!isVisible()) {
            if (QScreen* primary = QGuiApplication::primaryScreen()) {
                const QRect avail = primary->availableGeometry();
                anchor = QRect(avail.center().x() - 60, avail.center().y() - 60, 120, 120);
            }
        }
        m_alarmNotification->showAlarm(anchor,
                                       scheduledTime,
                                       m_settings->snoozeMinutes(),
                                       fromSnooze);
    }
    if (m_tray) {
        m_tray->notify(QStringLiteral("Time to go!"),
                       QStringLiteral("Your Chiikawa alarm is ringing!"));
    }
}

void PetWindow::onAlarmSnoozed(int minutes)
{
    if (m_uiSound) {
        m_uiSound->playClick();
    }
    if (m_soundManager) {
        m_soundManager->stop();
    }
    if (!m_alarmScheduler || !m_alarmScheduler->snooze(minutes)) {
        return;
    }
    if (m_bubble) {
        m_bubble->updateText(QStringLiteral("Snoozed for %1m... %1 more minutes...").arg(minutes));
        m_bubble->showNear(frameGeometry());
        m_snoozeBubbleTimer.start(3500);
    }
}

void PetWindow::onAlarmDismissed()
{
    if (m_uiSound) {
        m_uiSound->playClick();
    }
    if (m_soundManager) {
        m_soundManager->stop();
    }
    if (m_alarmScheduler) {
        m_alarmScheduler->dismiss();
    }
}

void PetWindow::showCompletionToast()
{
    if (!m_completionToast) {
        return;
    }
    // Single anchored bottom-right toast on the primary monitor; independent of pet position
    m_completionToast->showNotification();
}

void PetWindow::onTimerCompleted()
{
    if (m_completionHandled) {
        return;
    }
    m_completionHandled = true;

    if (m_animations) {
        m_animations->playCelebration();
    }
    showCompletionToast();
    playCompletionFeedback();
    if (m_bubble && m_hovering) {
        m_bubble->updateText(QStringLiteral("Done!"));
    }
}

QRect PetWindow::computeInteractiveHitRect(const QImage& frame,
                                           AnimationId animId,
                                           const QSize& windowSize)
{
    if ((animId != AnimationId::Fire && animId != AnimationId::Skip)
        || frame.isNull() || windowSize.isEmpty()) {
        return {};
    }

    const QImage rgba = (frame.format() == QImage::Format_ARGB32 || frame.format() == QImage::Format_ARGB32_Premultiplied)
        ? frame
        : frame.convertToFormat(QImage::Format_ARGB32);

    int minX = rgba.width();
    int minY = rgba.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < rgba.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
        for (int x = 0; x < rgba.width(); ++x) {
            if (qAlpha(row[x]) > 10) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return {};
    }

    const qreal dpr = std::max<qreal>(1.0, rgba.devicePixelRatio());
    const int logicalW = qRound(rgba.width() / dpr);
    const int logicalH = qRound(rgba.height() / dpr);
    const int logicalMinX = qRound(minX / dpr);
    const int logicalMinY = qRound(minY / dpr);
    const int logicalMaxX = qRound(maxX / dpr);
    const int logicalMaxY = qRound(maxY / dpr);

    const int offsetX = (windowSize.width() - logicalW) / 2;
    const int offsetY = (windowSize.height() - logicalH) / 2;
    QRect hitRect(logicalMinX + offsetX, logicalMinY + offsetY,
                  logicalMaxX - logicalMinX + 1, logicalMaxY - logicalMinY + 1);

    if (animId == AnimationId::Fire) {
        // Expand around the fast-moving small fire sprite so surrounding negative space is clickable/hoverable
        const int pad = std::max(14, std::min(windowSize.width(), windowSize.height()) / 6);
        hitRect = hitRect.adjusted(-pad, -pad, pad, pad);
    }

    return hitRect.intersected(QRect(QPoint(0, 0), windowSize));
}

void PetWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_interactiveHitRect.isEmpty()) {
        return;
    }
    // Windows WS_EX_LAYERED passes hits through alpha==0 pixels; alpha=1 is invisible
    // while keeping the target region clickable and hoverable.
    QPainter p(this);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 1));
    if (m_animations && m_animations->currentAnimation() == AnimationId::Fire) {
        p.drawRoundedRect(m_interactiveHitRect, 12, 12);
    } else {
        p.fillRect(m_interactiveHitRect, QColor(0, 0, 0, 1));
    }
}

void PetWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_quickActionMenu) {
        m_quickActionMenu->updateAnchor(frameGeometry(), currentVisibleSpriteBounds());
    }
    updateHitMask();
}

void PetWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    if (m_quickActionMenu) {
        m_quickActionMenu->updateAnchor(frameGeometry(), currentVisibleSpriteBounds());
    }
}

bool PetWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (m_desktopLayer.handleNativeEvent(this, eventType, message, result)) {
        return true;
    }
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            // Accept the whole pet HWND as clickable. Per-pixel / DPI-mismatched
            // hit testing previously returned HTTRANSPARENT and ate all clicks.
            *result = HTCLIENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void PetWindow::closeEvent(QCloseEvent* event)
{
    // Closing the pet window hides it; use tray Exit to quit
    event->ignore();
    hidePet();
}

void PetWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_quickActionMenu) {
        m_quickActionMenu->hideImmediately();
    }
    if (m_appNotice) {
        m_appNotice->dismiss();
    }
    if (m_movement) {
        m_movement->setPaused(MovementController::PauseReason::Hidden, true);
    }
    if (m_animations) {
        m_animations->setAutoSwitchSuspended(AnimationManager::AutoSwitchSuspendReason::Hidden, true);
    }
    if (m_bubble) {
        m_bubble->hide();
    }
    if (m_completionToast) {
        m_completionToast->dismiss();
    }
}

void PetWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, true);
#endif
    if (m_animations) {
        m_animations->setAutoSwitchSuspended(AnimationManager::AutoSwitchSuspendReason::Hidden, false);
    }
    if (m_movement) {
        m_movement->setPaused(MovementController::PauseReason::Hidden, false);
    }
}
