#pragma once

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <chrono>
#include <memory>

class QImage;
class QLabel;
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
class QuickActionMenu;
class AppLaunchNoticePopup;
class QuestionScheduler;
class QuizController;
class QuizPopup;
class StudyLibrary;
class OllamaClient;
class QuestionGenerator;
class ProgressTracker;
class WrittenAnswerEvaluator;
class StudyQuestionProvider;
class StudyLibraryWindow;

#include "pet/AnimationManager.h"
#include "platform/DesktopLayerHelper.h"
#include "quickactions/ExternalAppLauncher.h"

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
    [[nodiscard]] AnimationManager* animationManager() const { return m_animations.get(); }
    [[nodiscard]] OnboardingWindow* onboardingWindow() const { return m_onboardingWindow.get(); }
    [[nodiscard]] QuickActionMenu* quickActionMenu() const { return m_quickActionMenu.get(); }
    [[nodiscard]] AppLaunchNoticePopup* appNotice() const { return m_appNotice.get(); }
    [[nodiscard]] QuizController* quizController() const { return m_quizController.get(); }
    [[nodiscard]] QuizPopup* quizPopup() const { return m_quizPopup.get(); }
    [[nodiscard]] QuestionScheduler* questionScheduler() const { return m_questionScheduler.get(); }

    /** True when a scheduled trivia check-in may appear without interrupting anything. */
    [[nodiscard]] bool canShowTriviaCheckIn() const;

    /** Shows a question now (Settings "Ask Me a Question"); ignored if one is already open. */
    void askTriviaNow();

    /** Opens the Study Library window beside the pet. */
    void openStudyLibrary();

    // Lock-In Trivia study stack, created on first use (nullptr before that).
    [[nodiscard]] StudyLibrary* studyLibrary() const { return m_studyLibrary.get(); }
    [[nodiscard]] StudyLibraryWindow* studyLibraryWindow() const { return m_libraryWindow.get(); }
    [[nodiscard]] OllamaClient* ollamaClient() const { return m_ollama.get(); }
    [[nodiscard]] QuestionGenerator* questionGenerator() const { return m_generator.get(); }
    [[nodiscard]] ProgressTracker* progressTracker() const { return m_progressTracker.get(); }
    [[nodiscard]] WrittenAnswerEvaluator* writtenEvaluator() const { return m_writtenEvaluator.get(); }

    void setExternalAppLauncher(std::unique_ptr<ExternalAppLauncher> launcher);

    /** Entry point after construction: shows first-launch onboarding or reveals the pet directly. */
    void startApplication();

    /**
     * Computes the invisible alpha=1 hit-test rect for animations that need extra
     * clickable/hoverable area (Fire surrounding halo, Skip interior gap bridge).
     */
    [[nodiscard]] static QRect computeInteractiveHitRect(const QImage& frame,
                                                         AnimationId animId,
                                                         const QSize& windowSize);

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
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
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
    void hideQuickActions();
    void onSettingsQuickAction();
    void onExternalAppQuickAction(ExternalAppLauncher::AppId app);
    void onExitQuickAction();
    void updateHoverBubble();
    void onOnboardingFinished(bool wasReplay);

private:
    void setupWindowFlags();
    void setupUi();
    void placeInitialPosition();
    [[nodiscard]] QPoint defaultBottomRight() const;
    [[nodiscard]] bool isSavedPositionSafe(const QPoint& pos) const;
    void updateHitMask();
    void showQuickActions();
    [[nodiscard]] QRect currentVisibleSpriteBounds() const;
    void showCompletionToast();
    void playCompletionFeedback();
    void setupTrivia();
    void updateTriviaScheduling();
    /** Creates the study stack (database, ingestion thread, local AI client) if not created yet. */
    void ensureStudy();
    /** Points the controller at study questions or general trivia, per the current settings. */
    void applyStudyPreferences();
    void refreshStudyProgress();
    void refreshStudyCourses();
    void showNoStudyQuestionsNotice();

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
    std::unique_ptr<QuickActionMenu> m_quickActionMenu;
    std::unique_ptr<AppLaunchNoticePopup> m_appNotice;
    std::unique_ptr<ExternalAppLauncher> m_appLauncher;
    // Destroyed in reverse order: library window, popup, scheduler, controller, then the study stack
    // the controller and windows point into.
    std::unique_ptr<StudyLibrary> m_studyLibrary;
    std::unique_ptr<OllamaClient> m_ollama;
    std::unique_ptr<ProgressTracker> m_progressTracker;
    std::unique_ptr<QuestionGenerator> m_generator;
    std::unique_ptr<WrittenAnswerEvaluator> m_writtenEvaluator;
    StudyQuestionProvider* m_studyProvider = nullptr;   // owned by m_quizController
    std::unique_ptr<QuizController> m_quizController;
    std::unique_ptr<QuestionScheduler> m_questionScheduler;
    std::unique_ptr<QuizPopup> m_quizPopup;
    std::unique_ptr<StudyLibraryWindow> m_libraryWindow;
    DesktopLayerHelper m_desktopLayer;

    QTimer m_snoozeBubbleTimer;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_dragOffset;
    bool m_hovering = false;
    bool m_completionHandled = false;
    bool m_desktopLayerInitialized = false;
    bool m_appliedDesktopOnlyMode = false;
    bool m_exitPending = false;
    QRect m_interactiveHitRect;
    QTimer m_singleClickTimer;
};
