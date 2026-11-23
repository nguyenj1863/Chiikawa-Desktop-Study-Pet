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

public slots:
    void showPet();
    void hidePet();
    void openSettings();
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
    void onFacingChanged(bool facingLeft);
    void onPositionMoved(const QPoint& pos);
    void hideGear();
    void onGearClicked();
    void updateHoverBubble();

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
    QLabel* m_petLabel = nullptr;
    std::unique_ptr<AnimationManager> m_animations;
    std::unique_ptr<MovementController> m_movement;
    std::unique_ptr<TimerBubble> m_bubble;
    std::unique_ptr<SettingsPanel> m_settingsPanel;
    std::unique_ptr<SystemTrayManager> m_tray;
    std::unique_ptr<CompletionToast> m_completionToast;

    QPushButton* m_gearButton = nullptr;
    QGraphicsOpacityEffect* m_gearOpacity = nullptr;
    QPropertyAnimation* m_gearFade = nullptr;
    QTimer m_gearHideTimer;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_dragOffset;
    bool m_hovering = false;
    bool m_completionHandled = false;
    QTimer m_singleClickTimer;
};
