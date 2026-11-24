#pragma once

#include <QWidget>
#include <QPoint>
#include <QTimer>
#include <chrono>
#include <memory>

class QLabel;
class QPushButton;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class AppSettings;
class AnimationManager;
class MovementController;
class StudyTimer;
class TimerBubble;
class SettingsPanel;
class SystemTrayManager;
class CompletionToast;
class WindowCoordinator;
class AlarmScheduler;
class AlarmSoundManager;
class AlarmNotification;
class OnboardingWindow;
class UISoundManager;

#include "DesktopLayerHelper.h"

/**
 * Frameless translucent desktop pet window.
 * Orchestrates animation, movement, interactions, timer UI, and tray.
 */
class PetWindow : public QWidget {
    Q_OBJECT

public:
    explicit PetWindow(QWidget* parent = nullptr);
    ~PetWindow() override;

    [[nodiscard]] StudyTimer* studyTimer() const { return m_timer.get(); }
    [[nodiscard]] AppSettings* appSettings() const { return m_settings.get(); }
    [[nodiscard]] AlarmScheduler* alarmScheduler() const { return m_alarmScheduler.get(); }
    [[nodiscard]] MovementController* movementController() const { return m_movement.get(); }
    [[nodiscard]] OnboardingWindow* onboardingWindow() const { return m_onboardingWindow.get(); }

    /** Entry point after construction: shows first-launch onboarding or reveals the pet directly. */
    void startApplication();

public slots:
    void showPet();
    void hidePet();
    void openSettings();
    void showTutorialReplay();
    void applySettings();
    void startTimerFromTray();
    void pauseResumeFromTray();
    void exitApplication();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onTimerRemaining(std::chrono::milliseconds remaining);
    void onTimerCompleted();
    void onAlarmTriggered(const QTime& scheduledTime, bool fromSnooze);
    void onAlarmSnoozed(int minutes);
    void onAlarmDismissed();
    void onFacingChanged(bool facingLeft);
    void onPositionMoved(const QPoint& pos);
    void hideGear();
    void onGearClicked();
    void updateHoverBubble();
    void onOnboardingFinished(bool wasReplay);

private:
    void setupWindowFlags();
    void setupUi();
    void placeInitialPosition();
    [[nodiscard]] QPoint defaultBottomRight() const;
    [[nodiscard]] bool isSavedPositionSafe(const QPoint& pos) const;
    void updateHitMask();
    void showGear();
    void showCompletionToast();
    void playCompletionFeedback();

    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<StudyTimer> m_timer;
    std::unique_ptr<AlarmScheduler> m_alarmScheduler;
    std::unique_ptr<AlarmSoundManager> m_soundManager;
    std::unique_ptr<UISoundManager> m_uiSound;
    QLabel* m_petLabel = nullptr;
    std::unique_ptr<AnimationManager> m_animations;
    std::unique_ptr<MovementController> m_movement;
    std::unique_ptr<TimerBubble> m_bubble;
    std::unique_ptr<SettingsPanel> m_settingsPanel;
    std::unique_ptr<WindowCoordinator> m_coordinator;
    std::unique_ptr<SystemTrayManager> m_tray;
    std::unique_ptr<CompletionToast> m_completionToast;
    std::unique_ptr<AlarmNotification> m_alarmNotification;
    std::unique_ptr<OnboardingWindow> m_onboardingWindow;
    DesktopLayerHelper m_desktopLayer;

    QPushButton* m_gearButton = nullptr;
    QGraphicsOpacityEffect* m_gearOpacity = nullptr;
    QPropertyAnimation* m_gearFade = nullptr;
    QTimer m_gearHideTimer;
    QTimer m_snoozeBubbleTimer;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_dragOffset;
    bool m_hovering = false;
    bool m_completionHandled = false;
    QTimer m_singleClickTimer;
};
