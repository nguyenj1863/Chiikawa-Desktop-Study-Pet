#include "QuickActionMenu.h"

#include "AnimatedQuickActionButton.h"
#include "OnboardingPreview.h"
#include "PixelTheme.h"
#include "WindowCoordinator.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include "MacWindowHelper.h"
#endif

#include <QApplication>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kBtnSize = AnimatedQuickActionButton::kWidgetSize;
constexpr int kBtnHalf = kBtnSize / 2;
constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kTwoPi = 2.0 * kPi;
constexpr qreal kDegToRad = kPi / 180.0;

const QColor kSpotifyGreen{0x1D, 0xB9, 0x54};
const QColor kSpotifyGreenHover{0x24, 0xD8, 0x64};

QRect screenBoundsForPet(const QRect& petRect)
{
    return WindowCoordinator::availableGeometryFor(petRect.center());
}

QuickActionMenu::ArcPath lerpArc(const QuickActionMenu::ArcPath& a,
                                 const QuickActionMenu::ArcPath& b,
                                 qreal alpha)
{
    const qreal t = std::clamp(alpha, 0.0, 1.0);
    return QuickActionMenu::ArcPath{
        a.center + (b.center - a.center) * t,
        a.radiusX + (b.radiusX - a.radiusX) * t,
        a.radiusY + (b.radiusY - a.radiusY) * t,
        a.startAngle + (b.startAngle - a.startAngle) * t,
        a.endAngle + (b.endAngle - a.endAngle) * t
    };
}

#ifdef Q_OS_WIN
QPointer<QuickActionMenu> g_activeWinMenu;

void CALLBACK winForegroundEventProc(HWINEVENTHOOK /*hook*/,
                                     DWORD event,
                                     HWND hwnd,
                                     LONG /*idObject*/,
                                     LONG /*idChild*/,
                                     DWORD /*eventThread*/,
                                     DWORD /*eventTime*/)
{
    if (event != EVENT_SYSTEM_FOREGROUND || !hwnd || !g_activeWinMenu) {
        return;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {
        return;
    }
    QPointer<QuickActionMenu> menu = g_activeWinMenu;
    QMetaObject::invokeMethod(menu, [menu]() {
        if (menu && menu->elapsedSinceOpenedMs() >= 150
            && (menu->state() == QuickActionMenu::State::Open
                || menu->state() == QuickActionMenu::State::Opening)) {
            menu->hideMenu();
        }
    }, Qt::QueuedConnection);
}
#endif

} // namespace

// ============================================================================
// SpotifyNoticePopup
// ============================================================================

SpotifyNoticePopup::SpotifyNoticePopup(QWidget* parent)
#ifdef Q_OS_MACOS
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)
#else
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)
#endif
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    setFixedWidth(258);
    setStyleSheet(PixelTheme::styleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(6);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("toastTitle"));
    m_title->setWordWrap(true);
    root->addWidget(m_title);

    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("muted"));
    m_body->setWordWrap(true);
    root->addWidget(m_body);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 4, 0, 0);
    row->setSpacing(8);
    row->addStretch(1);

    m_dismissBtn = new QPushButton(QStringLiteral("Dismiss"), this);
    m_dismissBtn->setCursor(Qt::PointingHandCursor);
    m_dismissBtn->setFocusPolicy(Qt::NoFocus);

    m_webBtn = new QPushButton(QStringLiteral("Open Web Player"), this);
    m_webBtn->setObjectName(QStringLiteral("primary"));
    m_webBtn->setCursor(Qt::PointingHandCursor);
    m_webBtn->setFocusPolicy(Qt::NoFocus);

    row->addWidget(m_dismissBtn);
    row->addWidget(m_webBtn);
    root->addLayout(row);

    connect(m_dismissBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        dismiss();
    });
    connect(m_webBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit webPlayerRequested();
        dismiss();
    });

    m_autoHide.setSingleShot(true);
    connect(&m_autoHide, &QTimer::timeout, this, &SpotifyNoticePopup::dismiss);

    hide();
}

void SpotifyNoticePopup::showNear(const QRect& petGlobalGeometry,
                                  const QString& title,
                                  const QString& message,
                                  bool showWebPlayerAction)
{
    m_title->setText(title);
    m_body->setText(message);
    m_webBtn->setVisible(showWebPlayerAction);
    adjustSize();

    const QRect bounds = screenBoundsForPet(petGlobalGeometry);
    const QPoint desired(petGlobalGeometry.center().x() - width() / 2,
                         petGlobalGeometry.top() - height() - 10);
    const QPoint placed = WindowCoordinator::clampTopLeft(desired, size(), bounds);
    move(placed);

    m_hovered = false;
    show();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, false);
#endif
    raise();
    m_autoHide.start(6500);
}

void SpotifyNoticePopup::dismiss()
{
    m_autoHide.stop();
    m_hovered = false;
    hide();
}

void SpotifyNoticePopup::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    m_autoHide.stop();
    QWidget::enterEvent(event);
}

void SpotifyNoticePopup::leaveEvent(QEvent* event)
{
    m_hovered = false;
    if (isVisible()) {
        m_autoHide.start(3000);
    }
    QWidget::leaveEvent(event);
}

void SpotifyNoticePopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintGlassSurface(p, rect(), PixelTheme::GlassLevel::Compact, PixelTheme::kRadiusCard, 5);
}

// ============================================================================
// QuickActionMenu
// ============================================================================

QuickActionMenu::QuickActionMenu(QWidget* parent)
#ifdef Q_OS_MACOS
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)
#else
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)
#endif
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);

    // 1. Spotify (top)
    m_buttons[SpotifyIndex] = new AnimatedQuickActionButton(
        PixelIcon::Spotify,
        kSpotifyGreen,
        kSpotifyGreenHover,
        -6.0,
        this);
    m_buttons[SpotifyIndex]->setObjectName(QStringLiteral("quickActionSpotify"));
    m_buttons[SpotifyIndex]->setToolTip(QStringLiteral("Open Spotify"));
    m_buttons[SpotifyIndex]->setAccessibleName(QStringLiteral("Open Spotify"));

    // 2. Settings (middle)
    m_buttons[SettingsIndex] = new AnimatedQuickActionButton(
        PixelIcon::Gear,
        PixelTheme::kPrimaryText,
        QColor(Qt::white),
        6.0,
        this);
    m_buttons[SettingsIndex]->setObjectName(QStringLiteral("quickActionSettings"));
    m_buttons[SettingsIndex]->setToolTip(QStringLiteral("Settings"));
    m_buttons[SettingsIndex]->setAccessibleName(QStringLiteral("Open settings"));

    // 3. Exit (bottom)
    m_buttons[ExitIndex] = new AnimatedQuickActionButton(
        PixelIcon::Exit,
        PixelTheme::kPrimaryText,
        PixelTheme::kDangerText,
        -5.0,
        this);
    m_buttons[ExitIndex]->setObjectName(QStringLiteral("quickActionExit"));
    m_buttons[ExitIndex]->setToolTip(QStringLiteral("Exit Chiikawa"));
    m_buttons[ExitIndex]->setAccessibleName(QStringLiteral("Exit application"));

    connect(m_buttons[SpotifyIndex], &QAbstractButton::clicked,
            this, &QuickActionMenu::onSpotifyClicked);
    connect(m_buttons[SettingsIndex], &QAbstractButton::clicked,
            this, &QuickActionMenu::onSettingsClicked);
    connect(m_buttons[ExitIndex], &QAbstractButton::clicked,
            this, &QuickActionMenu::onExitClicked);

    for (int i = 0; i < ActionCount; ++i) {
        connect(m_buttons[i], &AnimatedQuickActionButton::hoveredChanged,
                this, &QuickActionMenu::onButtonHoveredChanged);

        m_pathAnims[i] = new QVariantAnimation(this);
        connect(m_pathAnims[i], &QVariantAnimation::valueChanged,
                this, [this, i](const QVariant& value) {
                    m_progress[i] = value.toReal();
                    applyIconProgress(i, m_progress[i]);
                });
        connect(m_pathAnims[i], &QVariantAnimation::finished,
                this, &QuickActionMenu::checkAnimationCompletion);

        m_staggerTimers[i] = new QTimer(this);
        m_staggerTimers[i]->setSingleShot(true);
        connect(m_staggerTimers[i], &QTimer::timeout,
                this, [this, i]() { startRevealForIndex(i); });
    }

    m_retargetAnim = new QVariantAnimation(this);
    m_retargetAnim->setDuration(kRetargetMs);
    m_retargetAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_retargetAnim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                applyRetargetBlend(value.toReal());
            });
    connect(m_retargetAnim, &QVariantAnimation::finished,
            this, [this]() {
                if (m_state != State::Closed) {
                    m_layout = m_retargetTo;
                    setGeometry(m_layout.overlayRect);
                    applyAllIconProgress();
                }
            });

    m_autoHideTimer.setSingleShot(true);
    connect(&m_autoHideTimer, &QTimer::timeout,
            this, &QuickActionMenu::hideMenu);

    syncReducedMotion();
    hide();
}

QuickActionMenu::~QuickActionMenu()
{
    m_autoHideTimer.stop();
    for (auto* t : m_staggerTimers) {
        if (t) t->stop();
    }
    if (m_retargetAnim) {
        m_retargetAnim->stop();
    }
    removeOutsideDismissObservers();
}

void QuickActionMenu::setAnchorWidget(QWidget* petWidget)
{
    m_anchorWidget = petWidget;
}

bool QuickActionMenu::isRetargeting() const
{
    return m_retargetAnim && m_retargetAnim->state() == QAbstractAnimation::Running;
}

QPointF QuickActionMenu::pointOnArc(const QPointF& center,
                                    qreal radiusX,
                                    qreal radiusY,
                                    qreal angle)
{
    return QPointF(
        center.x() + radiusX * std::cos(angle),
        center.y() + radiusY * std::sin(angle));
}

QPointF QuickActionMenu::evaluateArc(const ArcPath& arc, qreal t)
{
    const qreal clamped = std::clamp(t, 0.0, 1.0);
    const qreal angle = arc.startAngle + (arc.endAngle - arc.startAngle) * clamped;
    return pointOnArc(arc.center, arc.radiusX, arc.radiusY, angle);
}

qreal QuickActionMenu::opacityForProgress(qreal t)
{
    const qreal x = std::clamp(t, 0.0, 1.0);
    // Synchronized opacity: 0% -> 0%, 25% -> ~28%, 50% -> ~57%, 75% -> ~83%, 100% -> 100%.
    return std::clamp(x * (1.0 + 0.55 * x - 0.55 * x * x), 0.0, 1.0);
}

QuickActionMenu::Layout QuickActionMenu::computeLayout(const QRect& petGlobalGeometry,
                                                       const QRect& screenBounds)
{
    return computeLayout(petGlobalGeometry, QRect{}, screenBounds);
}

QuickActionMenu::Layout QuickActionMenu::computeLayout(const QRect& petGlobalGeometry,
                                                       const QRect& visibleSpriteLocalRect,
                                                       const QRect& screenBounds)
{
    Layout layout;

    const int petW = std::max(40, petGlobalGeometry.width());
    const int petH = std::max(40, petGlobalGeometry.height());

    // Derive visible sprite rect in global coordinates (fallback to inset pet rect if unavailable).
    QRect spriteGlobal;
    if (visibleSpriteLocalRect.isValid() && !visibleSpriteLocalRect.isEmpty()) {
        spriteGlobal = visibleSpriteLocalRect.translated(petGlobalGeometry.topLeft());
    } else {
        const int insetX = std::max(4, petW / 8);
        const int insetY = std::max(4, petH / 8);
        spriteGlobal = petGlobalGeometry.adjusted(insetX, insetY, -insetX, -insetY);
    }

    // Orbit center tracks the actual visible character's center.
    QPointF orbitCenter(spriteGlobal.left() + spriteGlobal.width() * 0.50,
                        spriteGlobal.top() + spriteGlobal.height() * 0.50);

    const qreal spriteHalfW = std::max<qreal>(16.0, spriteGlobal.width() * 0.50);
    const qreal spriteHalfH = std::max<qreal>(16.0, spriteGlobal.height() * 0.50);

    // Compact radial distance hugging the visible silhouette (16px button visual half + ~11px gap).
    constexpr qreal kSilhouetteClearance = 27.0;
    const qreal radiusX = std::max<qreal>(56.0, spriteHalfW + kSilhouetteClearance);
    const qreal radiusY = std::max<qreal>(54.0, spriteHalfH + kSilhouetteClearance - 2.0);

    // Guarantee >= 50.5 px center-to-center chord separation so 48x48 buttons at 1.22x hover never overlap.
    const qreal minRadius = std::min(radiusX, radiusY);
    constexpr qreal kTargetChord = 50.5;
    const qreal sinHalf = std::min<qreal>(0.90, kTargetChord / (2.0 * minRadius));
    const qreal stepAngle = std::clamp(2.0 * std::asin(sinHalf), 28.0 * kDegToRad, 56.0 * kDegToRad);

    // Check if there is enough room on the right side for the radial arc; otherwise mirror left.
    if (screenBounds.isValid()
        && (orbitCenter.x() + radiusX + kBtnHalf + 2.0 > screenBounds.right() + 1)) {
        const qreal spaceRight = (screenBounds.right() + 1) - orbitCenter.x();
        const qreal spaceLeft = orbitCenter.x() - screenBounds.left();
        if (spaceLeft > spaceRight) {
            layout.mirroredLeft = true;
        }
    }

    // Target angles (order is strictly Spotify top -> Settings middle -> Exit bottom).
    // Positioned slightly lower overall so Spotify sits near the former Settings gear spot.
    std::array<qreal, ActionCount> endAngles{};
    std::array<qreal, ActionCount> startAngles{};

    if (!layout.mirroredLeft) {
        constexpr qreal kMidAngle = -4.0 * kDegToRad;
        endAngles[SpotifyIndex] = kMidAngle - stepAngle;  // Top (~-46 deg)
        endAngles[SettingsIndex] = kMidAngle;             // Middle (-4 deg)
        endAngles[ExitIndex] = kMidAngle + stepAngle;     // Bottom (~+38 deg)

        // All three sweep upward counter-clockwise from the lower-right of Chiikawa.
        startAngles[SpotifyIndex] = 72.0 * kDegToRad;
        startAngles[SettingsIndex] = 80.0 * kDegToRad;
        startAngles[ExitIndex] = 88.0 * kDegToRad;
    } else {
        constexpr qreal kMidAngle = (180.0 + 4.0) * kDegToRad;
        endAngles[SpotifyIndex] = kMidAngle + stepAngle;  // Top (~+226 deg / -134 deg)
        endAngles[SettingsIndex] = kMidAngle;             // Middle (+184 deg)
        endAngles[ExitIndex] = kMidAngle - stepAngle;     // Bottom (~+142 deg)

        // All three sweep upward clockwise from the lower-left of Chiikawa.
        startAngles[SpotifyIndex] = 108.0 * kDegToRad;
        startAngles[SettingsIndex] = 100.0 * kDegToRad;
        startAngles[ExitIndex] = 92.0 * kDegToRad;
    }

    // Vertically clamp the orbit center only if needed so all 3 final icons stay inside screenBounds
    // without moving the pet window or altering icon order/spacing.
    if (screenBounds.isValid()) {
        qreal minEndY = orbitCenter.y();
        qreal maxEndY = orbitCenter.y();
        for (int i = 0; i < ActionCount; ++i) {
            const qreal y = pointOnArc(orbitCenter, radiusX, radiusY, endAngles[i]).y();
            minEndY = std::min(minEndY, y);
            maxEndY = std::max(maxEndY, y);
        }
        const qreal allowedTop = screenBounds.top() + kBtnHalf + 2.0;
        const qreal allowedBottom = (screenBounds.bottom() + 1) - kBtnHalf - 2.0;
        if (minEndY < allowedTop) {
            orbitCenter.ry() += (allowedTop - minEndY);
        } else if (maxEndY > allowedBottom) {
            orbitCenter.ry() -= (maxEndY - allowedBottom);
        }
    }

    std::array<ArcPath, ActionCount> globalArcs{};
    for (int i = 0; i < ActionCount; ++i) {
        qreal startA = startAngles[i];
        qreal endA = endAngles[i];
        if (!layout.mirroredLeft) {
            while (endA >= startA) {
                endA -= kTwoPi;
            }
        } else {
            while (endA <= startA) {
                endA += kTwoPi;
            }
        }
        globalArcs[i] = ArcPath{orbitCenter, radiusX, radiusY, startA, endA};
    }

    // Compute a tight bounding box covering the entire sweep of all three circular arcs.
    constexpr int kSamples = 20;
    const QPointF firstPt = evaluateArc(globalArcs[0], 1.0);
    qreal minX = firstPt.x();
    qreal maxX = firstPt.x();
    qreal minY = firstPt.y();
    qreal maxY = firstPt.y();
    auto expand = [&](const QPointF& pt) {
        minX = std::min(minX, pt.x());
        maxX = std::max(maxX, pt.x());
        minY = std::min(minY, pt.y());
        maxY = std::max(maxY, pt.y());
    };
    for (int a = 0; a < ActionCount; ++a) {
        for (int s = 0; s <= kSamples; ++s) {
            expand(evaluateArc(globalArcs[a], static_cast<qreal>(s) / static_cast<qreal>(kSamples)));
        }
    }

    const int pad = kBtnHalf + 4;
    const int left = static_cast<int>(std::floor(minX)) - pad;
    const int top = static_cast<int>(std::floor(minY)) - pad;
    const int right = static_cast<int>(std::ceil(maxX)) + pad;
    const int bottom = static_cast<int>(std::ceil(maxY)) + pad;
    layout.overlayRect = QRect(left, top, std::max(kBtnSize, right - left), std::max(kBtnSize, bottom - top));

    const QPointF offset(layout.overlayRect.topLeft());
    for (int i = 0; i < ActionCount; ++i) {
        ArcPath localArc = globalArcs[i];
        localArc.center -= offset;
        layout.arcAt(i) = localArc;
    }

    return layout;
}

void QuickActionMenu::setReducedMotionOverride(int overrideMode)
{
    m_reducedMotionOverride = overrideMode;
    syncReducedMotion();
}

bool QuickActionMenu::isReducedMotionActive() const
{
    if (m_reducedMotionOverride == 1) {
        return true;
    }
    if (m_reducedMotionOverride == 0) {
        return false;
    }
#ifdef Q_OS_MACOS
    if (MacWindowHelper::prefersReducedMotion()) {
        return true;
    }
#endif
    return OnboardingPreview::systemPrefersReducedMotion();
}

void QuickActionMenu::syncReducedMotion()
{
    const bool reduced = isReducedMotionActive();
    for (auto* btn : m_buttons) {
        if (btn) {
            btn->setReducedMotion(reduced);
        }
    }
}

bool QuickActionMenu::isAnyButtonHovered() const
{
    for (const auto* btn : m_buttons) {
        if (btn && btn->isHovered()) {
            return true;
        }
    }
    return false;
}

void QuickActionMenu::applyIconProgress(int index, qreal t) const
{
    if (index < 0 || index >= ActionCount || !m_buttons[index]) {
        return;
    }
    const QPointF center = evaluateArc(m_layout.arcAt(index), t);
    m_buttons[index]->move(qRound(center.x()) - kBtnHalf, qRound(center.y()) - kBtnHalf);
    m_buttons[index]->setMenuOpacity(opacityForProgress(t));
}

void QuickActionMenu::applyAllIconProgress() const
{
    for (int i = 0; i < ActionCount; ++i) {
        applyIconProgress(i, m_progress[i]);
    }
}

void QuickActionMenu::applyRetargetBlend(qreal alpha)
{
    const QRect unionRect = m_retargetFrom.overlayRect.united(m_retargetTo.overlayRect);
    if (geometry() != unionRect) {
        setGeometry(unionRect);
    }

    Layout blended;
    blended.overlayRect = unionRect;
    blended.mirroredLeft = m_retargetTo.mirroredLeft;

    const QPointF fromOffset(m_retargetFrom.overlayRect.topLeft());
    const QPointF toOffset(m_retargetTo.overlayRect.topLeft());
    const QPointF unionOffset(unionRect.topLeft());

    for (int i = 0; i < ActionCount; ++i) {
        ArcPath gFrom = m_retargetFrom.arcAt(i);
        gFrom.center += fromOffset;
        ArcPath gTo = m_retargetTo.arcAt(i);
        gTo.center += toOffset;

        ArcPath gMid = lerpArc(gFrom, gTo, alpha);
        gMid.center -= unionOffset;
        blended.arcAt(i) = gMid;
    }

    m_layout = blended;
    applyAllIconProgress();
}

void QuickActionMenu::updateAnchor(const QRect& petGlobalGeometry,
                                   const QRect& visibleSpriteLocalRect)
{
    m_lastPetRect = petGlobalGeometry;
    if (visibleSpriteLocalRect.isValid() && !visibleSpriteLocalRect.isEmpty()) {
        m_lastSpriteLocalRect = visibleSpriteLocalRect;
    }
    if (m_state == State::Closed) {
        return;
    }

    const Layout next = computeLayout(petGlobalGeometry, m_lastSpriteLocalRect, screenBoundsForPet(petGlobalGeometry));
    if (isRetargeting()) {
        // Keep the retarget animation moving smoothly with the dragged pet.
        const QPoint delta = next.overlayRect.topLeft() - m_retargetTo.overlayRect.topLeft();
        m_retargetFrom.overlayRect.translate(delta);
        m_retargetTo = next;
        applyRetargetBlend(m_retargetAnim->currentValue().toReal());
        return;
    }

    const bool geometryChanged = (next.overlayRect.size() != size())
                              || (next.mirroredLeft != m_layout.mirroredLeft);
    m_layout = next;
    if (geometryChanged) {
        setGeometry(m_layout.overlayRect);
        applyAllIconProgress();
    } else if (pos() != m_layout.overlayRect.topLeft()) {
        // Pure translation: moving the overlay shifts all three icons smoothly with Chiikawa
        // without interrupting or overwriting any running circular arc animation.
        move(m_layout.overlayRect.topLeft());
    }
}

void QuickActionMenu::updateSpriteBounds(const QRect& petGlobalGeometry,
                                         const QRect& visibleSpriteLocalRect)
{
    m_lastPetRect = petGlobalGeometry;
    m_lastSpriteLocalRect = visibleSpriteLocalRect;
    if (m_state == State::Closed) {
        return;
    }

    const Layout next = computeLayout(petGlobalGeometry, visibleSpriteLocalRect, screenBoundsForPet(petGlobalGeometry));

    // Compare new global target centers against current target centers to prevent jitter.
    const Layout& refLayout = isRetargeting() ? m_retargetTo : m_layout;
    const QPointF refOffset(refLayout.overlayRect.topLeft());
    const QPointF nextOffset(next.overlayRect.topLeft());
    qreal maxDelta = 0.0;
    for (int i = 0; i < ActionCount; ++i) {
        const QPointF a = refOffset + evaluateArc(refLayout.arcAt(i), 1.0);
        const QPointF b = nextOffset + evaluateArc(next.arcAt(i), 1.0);
        maxDelta = std::max(maxDelta, std::hypot(a.x() - b.x(), a.y() - b.y()));
    }
    if (maxDelta < kRetargetThresholdPx && next.mirroredLeft == refLayout.mirroredLeft) {
        return;
    }

    if (isReducedMotionActive()) {
        m_retargetAnim->stop();
        m_layout = next;
        setGeometry(m_layout.overlayRect);
        applyAllIconProgress();
        return;
    }

    // Smoothly interpolate from current displayed layout to target layout without
    // restarting or resetting ongoing opening/closing progress t.
    m_retargetFrom = m_layout;
    m_retargetTo = next;
    {
        QSignalBlocker blocker(m_retargetAnim);
        m_retargetAnim->stop();
        m_retargetAnim->setDuration(kRetargetMs);
        m_retargetAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_retargetAnim->setStartValue(0.0);
        m_retargetAnim->setEndValue(1.0);
        m_retargetAnim->setCurrentTime(0);
    }
    m_retargetAnim->start();
}

void QuickActionMenu::toggleMenu(const QRect& petGlobalGeometry,
                                 const QRect& visibleSpriteLocalRect)
{
    switch (m_state) {
    case State::Closed:
    case State::Closing:
        showMenu(petGlobalGeometry, visibleSpriteLocalRect);
        break;
    case State::Opening:
    case State::Open:
        hideMenu();
        break;
    }
}

void QuickActionMenu::showMenu(const QRect& petGlobalGeometry,
                               const QRect& visibleSpriteLocalRect)
{
    m_lastPetRect = petGlobalGeometry;
    if (visibleSpriteLocalRect.isValid() && !visibleSpriteLocalRect.isEmpty()) {
        m_lastSpriteLocalRect = visibleSpriteLocalRect;
    }
    syncReducedMotion();

    m_autoHideTimer.stop();
    for (auto* t : m_staggerTimers) {
        t->stop();
    }
    m_retargetAnim->stop();

    m_layout = computeLayout(petGlobalGeometry, m_lastSpriteLocalRect, screenBoundsForPet(petGlobalGeometry));
    setGeometry(m_layout.overlayRect);

    for (auto* btn : m_buttons) {
        btn->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        btn->show();
    }

    if (!isVisible()) {
        show();
#ifdef Q_OS_MACOS
        MacWindowHelper::configureFloatingOverlay(this, true);
#endif
    }
    raise();
    m_openedClock.restart();
    installOutsideDismissObservers();

    // If already fully open, keep icons steady and reset the auto-hide timer.
    if (m_state == State::Open) {
        for (int i = 0; i < ActionCount; ++i) {
            m_progress[i] = 1.0;
        }
        applyAllIconProgress();
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        return;
    }

    if (isReducedMotionActive()) {
        for (int i = 0; i < ActionCount; ++i) {
            m_pathAnims[i]->stop();
            m_progress[i] = 1.0;
        }
        applyAllIconProgress();
        m_state = State::Open;
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        emit revealed();
        return;
    }

    const bool freshReveal = (m_state == State::Closed);
    m_state = State::Opening;

    if (freshReveal) {
        for (int i = 0; i < ActionCount; ++i) {
            m_progress[i] = 0.0;
            m_buttons[i]->resetVisualState();
            applyIconProgress(i, 0.0);
        }
        startRevealForIndex(SpotifyIndex);
        m_staggerTimers[SettingsIndex]->start(kStaggerStepMs);
        m_staggerTimers[ExitIndex]->start(kStaggerStepMs * 2);
    } else {
        for (int i = 0; i < ActionCount; ++i) {
            startRevealForIndex(i);
        }
    }

    if (!isAnyButtonHovered()) {
        m_autoHideTimer.start(m_autoHideMs);
    }
}

void QuickActionMenu::startRevealForIndex(int index)
{
    if (m_state != State::Opening || index < 0 || index >= ActionCount) {
        return;
    }
    const qreal startProg = m_progress[index];
    const int fullMs = kRevealBaseMs + index * 10;
    {
        QSignalBlocker blocker(m_pathAnims[index]);
        m_pathAnims[index]->stop();
        m_pathAnims[index]->setDuration(std::max(120, qRound(fullMs * (1.0 - startProg))));
        m_pathAnims[index]->setEasingCurve(QEasingCurve::OutCubic);
        m_pathAnims[index]->setStartValue(startProg);
        m_pathAnims[index]->setEndValue(1.0);
        m_pathAnims[index]->setCurrentTime(0);
    }
    m_pathAnims[index]->start();
}

void QuickActionMenu::hideMenu()
{
    m_autoHideTimer.stop();
    for (auto* t : m_staggerTimers) {
        t->stop();
    }
    removeOutsideDismissObservers();

    if (m_state == State::Closed || m_state == State::Closing) {
        return;
    }

    if (isReducedMotionActive()) {
        finishDismissal();
        return;
    }

    m_state = State::Closing;

    // Retrace all three circular arcs in reverse (current progress -> 0.0).
    for (int i = 0; i < ActionCount; ++i) {
        const qreal startProg = m_progress[i];
        {
            QSignalBlocker blocker(m_pathAnims[i]);
            m_pathAnims[i]->stop();
            m_pathAnims[i]->setDuration(std::max(90, qRound(kDismissMs * std::max<qreal>(0.25, startProg))));
            m_pathAnims[i]->setEasingCurve(QEasingCurve::InCubic);
            m_pathAnims[i]->setStartValue(startProg);
            m_pathAnims[i]->setEndValue(0.0);
            m_pathAnims[i]->setCurrentTime(0);
        }
        m_pathAnims[i]->start();
    }
}

void QuickActionMenu::hideImmediately()
{
    m_autoHideTimer.stop();
    for (auto* t : m_staggerTimers) {
        t->stop();
    }
    m_retargetAnim->stop();
    removeOutsideDismissObservers();
    finishDismissal();
}

void QuickActionMenu::finishDismissal()
{
    m_retargetAnim->stop();
    for (int i = 0; i < ActionCount; ++i) {
        m_pathAnims[i]->stop();
        m_progress[i] = 0.0;
        if (m_buttons[i]) {
            m_buttons[i]->resetVisualState();
            m_buttons[i]->setMenuOpacity(0.0);
            m_buttons[i]->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_buttons[i]->hide();
        }
    }

    const bool wasActive = (m_state != State::Closed);
    m_state = State::Closed;
    hide();
    if (wasActive) {
        emit dismissed();
    }
}

void QuickActionMenu::resetAutoHideTimer()
{
    if ((m_state == State::Open || m_state == State::Opening) && !isAnyButtonHovered()) {
        m_autoHideTimer.start(m_autoHideMs);
    }
}

void QuickActionMenu::checkAnimationCompletion()
{
    for (int i = 0; i < ActionCount; ++i) {
        if (m_pathAnims[i]->state() == QAbstractAnimation::Running
            || m_staggerTimers[i]->isActive()) {
            return;
        }
    }

    if (m_state == State::Opening) {
        for (int i = 0; i < ActionCount; ++i) {
            m_progress[i] = 1.0;
        }
        m_state = State::Open;
        emit revealed();
    } else if (m_state == State::Closing) {
        finishDismissal();
    }
}

void QuickActionMenu::onButtonHoveredChanged(bool hovered)
{
    if (hovered) {
        m_autoHideTimer.stop();
        if (m_state == State::Closing && m_lastPetRect.isValid()) {
            showMenu(m_lastPetRect, m_lastSpriteLocalRect);
        }
        return;
    }

    if (!isAnyButtonHovered() && (m_state == State::Open || m_state == State::Opening)) {
        m_autoHideTimer.start(m_leaveHideMs);
    }
}

void QuickActionMenu::onSpotifyClicked()
{
    m_autoHideTimer.stop();
    emit spotifyRequested();
    hideMenu();
}

void QuickActionMenu::onSettingsClicked()
{
    m_autoHideTimer.stop();
    emit settingsRequested();
    hideMenu();
}

void QuickActionMenu::onExitClicked()
{
    m_autoHideTimer.stop();
    emit exitRequested();
    hideMenu();
}

void QuickActionMenu::installOutsideDismissObservers()
{
    if (!m_appFilterInstalled && qApp) {
        qApp->installEventFilter(this);
        m_appFilterInstalled = true;
    }

    if (!m_nativeOutsideObserverEnabled) {
        return;
    }

#ifdef Q_OS_WIN
    if (!m_nativeObserverToken) {
        g_activeWinMenu = this;
        HWINEVENTHOOK hook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            winForegroundEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        m_nativeObserverToken = reinterpret_cast<void*>(hook);
    }
#elif defined(Q_OS_MACOS)
    if (!m_nativeObserverToken) {
        QPointer<QuickActionMenu> guard(this);
        m_nativeObserverToken = MacWindowHelper::installOutsideClickMonitor([guard]() {
            if (!guard) {
                return;
            }
            QMetaObject::invokeMethod(guard, [guard]() {
                if (guard && (guard->state() == QuickActionMenu::State::Open
                              || guard->state() == QuickActionMenu::State::Opening)) {
                    guard->hideMenu();
                }
            }, Qt::QueuedConnection);
        });
    }
#endif
}

void QuickActionMenu::removeOutsideDismissObservers()
{
    if (m_appFilterInstalled && qApp) {
        qApp->removeEventFilter(this);
        m_appFilterInstalled = false;
    }

#ifdef Q_OS_WIN
    if (m_nativeObserverToken) {
        UnhookWinEvent(reinterpret_cast<HWINEVENTHOOK>(m_nativeObserverToken));
        m_nativeObserverToken = nullptr;
    }
    if (g_activeWinMenu == this) {
        g_activeWinMenu.clear();
    }
#elif defined(Q_OS_MACOS)
    if (m_nativeObserverToken) {
        MacWindowHelper::removeOutsideClickMonitor(m_nativeObserverToken);
        m_nativeObserverToken = nullptr;
    }
#endif
}

bool QuickActionMenu::isGlobalPointInsidePetOrButtons(const QPoint& globalPos) const
{
    const QRect petRect = m_anchorWidget ? m_anchorWidget->frameGeometry() : m_lastPetRect;
    if (petRect.isValid() && petRect.contains(globalPos)) {
        return true;
    }
    for (const auto* btn : m_buttons) {
        if (btn && btn->isVisible()) {
            const QRect btnGlobal(btn->mapToGlobal(QPoint(0, 0)), btn->size());
            if (btnGlobal.contains(globalPos)) {
                return true;
            }
        }
    }
    return false;
}

bool QuickActionMenu::eventFilter(QObject* watched, QEvent* event)
{
    if (m_state != State::Open && m_state != State::Opening) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ApplicationDeactivate) {
        hideMenu();
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<const QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        if (!isGlobalPointInsidePetOrButtons(globalPos)) {
            hideMenu();
        }
        return false;
    }

    return QWidget::eventFilter(watched, event);
}
