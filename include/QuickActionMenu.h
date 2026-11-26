#pragma once

#include <QElapsedTimer>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>
#include <array>

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
 * Floating translucent overlay hosting the sprite-aware three-icon radial
 * quick-action menu (Spotify -> Settings -> Exit) toggled by double-clicking Chiikawa.
 *
 * - Positions icons along an elliptical arc derived from the active sprite's
 *   visible silhouette rather than raw outer window dimensions.
 * - Icons sweep upward along circular arcs from the lower side of Chiikawa while
 *   fading in synchronously, and retrace those exact arcs in reverse on dismissal.
 * - Smoothly interpolates layout changes over 180 ms when Chiikawa switches
 *   animations while the menu is visible or mid-animation.
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

    enum ActionIndex : int {
        SpotifyIndex = 0,
        SettingsIndex = 1,
        ExitIndex = 2,
        ActionCount = 3
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
        ArcPath spotifyArc;  // Top icon, overlay-local coordinates
        ArcPath settingsArc; // Middle icon, overlay-local coordinates
        ArcPath exitArc;     // Bottom icon, overlay-local coordinates
        bool mirroredLeft = false;

        [[nodiscard]] const ArcPath& arcAt(int index) const
        {
            if (index == SpotifyIndex) return spotifyArc;
            if (index == SettingsIndex) return settingsArc;
            return exitArc;
        }
        [[nodiscard]] ArcPath& arcAt(int index)
        {
            if (index == SpotifyIndex) return spotifyArc;
            if (index == SettingsIndex) return settingsArc;
            return exitArc;
        }
    };

    static constexpr int kRevealBaseMs = 440;
    static constexpr int kStaggerStepMs = 40;
    static constexpr int kDismissMs = 340;
    static constexpr int kRetargetMs = 180;
    static constexpr qreal kRetargetThresholdPx = 3.0;
    static constexpr int kDefaultAutoHideMs = 5000;
    static constexpr int kDefaultLeaveHideMs = 2500;

    explicit QuickActionMenu(QWidget* parent = nullptr);
    ~QuickActionMenu() override;

    void setAnchorWidget(QWidget* petWidget);

    void showMenu(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect = {});
    void toggleMenu(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect = {});
    void hideMenu();
    void hideImmediately();
    void updateAnchor(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect = {});
    void updateSpriteBounds(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect);
    void resetAutoHideTimer();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isAnyButtonHovered() const;
    [[nodiscard]] AnimatedQuickActionButton* spotifyButton() const { return m_buttons[SpotifyIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* settingsButton() const { return m_buttons[SettingsIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* exitButton() const { return m_buttons[ExitIndex]; }
    [[nodiscard]] const Layout& currentLayout() const { return m_layout; }
    [[nodiscard]] qreal spotifyProgress() const { return m_progress[SpotifyIndex]; }
    [[nodiscard]] qreal settingsProgress() const { return m_progress[SettingsIndex]; }
    [[nodiscard]] qreal exitProgress() const { return m_progress[ExitIndex]; }
    [[nodiscard]] bool isAutoHideActive() const { return m_autoHideTimer.isActive(); }
    [[nodiscard]] bool isRetargeting() const;
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
                                              const QRect& visibleSpriteLocalRect,
                                              const QRect& screenBounds);

    [[nodiscard]] static Layout computeLayout(const QRect& petGlobalGeometry,
                                              const QRect& screenBounds);

signals:
    void spotifyRequested();
    void settingsRequested();
    void exitRequested();
    void revealed();
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onButtonHoveredChanged(bool hovered);
    void onSpotifyClicked();
    void onSettingsClicked();
    void onExitClicked();
    void checkAnimationCompletion();

private:
    void startRevealForIndex(int index);
    void applyIconProgress(int index, qreal t) const;
    void applyAllIconProgress() const;
    void applyRetargetBlend(qreal alpha);
    void finishDismissal();
    void syncReducedMotion();
    void installOutsideDismissObservers();
    void removeOutsideDismissObservers();
    [[nodiscard]] bool isGlobalPointInsidePetOrButtons(const QPoint& globalPos) const;

    QPointer<QWidget> m_anchorWidget;
    std::array<AnimatedQuickActionButton*, ActionCount> m_buttons{};
    std::array<QVariantAnimation*, ActionCount> m_pathAnims{};
    std::array<QTimer*, ActionCount> m_staggerTimers{};
    std::array<qreal, ActionCount> m_progress{0.0, 0.0, 0.0};

    QVariantAnimation* m_retargetAnim = nullptr;
    QTimer m_autoHideTimer;
    QElapsedTimer m_openedClock;

    QRect m_lastPetRect;
    QRect m_lastSpriteLocalRect;
    Layout m_layout;
    Layout m_retargetFrom;
    Layout m_retargetTo;
    State m_state = State::Closed;
    int m_autoHideMs = kDefaultAutoHideMs;
    int m_leaveHideMs = kDefaultLeaveHideMs;
    int m_reducedMotionOverride = -1;
    bool m_nativeOutsideObserverEnabled = true;
    bool m_appFilterInstalled = false;
    void* m_nativeObserverToken = nullptr;
};
