#pragma once

#include "ExternalAppLauncher.h"
#include "SettingsDefaults.h"

#include <QElapsedTimer>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <array>

class AnimatedQuickActionButton;
class QEvent;
class QLabel;
class QMouseEvent;
class QPushButton;
class QVariantAnimation;

/**
 * Compact smoked-black glass notice shown near Chiikawa when an external
 * desktop application isn't installed or couldn't be started.
 * Offers an optional explicit website button without automatically opening a browser.
 */
class AppLaunchNoticePopup : public QWidget {
    Q_OBJECT

public:
    explicit AppLaunchNoticePopup(QWidget* parent = nullptr);

    void showNear(const QRect& petGlobalGeometry,
                  const QString& title,
                  const QString& message,
                  const QString& fallbackActionLabel = {},
                  const QUrl& fallbackUrl = {});
    void dismiss();

signals:
    void fallbackUrlRequested(const QUrl& url);
    void buttonActivated();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_title = nullptr;
    QLabel* m_body = nullptr;
    QPushButton* m_actionBtn = nullptr;
    QPushButton* m_dismissBtn = nullptr;
    QUrl m_fallbackUrl;
    QTimer m_autoHide;
    bool m_hovered = false;
};

/**
 * Floating translucent overlay hosting Chiikawa's sprite-aware six-icon
 * overlapping animated app stack:
 *   Spotify -> Discord -> League of Legends -> Cursor -> Settings -> Exit
 *
 * - Positioned farther right of Chiikawa's visible sprite silhouette (mirrored
 *   left near the right screen edge) with ~40–45% vertical overlap along a
 *   gentle curve.
 * - Icons emerge along circular arcs from the lower side of Chiikawa while
 *   fading in synchronously, and retrace those exact arcs in reverse on dismissal.
 * - Centralized hit-testing drives the two-phase pull-out hover animation and
 *   dynamic Z-order elevation without overlap-induced enter/leave flicker.
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
        DiscordIndex = 1,
        LeagueIndex = 2,
        CursorIndex = 3,
        SettingsIndex = 4,
        ExitIndex = 5,
        ActionCount = 6
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
        std::array<ArcPath, ActionCount> arcs{};
        bool mirroredLeft = false;

        [[nodiscard]] const ArcPath& arcAt(int index) const
        {
            return arcs[static_cast<std::size_t>(index)];
        }
        [[nodiscard]] ArcPath& arcAt(int index)
        {
            return arcs[static_cast<std::size_t>(index)];
        }
    };

    static constexpr int kRevealBaseMs = 380;
    static constexpr int kStaggerStepMs = 26;
    static constexpr int kDismissMs = 340;
    static constexpr int kRetargetMs = 180;
    static constexpr qreal kRetargetThresholdPx = 3.0;
    static constexpr int kDefaultAutoHideMs = 5000;
    static constexpr int kDefaultLeaveHideMs = 2500;
    static constexpr qreal kTargetOverlapRatio = 0.15; // ~15% layered-card overlap
    static constexpr qreal kArcHalfSpanDeg = 60.0;     // 120° C-shaped circular arc
    static constexpr qreal kSpriteEdgeGapPx = 18.0;    // minimum clear gap from sprite silhouette

    explicit QuickActionMenu(QWidget* parent = nullptr);
    ~QuickActionMenu() override;

    void setAnchorWidget(QWidget* petWidget);

    void setIconScalePercent(int percent);
    [[nodiscard]] int iconScalePercent() const { return m_iconScalePercent; }

    void showMenu(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect = {});
    void toggleMenu(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect = {});
    void hideMenu();
    void hideImmediately();
    void updateAnchor(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect = {});
    void updateSpriteBounds(const QRect& petGlobalGeometry, const QRect& visibleSpriteLocalRect);
    void resetAutoHideTimer();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isAnyButtonHovered() const;
    [[nodiscard]] int hoveredIndex() const { return m_hoveredIndex; }
    [[nodiscard]] AnimatedQuickActionButton* buttonAt(int index) const;
    [[nodiscard]] AnimatedQuickActionButton* spotifyButton() const { return m_buttons[SpotifyIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* discordButton() const { return m_buttons[DiscordIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* leagueButton() const { return m_buttons[LeagueIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* cursorButton() const { return m_buttons[CursorIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* settingsButton() const { return m_buttons[SettingsIndex]; }
    [[nodiscard]] AnimatedQuickActionButton* exitButton() const { return m_buttons[ExitIndex]; }
    [[nodiscard]] const Layout& currentLayout() const { return m_layout; }
    [[nodiscard]] qreal progressAt(int index) const { return m_progress[static_cast<std::size_t>(index)]; }
    [[nodiscard]] bool isAutoHideActive() const { return m_autoHideTimer.isActive(); }
    [[nodiscard]] bool isRetargeting() const;
    [[nodiscard]] qint64 elapsedSinceOpenedMs() const { return m_openedClock.isValid() ? m_openedClock.elapsed() : 1000; }

    /** Resting 32×32 visual rect of icon `index` in overlay-local coordinates. */
    [[nodiscard]] QRectF restingVisualRect(int index) const;

    /** Hit-tests an overlay-local point against the overlapping stack (favors active pulled-out card, then front-to-back). */
    [[nodiscard]] int hitTestButton(const QPoint& localPos) const;

    /** Programmatically updates hover selection (also called from mouseMoveEvent). */
    void setHoveredIndex(int index);

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
                                              const QRect& screenBounds,
                                              int iconScalePercent = SettingsDefaults::kQuickActionIconScalePercent);

    [[nodiscard]] static Layout computeLayout(const QRect& petGlobalGeometry,
                                              const QRect& screenBounds,
                                              int iconScalePercent = SettingsDefaults::kQuickActionIconScalePercent);

signals:
    void activeStateChanged(bool active);
    void externalAppRequested(ExternalAppLauncher::AppId app);
    void spotifyRequested();
    void settingsRequested();
    void exitRequested();
    void revealed();
    void dismissed();

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onButtonHoveredChanged(bool hovered);
    void checkAnimationCompletion();

private:
    void triggerAction(int index);
    void setActiveFlag(bool active);
    void applyDefaultStackOrder();
    void syncOutwardDirection();
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
    std::array<qreal, ActionCount> m_progress{};

    QVariantAnimation* m_retargetAnim = nullptr;
    QTimer m_autoHideTimer;
    QElapsedTimer m_openedClock;

    QRect m_lastPetRect;
    QRect m_lastSpriteLocalRect;
    Layout m_layout;
    Layout m_retargetFrom;
    Layout m_retargetTo;
    State m_state = State::Closed;
    bool m_active = false;
    int m_iconScalePercent = SettingsDefaults::kQuickActionIconScalePercent;
    int m_hoveredIndex = -1;
    int m_pressedIndex = -1;
    int m_autoHideMs = kDefaultAutoHideMs;
    int m_leaveHideMs = kDefaultLeaveHideMs;
    int m_reducedMotionOverride = -1;
    bool m_nativeOutsideObserverEnabled = true;
    bool m_appFilterInstalled = false;
    void* m_nativeObserverToken = nullptr;
};
