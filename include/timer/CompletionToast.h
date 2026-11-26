#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QMovie;
class QPropertyAnimation;
class QPushButton;
class QScreen;

/**
 * Custom study-completion notification anchored to the bottom-right work area
 * of the primary display (above the taskbar). Stays visible for 12 seconds with
 * hover-aware pause/resume and never steals keyboard focus.
 */
class CompletionToast : public QWidget {
    Q_OBJECT

public:
    static constexpr int kDefaultDisplayDurationMs = 12000;
    static constexpr int kScreenEdgeMarginPx = 20;
    static constexpr int kFadeInMs = 180;
    static constexpr int kFadeOutMs = 200;
    static constexpr int kEntranceSlidePx = 6;

    explicit CompletionToast(QWidget* parent = nullptr);
    ~CompletionToast() override;

    /** Shows or refreshes the single completion toast anchored to the primary monitor bottom-right. */
    void showNotification(int durationMs = kDefaultDisplayDurationMs);

    /** Compatibility wrapper that delegates to showNotification() (does not follow the pet). */
    void showNear(const QRect& petGlobalGeometry);

    void dismiss();

    /** Pure geometry helper for bottom-right work-area anchoring (exposed for deterministic tests). */
    [[nodiscard]] static QPoint calculateBottomRightPosition(const QRect& availableGeometry,
                                                             const QSize& toastSize,
                                                             int margin = kScreenEdgeMarginPx);

    [[nodiscard]] int remainingDismissMs() const;
    [[nodiscard]] bool isAutoHidePaused() const { return m_hovered && !m_fadingOut; }

signals:
    void dismissed();
    void startAgainRequested();
    void buttonActivated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onDismissClicked();
    void onStartAgainClicked();
    void finishFadeOut();
    void repositionToPrimaryScreen();

private:
    void bindScreenSignals(QScreen* screen);
    [[nodiscard]] QScreen* targetScreen() const;
    void startFadeIn(const QPoint& finalPos);
    void startFadeOut();

    QLabel* m_gifLabel = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QPushButton* m_againBtn = nullptr;
    QMovie* m_movie = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    QPropertyAnimation* m_slide = nullptr;
    QPointer<QScreen> m_trackedScreen;

    QTimer m_autoHide;
    QElapsedTimer m_remainingClock;
    int m_remainingMs = kDefaultDisplayDurationMs;
    bool m_hovered = false;
    bool m_fadingOut = false;
};
