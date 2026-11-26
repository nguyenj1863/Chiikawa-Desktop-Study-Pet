#pragma once

#include <QElapsedTimer>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

class AnimatedQuickActionButton;
class QEvent;
class QLabel;
class QPushButton;
class QVariantAnimation;

/**
 * Compact smoked-black glass notice shown near Chiikawa when Spotify isn't installed.
 * Offers an optional "Open Web Player" action without automatically redirecting.
 */
class SpotifyNoticePopup : public QWidget {
    Q_OBJECT

public:
    explicit SpotifyNoticePopup(QWidget* parent = nullptr);

    void showNear(const QRect& petGlobalGeometry,
                  const QString& title,
                  const QString& message,
                  bool showWebPlayerAction = true);
    void dismiss();

signals:
    void webPlayerRequested();
    void buttonActivated();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_title = nullptr;
    QLabel* m_body = nullptr;
    QPushButton* m_webBtn = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QTimer m_autoHide;
    bool m_hovered = false;
};

/**
 * Floating translucent overlay hosting the circular-arc animated quick-action menu
 * (Settings + Spotify) toggled by double-clicking Chiikawa.
 *
 * - Icons emerge from the lower sides of Chiikawa and sweep upward along
 *   complementary circular arcs while fading in synchronously.
 * - Closing retraces the exact same circular arcs in reverse (t: 1 -> 0).
 * - Double-clicking toggles Closed <-> Open and reverses cleanly mid-animation.
 * - Outside clicks (in-app and supported OS focus/click events) dismiss smoothly
 *   without consuming or blocking the user's click.
 * - Hover scale/rotation animations live on AnimatedQuickActionButton and run
 *   independently from the menu's arc position and opacity timeline.
 */
class QuickActionMenu : public QWidget {
    Q_OBJECT

public:
    enum class State {
        Closed,
        Opening,
        Open,
        Closing
    };

    struct ArcPath {
        QPointF center;
        qreal radiusX = 0.0;
        qreal radiusY = 0.0;
        qreal startAngle = 0.0; // radians (0 = right, +pi/2 = down, pi = left, -pi/2 = up)
        qreal endAngle = 0.0;   // radians
    };

    struct Layout {
        QRect overlayRect;
        ArcPath settingsArc; // In overlay-local coordinates (button centers)
        ArcPath spotifyArc;  // In overlay-local coordinates (button centers)
        bool mirroredLeft = false;
        bool sideAdapted = false;
    };

    static constexpr int kSettingsRevealMs = 440;
    static constexpr int kSpotifyRevealMs = 450;
    static constexpr int kSpotifyStaggerMs = 40;
    static constexpr int kDismissMs = 340;
    static constexpr int kDefaultAutoHideMs = 5000;
    static constexpr int kDefaultLeaveHideMs = 2500;

    explicit QuickActionMenu(QWidget* parent = nullptr);
    ~QuickActionMenu() override;

    void setAnchorWidget(QWidget* petWidget);

    void showMenu(const QRect& petGlobalGeometry);
    void toggleMenu(const QRect& petGlobalGeometry);
    void hideMenu();
    void hideImmediately();
    void updateAnchor(const QRect& petGlobalGeometry);
    void resetAutoHideTimer();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isAnyButtonHovered() const;
    [[nodiscard]] AnimatedQuickActionButton* settingsButton() const { return m_settingsBtn; }
    [[nodiscard]] AnimatedQuickActionButton* spotifyButton() const { return m_spotifyBtn; }
    [[nodiscard]] const Layout& currentLayout() const { return m_layout; }
    [[nodiscard]] qreal settingsProgress() const { return m_settingsProgress; }
    [[nodiscard]] qreal spotifyProgress() const { return m_spotifyProgress; }
    [[nodiscard]] bool isAutoHideActive() const { return m_autoHideTimer.isActive(); }
    [[nodiscard]] qint64 elapsedSinceOpenedMs() const { return m_openedClock.isValid() ? m_openedClock.elapsed() : 1000; }

    void setAutoHideDurationMs(int ms) { m_autoHideMs = ms; }
    void setLeaveHideDelayMs(int ms) { m_leaveHideMs = ms; }
    void setNativeOutsideObserverEnabled(bool enabled) { m_nativeOutsideObserverEnabled = enabled; }
    void setReducedMotionOverride(int overrideMode); // -1 = system, 0 = off, 1 = on
    [[nodiscard]] bool isReducedMotionActive() const;

    [[nodiscard]] static QPointF pointOnArc(const QPointF& center,
                                            qreal radiusX,
                                            qreal radiusY,
                                            qreal angle);

    [[nodiscard]] static QPointF evaluateArc(const ArcPath& arc, qreal t);

    [[nodiscard]] static qreal opacityForProgress(qreal t);

    [[nodiscard]] static Layout computeLayout(const QRect& petGlobalGeometry,
                                              const QRect& screenBounds);

signals:
    void settingsRequested();
    void spotifyRequested();
    void revealed();
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onButtonHoveredChanged(bool hovered);
    void onSettingsClicked();
    void onSpotifyClicked();
    void startSpotifyReveal();
    void checkAnimationCompletion();

private:
    void applyIconProgress(AnimatedQuickActionButton* button,
                           const ArcPath& arc,
                           qreal t) const;
    void finishDismissal();
    void syncReducedMotion();
    void installOutsideDismissObservers();
    void removeOutsideDismissObservers();
    [[nodiscard]] bool isGlobalPointInsidePetOrButtons(const QPoint& globalPos) const;

    QPointer<QWidget> m_anchorWidget;
    AnimatedQuickActionButton* m_settingsBtn = nullptr;
    AnimatedQuickActionButton* m_spotifyBtn = nullptr;

    QVariantAnimation* m_settingsPathAnim = nullptr;
    QVariantAnimation* m_spotifyPathAnim = nullptr;

    QTimer m_spotifyStaggerTimer;
    QTimer m_autoHideTimer;
    QElapsedTimer m_openedClock;

    QRect m_lastPetRect;
    Layout m_layout;
    State m_state = State::Closed;
    qreal m_settingsProgress = 0.0;
    qreal m_spotifyProgress = 0.0;
    int m_autoHideMs = kDefaultAutoHideMs;
    int m_leaveHideMs = kDefaultLeaveHideMs;
    int m_reducedMotionOverride = -1;
    bool m_nativeOutsideObserverEnabled = true;
    bool m_appFilterInstalled = false;
    void* m_nativeObserverToken = nullptr;
};
