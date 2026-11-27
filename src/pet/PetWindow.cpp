#include "pet/PetWindow.h"

#include "alarm/AlarmNotification.h"
#include "alarm/AlarmScheduler.h"
#include "alarm/AlarmSoundManager.h"
#include "pet/AnimationManager.h"
#include "app/AppSettings.h"
#include "timer/CompletionToast.h"
#include "pet/MovementController.h"
#include "ui/OnboardingWindow.h"
#include "ui/PixelTheme.h"
#include "quickactions/QuickActionMenu.h"
#include "ui/SettingsPanel.h"
#include "quickactions/ExternalAppLauncher.h"
#include "timer/StudyTimer.h"
#include "app/SystemTrayManager.h"
#include "timer/TimerBubble.h"
#include "alarm/UISoundManager.h"
#include "app/WindowCoordinator.h"
#include "study/QuestionScheduler.h"
#include "study/QuizController.h"
#include "ui/QuizPopup.h"
#include "ai/OllamaClient.h"
#include "app/SettingsDefaults.h"
#include "study/ProgressTracker.h"
#include "study/QuestionGenerator.h"
#include "study/QuestionSelector.h"
#include "study/StudyLibrary.h"
#include "study/WrittenAnswerEvaluator.h"
#include "ui/StudyLibraryWindow.h"

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
#  include "platform/MacWindowHelper.h"
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
        updateCheckInScheduling();
    });

    setupCheckIns();

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
    if (m_questionScheduler) {
        m_questionScheduler->stop();
    }
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
    updateCheckInScheduling();
    applyStudyPreferences();
    if (m_ollama && m_ollama->model() != m_settings->studyModel()) {
        m_ollama->setModel(m_settings->studyModel());
        m_generator->setModelName(m_ollama->model());
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
    if (m_quizPopup && m_quizPopup->isVisible()) {
        m_quizPopup->dismiss();
    }
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
    if (m_questionScheduler) {
        m_questionScheduler->stop();
    }
    if (m_quizPopup) {
        m_quizPopup->hide();
    }
    if (m_libraryWindow) {
        m_libraryWindow->hide();
    }
    if (m_generator) {
        m_generator->cancel();
    }
    if (m_studyLibrary) {
        m_studyLibrary->cancelAll();
    }
    if (m_ollama) {
        m_ollama->cancelAll();
        m_ollama->cancelPull();
    }
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
    if (m_quizPopup && m_quizPopup->isVisible()) {
        m_quizPopup->followPet(frameGeometry());
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
    // The question bubble occupies the same spot above the pet; keep the countdown out of its way.
    const bool questionOpen = m_quizPopup && m_quizPopup->isVisible();
    if (!m_hovering || !m_timer->isActive() || questionOpen) {
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

void PetWindow::setupCheckIns()
{
    m_quizController = std::make_unique<QuizController>(m_settings.get());
    m_questionScheduler = std::make_unique<QuestionScheduler>();
    m_questionScheduler->setCanAskPredicate([this]() { return canShowCheckIn(); });
    m_quizPopup = std::make_unique<QuizPopup>(nullptr);

    connect(m_questionScheduler.get(), &QuestionScheduler::questionDue, this, [this]() {
        ensureStudy();
        applyStudyPreferences();
        m_quizController->ask();   // silently nothing when no questions are available yet
    });
    connect(m_quizController.get(), &QuizController::questionReady, this, [this](const Question& question) {
        if (m_bubble && m_bubble->isVisible()) {
            m_bubble->fadeOut();
        }
        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::QuestionOpen, true);
        }
        m_quizPopup->showQuestion(question, frameGeometry());
    });
    connect(m_quizController.get(), &QuizController::progressChanged, this, [this]() {
        if (m_settingsPanel) {
            m_settingsPanel->refreshStudySummary();
        }
    });
    connect(m_quizPopup.get(), &QuizPopup::skipRequested, m_quizController.get(), &QuizController::skip);
    connect(m_quizPopup.get(), &QuizPopup::choiceSubmitted, this, [this](const QString& optionId) {
        m_quizController->submitChoice(optionId);
    });
    connect(m_quizPopup.get(), &QuizPopup::selectionSubmitted, this, [this](const QStringList& optionIds) {
        m_quizController->submitSelection(QSet<QString>(optionIds.cbegin(), optionIds.cend()));
    });
    connect(m_quizPopup.get(), &QuizPopup::writtenSubmitted, this, [this](const QString& answer, bool allowSuggestion) {
        if (m_quizController->submitWritten(answer, allowSuggestion) == QuizController::WrittenStep::Judging) {
            m_quizPopup->showJudging();
        }
    });
    connect(m_quizPopup.get(), &QuizPopup::revealRequested, this, [this]() {
        if (m_quizController->reveal()) {
            m_quizPopup->showReveal();
        }
    });
    connect(m_quizPopup.get(), &QuizPopup::selfAssessed, this, [this](int rating) {
        m_quizController->selfAssess(static_cast<SelfRating>(rating));
    });
    connect(m_quizPopup.get(), &QuizPopup::postponeRequested, this, [this]() {
        // No penalty: close this question and ask again in a few minutes.
        m_quizController->skip();
        m_questionScheduler->postpone(std::chrono::minutes(5));
    });
    connect(m_quizPopup.get(), &QuizPopup::noticeActionTriggered, this, &PetWindow::openStudyLibrary);
    connect(m_quizController.get(), &QuizController::evaluationReady, this, [this](const Evaluation& evaluation, int xp) {
        m_quizPopup->showEvaluation(evaluation, xp);
        refreshStudyProgress();
    });
    connect(m_quizController.get(), &QuizController::suggestionOffered, m_quizPopup.get(), &QuizPopup::showSuggestion);
    connect(m_quizPopup.get(), &QuizPopup::closed, this, [this]() {
        m_quizController->dismiss();
        if (m_movement) {
            m_movement->setPaused(MovementController::PauseReason::QuestionOpen, false);
        }
    });
    connect(m_quizPopup.get(), &QuizPopup::buttonActivated, this, [this]() {
        if (m_uiSound) {
            m_uiSound->playClick();
        }
    });

    connect(m_settingsPanel.get(), &SettingsPanel::askQuestionRequested, this, &PetWindow::askQuestionNow);
    connect(m_settingsPanel.get(), &SettingsPanel::studyProgressResetRequested, this, [this]() {
        m_quizController->resetProgress();
        if (m_progressTracker) {
            m_progressTracker->reset(0);
        }
        refreshStudyProgress();
    });
    connect(m_settingsPanel.get(), &SettingsPanel::studyLibraryRequested, this, &PetWindow::openStudyLibrary);

    // Someone who already set up a course gets the study stack (and its course list) right away;
    // everyone else only pays for it when they first use the Study App.
    if (m_settings->studyCourseId() > 0) {
        ensureStudy();
    }
    applyStudyPreferences();
    updateCheckInScheduling();
}

void PetWindow::ensureStudy()
{
    if (m_studyLibrary) {
        return;
    }
    m_studyLibrary = std::make_unique<StudyLibrary>();
    m_ollama = std::make_unique<OllamaClient>();
    m_ollama->setModel(m_settings->studyModel());
    m_progressTracker = std::make_unique<ProgressTracker>(m_studyLibrary->database());
    m_generator = std::make_unique<QuestionGenerator>(m_studyLibrary->database(), m_ollama.get());
    m_generator->setModelName(m_ollama->model());
    m_writtenEvaluator = std::make_unique<WrittenAnswerEvaluator>(m_ollama.get());

    auto provider = std::make_unique<StudyQuestionProvider>(m_studyLibrary->database(), m_progressTracker.get());
    m_studyProvider = provider.get();
    m_quizController->addProvider(std::move(provider));
    m_quizController->setProgressTracker(m_progressTracker.get());
    m_quizController->setWrittenEvaluator(m_writtenEvaluator.get());

    connect(m_studyLibrary.get(), &StudyLibrary::documentsChanged, this, &PetWindow::refreshStudyCourses);
    refreshStudyCourses();
    refreshStudyProgress();
}

void PetWindow::applyStudyPreferences()
{
    if (!m_quizController || !m_settings) {
        return;
    }
    m_quizController->setActiveProvider(QStringLiteral("study"));
    if (!m_studyProvider) {
        return;   // the study stack is configured when it is first created
    }
    SelectionPrefs prefs;
    prefs.enabled.clear();
    const int mask = m_settings->studyFormats();
    if (mask & SettingsDefaults::kStudyFormatSingleChoice) {
        prefs.enabled.append(QuestionType::SingleChoice);
    }
    if (mask & SettingsDefaults::kStudyFormatMultiSelect) {
        prefs.enabled.append(QuestionType::MultiSelect);
    }
    if (mask & SettingsDefaults::kStudyFormatWritten) {
        prefs.enabled.append(QuestionType::Written);
    }
    prefs.mode = m_settings->studyFormatMode() == SettingsDefaults::kStudyFormatModeFocus ? FormatMode::Focus
                                                                                          : FormatMode::Mixed;
    switch (m_settings->studyFocusFormat()) {
    case SettingsDefaults::kStudyFormatMultiSelect:
        prefs.focus = QuestionType::MultiSelect;
        break;
    case SettingsDefaults::kStudyFormatWritten:
        prefs.focus = QuestionType::Written;
        break;
    default:
        prefs.focus = QuestionType::SingleChoice;
        break;
    }
    m_studyProvider->setCourse(m_settings->studyCourseId());
    m_studyProvider->setPreferences(prefs);
}

void PetWindow::refreshStudyProgress()
{
    if (!m_progressTracker || !m_settingsPanel) {
        return;
    }
    const StudyProgress p = m_progressTracker->progress(m_settings->studyCourseId());
    const int answered = p.attempted + p.selfAssessed;
    QString text = QStringLiteral("%1 XP · %2 answered").arg(p.totalXp()).arg(answered);
    if (!p.reviewTopics.isEmpty()) {
        text += QStringLiteral(" · %1 to review").arg(p.reviewTopics.size());
    }
    m_settingsPanel->setStudyProgressText(text);
}

void PetWindow::refreshStudyCourses()
{
    if (!m_studyLibrary || !m_settingsPanel) {
        return;
    }
    const QVector<Course> courses = m_studyLibrary->database()->courses();
    bool known = false;
    for (const Course& c : courses) {
        known = known || c.id == m_settings->studyCourseId();
    }
    if (!known) {
        // The selected course was deleted: fall back to the first one (or none).
        m_settings->setStudyCourseId(courses.isEmpty() ? 0 : courses.first().id);
        m_settings->save();
    }
    m_settingsPanel->setStudyCourses(courses);
    applyStudyPreferences();
    refreshStudyProgress();
}

void PetWindow::showNoStudyQuestionsNotice()
{
    if (!m_quizPopup || !m_studyLibrary) {
        return;
    }
    const qint64 course = m_settings->studyCourseId();
    QString title;
    QString body;
    if (course <= 0) {
        title = QStringLiteral("Your Study Library is empty");
        body = QStringLiteral("Import lecture slides, PDFs, or notes and Chiikawa will quiz you on them, right on this computer.");
    } else if (m_studyLibrary->database()->courseQuestions(course, {}).isEmpty()) {
        title = QStringLiteral("No questions yet");
        body = QStringLiteral("Import materials for this course and press Generate in the Study Library.");
    } else {
        title = QStringLiteral("No questions in your chosen formats");
        body = QStringLiteral("Turn on more question formats, or generate questions in these formats.");
    }
    if (m_movement) {
        m_movement->setPaused(MovementController::PauseReason::QuestionOpen, true);
    }
    m_quizPopup->showNotice(title, body, QStringLiteral("Open Study Library"), frameGeometry());
}

void PetWindow::openStudyLibrary()
{
    ensureStudy();
    if (!m_libraryWindow) {
        m_libraryWindow = std::make_unique<StudyLibraryWindow>(m_settings.get(), m_studyLibrary.get(), m_generator.get(),
                                                               m_ollama.get(), m_progressTracker.get(), m_uiSound.get(),
                                                               nullptr);
        connect(m_libraryWindow.get(), &StudyLibraryWindow::courseChanged, this, [this](qint64) { refreshStudyCourses(); });
        connect(m_libraryWindow.get(), &StudyLibraryWindow::preferencesChanged, this, [this]() {
            if (m_ollama && m_ollama->model() != m_settings->studyModel()) {
                m_ollama->setModel(m_settings->studyModel());
            }
            applyStudyPreferences();
            refreshStudyProgress();
        });
    }
    if (m_quizPopup && m_quizPopup->isVisible()) {
        m_quizPopup->dismiss();
    }
    m_libraryWindow->showNear(frameGeometry());
}


void PetWindow::updateCheckInScheduling()
{
    if (!m_questionScheduler || !m_settings || !m_timer) {
        return;
    }
    if (!m_settings->checkInsEnabled()) {
        m_questionScheduler->cancelPostponed();
    }
    const bool wanted = m_settings->checkInsEnabled() && m_timer->isRunning();
    if (!wanted) {
        m_questionScheduler->stop();
        return;
    }
    const std::chrono::milliseconds interval = std::chrono::minutes(m_settings->checkInMinutes());
    if (!m_questionScheduler->isRunning()) {
        m_questionScheduler->setBaseInterval(interval);
        m_questionScheduler->start();
    } else if (m_questionScheduler->baseInterval() != interval) {
        m_questionScheduler->setBaseInterval(interval); // restarts the countdown
    }
}

bool PetWindow::canShowCheckIn() const
{
    if (!isVisible() || m_dragging || m_exitPending) {
        return false;
    }
    if (m_quizPopup && m_quizPopup->isVisible()) {
        return false;
    }
    if (m_movement
        && (m_movement->isPausedFor(MovementController::PauseReason::QuickActionMenuOpen)
            || m_movement->isPausedFor(MovementController::PauseReason::Dragging))) {
        return false;
    }
    const auto visible = [](const QWidget* w) { return w && w->isVisible(); };
    if (visible(m_settingsPanel.get()) || visible(m_onboardingWindow.get()) || visible(m_libraryWindow.get())
        || visible(m_alarmNotification.get()) || visible(m_completionToast.get())
        || visible(m_appNotice.get()) || visible(m_quickActionMenu.get())) {
        return false;
    }
    return QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr;
}

void PetWindow::askQuestionNow()
{
    if (!m_quizController || (m_quizPopup && m_quizPopup->isVisible())) {
        return;
    }
    if (!isVisible()) {
        showPet();
    }
    ensureStudy();
    applyStudyPreferences();
    if (!m_quizController->ask()) {
        showNoStudyQuestionsNotice();
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
