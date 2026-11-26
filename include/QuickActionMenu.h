#pragma once

#include <QPointF>
#include <QRect>
#include <QTimer>
#include <QWidget>

class AnimatedQuickActionButton;
class QLabel;
class QPropertyAnimation;
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
 * Floating translucent overlay hosting the curved, animated quick-action menu
 * (Settings + Spotify) revealed when double-clicking Chiikawa.
 *
 * - Icons emerge along cubic Bézier arcs from behind the pet while fading in.
 * - Hover scale/rotation animations live on AnimatedQuickActionButton and run
 *   independently from the menu's position and opacity animations.
 * - Adapts/mirrors cleanly near screen edges and follows pet drags smoothly.
 */
class QuickActionMenu : public QWidget {
    Q_OBJECT

public:
    enum class State {
        Hidden,
        Revealing,
        Visible,
        Dismissing
    };

    struct BezierCurve {
        QPointF p0;
        QPointF p1;
        QPointF p2;
        QPointF p3;
    };

    struct Layout {
        QRect overlayRect;
        BezierCurve settingsCurve; // In overlay-local coordinates (button centers)
        BezierCurve spotifyCurve;  // In overlay-local coordinates (button centers)
        bool mirroredLeft = false;
        bool sideAdapted = false;
    };

    static constexpr int kSettingsRevealMs = 340;
    static constexpr int kSpotifyRevealMs = 350;
    static constexpr int kSpotifyStaggerMs = 60;
    static constexpr int kOpacityRevealMs = 250;
    static constexpr int kDismissMs = 210;
    static constexpr int kOpacityDismissMs = 190;
    static constexpr int kDefaultAutoHideMs = 5000;
    static constexpr int kDefaultLeaveHideMs = 2500;

    explicit QuickActionMenu(QWidget* parent = nullptr);
    ~QuickActionMenu() override;

    void showMenu(const QRect& petGlobalGeometry);
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

    void setAutoHideDurationMs(int ms) { m_autoHideMs = ms; }
    void setLeaveHideDelayMs(int ms) { m_leaveHideMs = ms; }
    void setReducedMotionOverride(int overrideMode); // -1 = system, 0 = off, 1 = on
    [[nodiscard]] bool isReducedMotionActive() const;

    [[nodiscard]] static QPointF cubicBezier(const QPointF& p0,
                                             const QPointF& p1,
                                             const QPointF& p2,
                                             const QPointF& p3,
                                             qreal t);

    [[nodiscard]] static Layout computeLayout(const QRect& petGlobalGeometry,
                                              const QRect& screenBounds);

signals:
    void settingsRequested();
    void spotifyRequested();
    void revealed();
    void dismissed();

private slots:
    void onButtonHoveredChanged(bool hovered);
    void onSettingsClicked();
    void onSpotifyClicked();
    void startSpotifyReveal();
    void checkAnimationCompletion();

private:
    void positionButtonAt(AnimatedQuickActionButton* button,
                          const BezierCurve& curve,
                          qreal t) const;
    void finishDismissal();
    void syncReducedMotion();

    AnimatedQuickActionButton* m_settingsBtn = nullptr;
    AnimatedQuickActionButton* m_spotifyBtn = nullptr;

    QVariantAnimation* m_settingsPathAnim = nullptr;
    QVariantAnimation* m_spotifyPathAnim = nullptr;
    QPropertyAnimation* m_settingsOpacityAnim = nullptr;
    QPropertyAnimation* m_spotifyOpacityAnim = nullptr;

    QTimer m_spotifyStaggerTimer;
    QTimer m_autoHideTimer;

    QRect m_lastPetRect;
    Layout m_layout;
    State m_state = State::Hidden;
    qreal m_settingsProgress = 0.0;
    qreal m_spotifyProgress = 0.0;
    int m_autoHideMs = kDefaultAutoHideMs;
    int m_leaveHideMs = kDefaultLeaveHideMs;
    int m_reducedMotionOverride = -1;
};
