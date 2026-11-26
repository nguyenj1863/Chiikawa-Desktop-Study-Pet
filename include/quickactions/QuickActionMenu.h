#pragma once

#include "quickactions/ExternalAppLauncher.h"
#include "quickactions/QuickActionItem.h"
#include "app/SettingsDefaults.h"

#include <QElapsedTimer>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QWidget>

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
 * Floating translucent overlay hosting Chiikawa's data-driven, sprite-aware
 * circular quick-action menu (1..10 enabled shortcuts).
 *
 * - Dynamically builds buttons and circular arc paths from QuickActionItem
 *   configuration (built-in apps, user-added desktop apps, Settings, Exit).
 * - Adapts arc curvature, radii, and spacing cleanly to any shortcut count
 *   (up to kMaxVisibleRadialShortcuts = 10) and any icon scale (80%–150%).
 * - Centralized hit-testing drives the two-phase pull-out hover animation and
 *   dynamic Z-order elevation without overlap-induced enter/leave flicker.
 * - Emits activeStateChanged(bool) so PetWindow suspends autonomous movement
 *   and automatic sprite switching for the full open/closing lifecycle.
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
        QVector<ArcPath> arcs;
        bool mirroredLeft = false;

        [[nodiscard]] int count() const { return static_cast<int>(arcs.size()); }
        [[nodiscard]] const ArcPath& arcAt(int index) const { return arcs[index]; }
        [[nodiscard]] ArcPath& arcAt(int index) { return arcs[index]; }
    };

    static constexpr int kRevealBaseMs = 380;
    static constexpr int kMaxTotalStaggerMs = 150;
    static constexpr int kDismissMs = 340;
    static constexpr int kRetargetMs = 180;
    static constexpr qreal kRetargetThresholdPx = 3.0;
    static constexpr int kDefaultAutoHideMs = 5000;
    static constexpr int kDefaultLeaveHideMs = 2500;
    static constexpr qreal kTargetOverlapRatio = 0.15; // ~15% layered-card overlap
    static constexpr qreal kBaseStepDeg = 24.0;        // 120° C-shaped arc at 6 icons
    static constexpr qreal kMaxArcHalfSpanDeg = 81.0;  // up to 162° arc at 10 icons
    static constexpr qreal kSpriteEdgeGapPx = 18.0;    // minimum clear gap from sprite silhouette

    explicit QuickActionMenu(QWidget* parent = nullptr);
    ~QuickActionMenu() override;

    void setAnchorWidget(QWidget* petWidget);

    void setShortcuts(const QVector<QuickActionItem>& items);
    [[nodiscard]] const QVector<QuickActionItem>& visibleShortcuts() const { return m_shortcuts; }
    [[nodiscard]] int actionCount() const { return static_cast<int>(m_buttons.size()); }

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
    [[nodiscard]] AnimatedQuickActionButton* findButtonById(const QString& id) const;
    [[nodiscard]] int indexOfId(const QString& id) const;
    [[nodiscard]] AnimatedQuickActionButton* spotifyButton() const { return findButtonById(QStringLiteral("builtin:spotify")); }
    [[nodiscard]] AnimatedQuickActionButton* discordButton() const { return findButtonById(QStringLiteral("builtin:discord")); }
    [[nodiscard]] AnimatedQuickActionButton* leagueButton() const { return findButtonById(QStringLiteral("builtin:league")); }
    [[nodiscard]] AnimatedQuickActionButton* cursorButton() const { return findButtonById(QStringLiteral("builtin:cursor")); }
    [[nodiscard]] AnimatedQuickActionButton* settingsButton() const { return findButtonById(QStringLiteral("builtin:settings")); }
    [[nodiscard]] AnimatedQuickActionButton* exitButton() const { return findButtonById(QStringLiteral("builtin:exit")); }
    [[nodiscard]] const Layout& currentLayout() const { return m_layout; }
    [[nodiscard]] qreal progressAt(int index) const;
    [[nodiscard]] bool isAutoHideActive() const { return m_autoHideTimer.isActive(); }
    [[nodiscard]] bool isRetargeting() const;
    [[nodiscard]] qint64 elapsedSinceOpenedMs() const { return m_openedClock.isValid() ? m_openedClock.elapsed() : 1000; }

    [[nodiscard]] QRectF restingVisualRect(int index) const;
    [[nodiscard]] int hitTestButton(const QPoint& localPos) const;
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
                                              int iconScalePercent = SettingsDefaults::kQuickActionIconScalePercent,
                                              int visibleCount = 6);

    [[nodiscard]] static Layout computeLayout(const QRect& petGlobalGeometry,
                                              const QRect& screenBounds,
                                              int iconScalePercent = SettingsDefaults::kQuickActionIconScalePercent,
                                              int visibleCount = 6);

signals:
    void activeStateChanged(bool active);
    void shortcutTriggered(const QuickActionItem& item);
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
    void rebuildActionWidgets();
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
    QVector<QuickActionItem> m_shortcuts;
    QVector<AnimatedQuickActionButton*> m_buttons;
    QVector<QVariantAnimation*> m_pathAnims;
    QVector<QTimer*> m_staggerTimers;
    QVector<qreal> m_progress;

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
