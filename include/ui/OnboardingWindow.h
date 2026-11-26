#pragma once

#include <QPoint>
#include <QWidget>
#include <array>

class OnboardingPreview;
class PixelIconButton;
class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;
class UISoundManager;

/**
 * First-launch and replayable visual onboarding window for Chiikawa Desktop Study Pet.
 * Displays a compact Welcome screen, 4 animated tutorial steps, and a final screen
 * using the shared warm pastel pixel-art design system.
 */
class OnboardingWindow : public QWidget {
    Q_OBJECT

public:
    enum Page {
        Welcome = 0,
        Interact = 1,
        Drag = 2,
        StudyTimer = 3,
        Alarm = 4,
        Complete = 5,
        PageCount = 6
    };

    static constexpr int kDefaultWidth = 504;
    static constexpr int kStepCount = 4;
    static constexpr int kTransitionMs = 160;

    explicit OnboardingWindow(UISoundManager* uiSound = nullptr, QWidget* parent = nullptr);
    ~OnboardingWindow() override;

    /** Resets to Welcome, centers on the primary monitor, and shows the window. */
    void openOnboarding(bool replay = false);

    [[nodiscard]] int currentPage() const;
    [[nodiscard]] bool isReplay() const { return m_isReplay; }
    [[nodiscard]] QString progressText() const;
    [[nodiscard]] OnboardingPreview* previewForPage(int page) const;

    /** Programmatic page navigation (does not play UI click sounds). */
    void goToPage(int page, bool animate = false);

signals:
    void onboardingCompleted(bool wasReplay);
    void onboardingSkipped(bool wasReplay);
    void onboardingExitRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPrimaryClicked();
    void onBackClicked();
    void onSkipClicked();
    void onCloseClicked();

private:
    void buildUi();
    QWidget* createPageWidget(int pageIndex,
                              const QString& heading,
                              const QString& appTitle,
                              const QString& description,
                              OnboardingPreview* preview);
    void playUiClick();
    void updateNavigationState();
    void updateActivePreview();
    void stopAllPreviews();
    void centerOnPrimaryScreen();
    void finishAndClose(bool completed);

    UISoundManager* m_uiSound = nullptr;
    QWidget* m_headerWidget = nullptr;
    QWidget* m_progressContainer = nullptr;
    std::array<QWidget*, kStepCount> m_progressDots{};
    QLabel* m_progressLabel = nullptr;
    PixelIconButton* m_closeBtn = nullptr;

    QStackedWidget* m_stack = nullptr;
    std::array<OnboardingPreview*, PageCount> m_previews{};
    QGraphicsOpacityEffect* m_stackOpacity = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;

    QPushButton* m_skipBtn = nullptr;
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_primaryBtn = nullptr;
    QLabel* m_footerLabel = nullptr;

    bool m_isReplay = false;
    bool m_outcomeEmitted = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
