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
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kTwoPi = 2.0 * kPi;
constexpr qreal kDegToRad = kPi / 180.0;

struct ActionSpec {
    QuickActionMenu::ActionIndex index;
    PixelIcon icon;
    QColor restColor;
    QColor hoverColor;
    qreal hoverRotationDeg;
    const char* objectName;
    const char* toolTip;
    const char* accessibleName;
};

const std::array<ActionSpec, QuickActionMenu::ActionCount>& actionSpecs()
{
    static const std::array<ActionSpec, QuickActionMenu::ActionCount> kSpecs = {{
        {QuickActionMenu::SpotifyIndex, PixelIcon::Spotify,
         QColor(0x1D, 0xB9, 0x54), QColor(0x24, 0xD8, 0x64), -6.5,
         "quickActionSpotify", "Open Spotify", "Open Spotify"},
        {QuickActionMenu::DiscordIndex, PixelIcon::Discord,
         QColor(0x58, 0x65, 0xF2), QColor(0x79, 0x83, 0xF5), 6.0,
         "quickActionDiscord", "Open Discord", "Open Discord"},
        {QuickActionMenu::LeagueIndex, PixelIcon::LeagueOfLegends,
         QColor(0xC8, 0xAA, 0x6E), QColor(0xE3, 0xC7, 0x89), -5.5,
         "quickActionLeague", "Open League of Legends", "Open League of Legends"},
        {QuickActionMenu::CursorIndex, PixelIcon::Cursor,
         QColor(0xE4, 0xE8, 0xF0), QColor(0xFF, 0xFF, 0xFF), 6.5,
         "quickActionCursor", "Open Cursor", "Open Cursor"},
        {QuickActionMenu::SettingsIndex, PixelIcon::Gear,
         PixelTheme::kPrimaryText, QColor(0xFF, 0xFF, 0xFF), -5.5,
         "quickActionSettings", "Open Settings", "Open Settings"},
        {QuickActionMenu::ExitIndex, PixelIcon::Exit,
         PixelTheme::kPrimaryText, PixelTheme::kDangerText, 5.5,
         "quickActionExit", "Exit Chiikawa", "Exit application"}
    }};
    return kSpecs;
}

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
// AppLaunchNoticePopup
// ============================================================================

AppLaunchNoticePopup::AppLaunchNoticePopup(QWidget* parent)
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
    setFixedWidth(264);
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

    m_actionBtn = new QPushButton(QStringLiteral("Open Website"), this);
    m_actionBtn->setObjectName(QStringLiteral("primary"));
    m_actionBtn->setCursor(Qt::PointingHandCursor);
    m_actionBtn->setFocusPolicy(Qt::NoFocus);

    row->addWidget(m_dismissBtn);
    row->addWidget(m_actionBtn);
    root->addLayout(row);

    connect(m_dismissBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        dismiss();
    });
    connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        if (m_fallbackUrl.isValid()) {
            emit fallbackUrlRequested(m_fallbackUrl);
        }
        dismiss();
    });

    m_autoHide.setSingleShot(true);
    connect(&m_autoHide, &QTimer::timeout, this, &AppLaunchNoticePopup::dismiss);

    hide();
}

void AppLaunchNoticePopup::showNear(const QRect& petGlobalGeometry,
                                    const QString& title,
                                    const QString& message,
                                    const QString& fallbackActionLabel,
                                    const QUrl& fallbackUrl)
{
    m_title->setText(title);
    m_body->setText(message);
    m_fallbackUrl = fallbackUrl;

    const bool hasAction = !fallbackActionLabel.isEmpty() && fallbackUrl.isValid();
    if (hasAction) {
        m_actionBtn->setText(fallbackActionLabel);
    }
    m_actionBtn->setVisible(hasAction);
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

void AppLaunchNoticePopup::dismiss()
{
    m_autoHide.stop();
    m_hovered = false;
    hide();
}

void AppLaunchNoticePopup::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    m_autoHide.stop();
    QWidget::enterEvent(event);
}

void AppLaunchNoticePopup::leaveEvent(QEvent* event)
{
    m_hovered = false;
    if (isVisible()) {
        m_autoHide.start(3000);
    }
    QWidget::leaveEvent(event);
}

void AppLaunchNoticePopup::paintEvent(QPaintEvent* event)
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
    setMouseTracking(true);

    const auto& specs = actionSpecs();
    for (int i = 0; i < ActionCount; ++i) {
        const ActionSpec& spec = specs[static_cast<std::size_t>(i)];
        auto* btn = new AnimatedQuickActionButton(
            spec.icon,
            spec.restColor,
            spec.hoverColor,
            spec.hoverRotationDeg,
            this);
        btn->setObjectName(QString::fromLatin1(spec.objectName));
        btn->setToolTip(QString::fromLatin1(spec.toolTip));
        btn->setAccessibleName(QString::fromLatin1(spec.accessibleName));
        // Centralized hit-testing in QuickActionMenu prevents Z-order raise() flicker.
        btn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_buttons[static_cast<std::size_t>(i)] = btn;

        connect(btn, &QAbstractButton::clicked,
                this, [this, i]() { triggerAction(i); });
        connect(btn, &AnimatedQuickActionButton::hoveredChanged,
                this, &QuickActionMenu::onButtonHoveredChanged);

        m_pathAnims[static_cast<std::size_t>(i)] = new QVariantAnimation(this);
        connect(m_pathAnims[static_cast<std::size_t>(i)], &QVariantAnimation::valueChanged,
                this, [this, i](const QVariant& value) {
                    m_progress[static_cast<std::size_t>(i)] = value.toReal();
                    applyIconProgress(i, m_progress[static_cast<std::size_t>(i)]);
                });
        connect(m_pathAnims[static_cast<std::size_t>(i)], &QVariantAnimation::finished,
                this, &QuickActionMenu::checkAnimationCompletion);

        m_staggerTimers[static_cast<std::size_t>(i)] = new QTimer(this);
        m_staggerTimers[static_cast<std::size_t>(i)]->setSingleShot(true);
        connect(m_staggerTimers[static_cast<std::size_t>(i)], &QTimer::timeout,
                this, [this, i]() { startRevealForIndex(i); });
    }

    applyDefaultStackOrder();

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
                    syncOutwardDirection();
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

void QuickActionMenu::setIconScalePercent(int percent)
{
    const int clamped = AnimatedQuickActionButton::clampScalePercent(percent);
    if (m_iconScalePercent == clamped) {
        return;
    }
    m_iconScalePercent = clamped;
    for (auto* btn : m_buttons) {
        if (btn) {
            btn->setScalePercent(m_iconScalePercent);
        }
    }

    if (m_state != State::Closed && m_lastPetRect.isValid()) {
        const Layout next = computeLayout(
            m_lastPetRect, m_lastSpriteLocalRect, screenBoundsForPet(m_lastPetRect), m_iconScalePercent);
        if (isReducedMotionActive()) {
            m_retargetAnim->stop();
            m_layout = next;
            setGeometry(m_layout.overlayRect);
            syncOutwardDirection();
            applyAllIconProgress();
        } else {
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
    }
}

void QuickActionMenu::setActiveFlag(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    emit activeStateChanged(m_active);
}

AnimatedQuickActionButton* QuickActionMenu::buttonAt(int index) const
{
    if (index < 0 || index >= ActionCount) {
        return nullptr;
    }
    return m_buttons[static_cast<std::size_t>(index)];
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
                                                       const QRect& screenBounds,
                                                       int iconScalePercent)
{
    return computeLayout(petGlobalGeometry, QRect{}, screenBounds, iconScalePercent);
}

QuickActionMenu::Layout QuickActionMenu::computeLayout(const QRect& petGlobalGeometry,
                                                       const QRect& visibleSpriteLocalRect,
                                                       const QRect& screenBounds,
                                                       int iconScalePercent)
{
    Layout layout;

    const int petW = std::max(40, petGlobalGeometry.width());
    const int petH = std::max(40, petGlobalGeometry.height());
    const int visualSize = AnimatedQuickActionButton::visualSizeForScale(iconScalePercent);
    const int btnSize = AnimatedQuickActionButton::widgetSizeForScale(iconScalePercent);
    const qreal btnHalf = btnSize * 0.5;
    const qreal visHalf = visualSize * 0.5;

    // Derive visible sprite rect in global coordinates (fallback to inset pet rect).
    QRect spriteGlobal;
    if (visibleSpriteLocalRect.isValid() && !visibleSpriteLocalRect.isEmpty()) {
        spriteGlobal = visibleSpriteLocalRect.translated(petGlobalGeometry.topLeft());
    } else {
        const int insetX = std::max(4, petW / 8);
        const int insetY = std::max(4, petH / 8);
        spriteGlobal = petGlobalGeometry.adjusted(insetX, insetY, -insetX, -insetY);
    }

    // Pronounced C-shaped circular arc spanning 120° (-60°..+60°) with ~15% layered-card overlap.
    constexpr qreal kHalfSpanRad = kArcHalfSpanDeg * kDegToRad;
    const qreal stepAngle = (2.0 * kHalfSpanRad) / static_cast<qreal>(ActionCount - 1); // 24°
    const qreal targetChord = visualSize * (1.0 - kTargetOverlapRatio);
    const qreal baseRadius = targetChord / (2.0 * std::sin(stepAngle * 0.5));
    const qreal radiusX = baseRadius * 1.06;
    const qreal radiusY = baseRadius * 0.96;

    // Place orbit center so even the innermost top/bottom cards (cos 60° = 0.50) clear
    // Chiikawa's visible silhouette by at least kSpriteEdgeGapPx (~18px).
    const qreal cosEdge = std::cos(kHalfSpanRad);
    const qreal centerY = spriteGlobal.top() + spriteGlobal.height() * 0.50;
    const qreal rightOrbitX = (spriteGlobal.right() + 1) + kSpriteEdgeGapPx + visHalf - cosEdge * radiusX;
    const qreal leftOrbitX = spriteGlobal.left() - kSpriteEdgeGapPx - visHalf + cosEdge * radiusX;

    // Mirror to the left side when there isn't enough room on the right screen edge.
    if (screenBounds.isValid()
        && (rightOrbitX + radiusX + btnHalf + 2.0 > screenBounds.right() + 1)) {
        const qreal spaceRight = (screenBounds.right() + 1) - (spriteGlobal.right() + 1);
        const qreal spaceLeft = spriteGlobal.left() - screenBounds.left();
        if (spaceLeft > spaceRight) {
            layout.mirroredLeft = true;
        }
    }

    QPointF orbitCenter(layout.mirroredLeft ? leftOrbitX : rightOrbitX, centerY);

    std::array<qreal, ActionCount> endAngles{};
    std::array<qreal, ActionCount> startAngles{};
    const qreal halfSteps = (ActionCount - 1) * 0.5; // 2.5 for 6 icons

    if (!layout.mirroredLeft) {
        for (int i = 0; i < ActionCount; ++i) {
            endAngles[static_cast<std::size_t>(i)] = (i - halfSteps) * stepAngle;
            // All six sweep upward counter-clockwise from lower-right of Chiikawa.
            startAngles[static_cast<std::size_t>(i)] = (84.0 + i * 4.0) * kDegToRad;
        }
    } else {
        constexpr qreal kMidAngle = 180.0 * kDegToRad;
        for (int i = 0; i < ActionCount; ++i) {
            endAngles[static_cast<std::size_t>(i)] = kMidAngle - (i - halfSteps) * stepAngle;
            // All six sweep upward clockwise from lower-left of Chiikawa.
            startAngles[static_cast<std::size_t>(i)] = (96.0 - i * 4.0) * kDegToRad;
        }
    }

    // Vertically clamp the orbit center only if needed so all 6 final icons + hover pull-out stay on-screen.
    if (screenBounds.isValid()) {
        qreal minEndY = orbitCenter.y();
        qreal maxEndY = orbitCenter.y();
        for (int i = 0; i < ActionCount; ++i) {
            const qreal y = pointOnArc(orbitCenter, radiusX, radiusY, endAngles[static_cast<std::size_t>(i)]).y();
            minEndY = std::min(minEndY, y);
            maxEndY = std::max(maxEndY, y);
        }
        const qreal allowedTop = screenBounds.top() + btnHalf + 2.0;
        const qreal allowedBottom = (screenBounds.bottom() + 1) - btnHalf - 2.0;
        if (minEndY < allowedTop) {
            orbitCenter.ry() += (allowedTop - minEndY);
        } else if (maxEndY > allowedBottom) {
            orbitCenter.ry() -= (maxEndY - allowedBottom);
        }
    }

    std::array<ArcPath, ActionCount> globalArcs{};
    for (int i = 0; i < ActionCount; ++i) {
        qreal startA = startAngles[static_cast<std::size_t>(i)];
        qreal endA = endAngles[static_cast<std::size_t>(i)];
        if (!layout.mirroredLeft) {
            while (endA >= startA) {
                endA -= kTwoPi;
            }
        } else {
            while (endA <= startA) {
                endA += kTwoPi;
            }
        }
        globalArcs[static_cast<std::size_t>(i)] = ArcPath{orbitCenter, radiusX, radiusY, startA, endA};
    }

    // Tight overlay bounding box covering the entire sweep of all six arcs + hover pull-out room.
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
            expand(evaluateArc(globalArcs[static_cast<std::size_t>(a)],
                               static_cast<qreal>(s) / static_cast<qreal>(kSamples)));
        }
    }

    const int pad = static_cast<int>(std::ceil(btnHalf)) + 4;
    const int left = static_cast<int>(std::floor(minX)) - pad;
    const int top = static_cast<int>(std::floor(minY)) - pad;
    const int right = static_cast<int>(std::ceil(maxX)) + pad;
    const int bottom = static_cast<int>(std::ceil(maxY)) + pad;
    layout.overlayRect = QRect(left, top, std::max(btnSize, right - left), std::max(btnSize, bottom - top));

    const QPointF offset(layout.overlayRect.topLeft());
    for (int i = 0; i < ActionCount; ++i) {
        ArcPath localArc = globalArcs[static_cast<std::size_t>(i)];
        localArc.center -= offset;
        layout.arcAt(i) = localArc;
    }

    return layout;
}

void QuickActionMenu::applyDefaultStackOrder()
{
    for (int i = 0; i < ActionCount; ++i) {
        if (m_buttons[static_cast<std::size_t>(i)]) {
            m_buttons[static_cast<std::size_t>(i)]->raise();
        }
    }
}

void QuickActionMenu::syncOutwardDirection()
{
    const qreal dir = m_layout.mirroredLeft ? -1.0 : 1.0;
    for (auto* btn : m_buttons) {
        if (btn) {
            btn->setOutwardDirection(dir);
        }
    }
}

QRectF QuickActionMenu::restingVisualRect(int index) const
{
    const AnimatedQuickActionButton* btn = buttonAt(index);
    if (!btn) {
        return {};
    }
    const qreal vis = static_cast<qreal>(btn->visualSize());
    const QPointF center = QRectF(btn->geometry()).center();
    return QRectF(center.x() - vis * 0.5, center.y() - vis * 0.5, vis, vis);
}

int QuickActionMenu::hitTestButton(const QPoint& localPos) const
{
    if (m_state != State::Open && m_state != State::Opening) {
        return -1;
    }

    const QPointF p(localPos);

    // First give hysteresis to the currently active pulled-out card so hovering its
    // raised surface stays rock-solid without flicker.
    if (m_hoveredIndex >= 0 && m_hoveredIndex < ActionCount) {
        const AnimatedQuickActionButton* active = m_buttons[static_cast<std::size_t>(m_hoveredIndex)];
        if (active && active->isVisible()) {
            const QRectF elevatedRect = restingVisualRect(m_hoveredIndex)
                .translated(active->hoverOffset())
                .adjusted(-2.0, -2.0, 2.0, 2.0);
            if (elevatedRect.contains(p)) {
                return m_hoveredIndex;
            }
        }
    }

    // Otherwise test resting card surfaces front-to-back matching default stack order (5 down to 0).
    for (int i = ActionCount - 1; i >= 0; --i) {
        const AnimatedQuickActionButton* btn = m_buttons[static_cast<std::size_t>(i)];
        if (btn && btn->isVisible() && restingVisualRect(i).contains(p)) {
            return i;
        }
    }
    return -1;
}

void QuickActionMenu::setHoveredIndex(int index)
{
    if (index < -1 || index >= ActionCount) {
        index = -1;
    }
    if (m_hoveredIndex == index) {
        return;
    }

    const int previous = m_hoveredIndex;
    m_hoveredIndex = index;

    if (previous >= 0 && previous < ActionCount && m_buttons[static_cast<std::size_t>(previous)]) {
        m_buttons[static_cast<std::size_t>(previous)]->setHovered(false);
    }

    applyDefaultStackOrder();

    if (m_hoveredIndex >= 0 && m_buttons[static_cast<std::size_t>(m_hoveredIndex)]) {
        AnimatedQuickActionButton* active = m_buttons[static_cast<std::size_t>(m_hoveredIndex)];
        active->raise();
        active->setHovered(true);
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
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
    if (index < 0 || index >= ActionCount || !m_buttons[static_cast<std::size_t>(index)]) {
        return;
    }
    AnimatedQuickActionButton* btn = m_buttons[static_cast<std::size_t>(index)];
    const int btnHalf = btn->widgetSizePx() / 2;
    const QPointF center = evaluateArc(m_layout.arcAt(index), t);
    btn->move(qRound(center.x()) - btnHalf, qRound(center.y()) - btnHalf);
    btn->setMenuOpacity(opacityForProgress(t));
}

void QuickActionMenu::applyAllIconProgress() const
{
    for (int i = 0; i < ActionCount; ++i) {
        applyIconProgress(i, m_progress[static_cast<std::size_t>(i)]);
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
    syncOutwardDirection();
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

    const Layout next = computeLayout(petGlobalGeometry, m_lastSpriteLocalRect, screenBoundsForPet(petGlobalGeometry), m_iconScalePercent);
    if (isRetargeting()) {
        const QPoint delta = next.overlayRect.topLeft() - m_retargetTo.overlayRect.topLeft();
        m_retargetFrom.overlayRect.translate(delta);
        m_retargetTo = next;
        applyRetargetBlend(m_retargetAnim->currentValue().toReal());
        return;
    }

    const bool geometryChanged = (next.overlayRect.size() != size())
                              || (next.mirroredLeft != m_layout.mirroredLeft);
    m_layout = next;
    syncOutwardDirection();
    if (geometryChanged) {
        setGeometry(m_layout.overlayRect);
        applyAllIconProgress();
    } else if (pos() != m_layout.overlayRect.topLeft()) {
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

    const Layout next = computeLayout(petGlobalGeometry, visibleSpriteLocalRect, screenBoundsForPet(petGlobalGeometry), m_iconScalePercent);

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
        syncOutwardDirection();
        applyAllIconProgress();
        return;
    }

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

    m_layout = computeLayout(petGlobalGeometry, m_lastSpriteLocalRect, screenBoundsForPet(petGlobalGeometry), m_iconScalePercent);
    setGeometry(m_layout.overlayRect);
    syncOutwardDirection();
    setActiveFlag(true);

    for (auto* btn : m_buttons) {
        btn->show();
    }
    if (m_hoveredIndex < 0) {
        applyDefaultStackOrder();
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

    if (m_state == State::Open) {
        for (int i = 0; i < ActionCount; ++i) {
            m_progress[static_cast<std::size_t>(i)] = 1.0;
        }
        applyAllIconProgress();
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        return;
    }

    if (isReducedMotionActive()) {
        for (int i = 0; i < ActionCount; ++i) {
            m_pathAnims[static_cast<std::size_t>(i)]->stop();
            m_progress[static_cast<std::size_t>(i)] = 1.0;
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
        m_hoveredIndex = -1;
        m_pressedIndex = -1;
        for (int i = 0; i < ActionCount; ++i) {
            m_progress[static_cast<std::size_t>(i)] = 0.0;
            m_buttons[static_cast<std::size_t>(i)]->resetVisualState();
            applyIconProgress(i, 0.0);
        }
        applyDefaultStackOrder();
        startRevealForIndex(0);
        for (int i = 1; i < ActionCount; ++i) {
            m_staggerTimers[static_cast<std::size_t>(i)]->start(kStaggerStepMs * i);
        }
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
    const auto idx = static_cast<std::size_t>(index);
    const qreal startProg = m_progress[idx];
    const int fullMs = kRevealBaseMs + index * 6;
    {
        QSignalBlocker blocker(m_pathAnims[idx]);
        m_pathAnims[idx]->stop();
        m_pathAnims[idx]->setDuration(std::max(120, qRound(fullMs * (1.0 - startProg))));
        m_pathAnims[idx]->setEasingCurve(QEasingCurve::OutCubic);
        m_pathAnims[idx]->setStartValue(startProg);
        m_pathAnims[idx]->setEndValue(1.0);
        m_pathAnims[idx]->setCurrentTime(0);
    }
    m_pathAnims[idx]->start();
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

    // Smoothly resolve any active hover pull-out before retracing arcs.
    m_pressedIndex = -1;
    setHoveredIndex(-1);

    if (isReducedMotionActive()) {
        finishDismissal();
        return;
    }

    m_state = State::Closing;

    // Retrace all six circular arcs in reverse (current progress -> 0.0).
    for (int i = 0; i < ActionCount; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const qreal startProg = m_progress[idx];
        {
            QSignalBlocker blocker(m_pathAnims[idx]);
            m_pathAnims[idx]->stop();
            m_pathAnims[idx]->setDuration(std::max(90, qRound(kDismissMs * std::max<qreal>(0.25, startProg))));
            m_pathAnims[idx]->setEasingCurve(QEasingCurve::InCubic);
            m_pathAnims[idx]->setStartValue(startProg);
            m_pathAnims[idx]->setEndValue(0.0);
            m_pathAnims[idx]->setCurrentTime(0);
        }
        m_pathAnims[idx]->start();
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
    m_hoveredIndex = -1;
    m_pressedIndex = -1;
    unsetCursor();

    for (int i = 0; i < ActionCount; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        m_pathAnims[idx]->stop();
        m_progress[idx] = 0.0;
        if (m_buttons[idx]) {
            m_buttons[idx]->resetVisualState();
            m_buttons[idx]->setMenuOpacity(0.0);
            m_buttons[idx]->hide();
        }
    }

    const bool wasActive = (m_state != State::Closed);
    m_state = State::Closed;
    hide();
    setActiveFlag(false);
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
        const auto idx = static_cast<std::size_t>(i);
        if (m_pathAnims[idx]->state() == QAbstractAnimation::Running
            || m_staggerTimers[idx]->isActive()) {
            return;
        }
    }

    if (m_state == State::Opening) {
        for (int i = 0; i < ActionCount; ++i) {
            m_progress[static_cast<std::size_t>(i)] = 1.0;
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

void QuickActionMenu::triggerAction(int index)
{
    m_autoHideTimer.stop();
    switch (index) {
    case SpotifyIndex:
        emit spotifyRequested();
        emit externalAppRequested(ExternalAppLauncher::AppId::Spotify);
        break;
    case DiscordIndex:
        emit externalAppRequested(ExternalAppLauncher::AppId::Discord);
        break;
    case LeagueIndex:
        emit externalAppRequested(ExternalAppLauncher::AppId::LeagueOfLegends);
        break;
    case CursorIndex:
        emit externalAppRequested(ExternalAppLauncher::AppId::Cursor);
        break;
    case SettingsIndex:
        emit settingsRequested();
        break;
    case ExitIndex:
        emit exitRequested();
        break;
    default:
        return;
    }
    hideMenu();
}

bool QuickActionMenu::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto* helpEvent = static_cast<QHelpEvent*>(event);
        const int idx = hitTestButton(helpEvent->pos());
        if (idx >= 0 && m_buttons[static_cast<std::size_t>(idx)]) {
            QToolTip::showText(helpEvent->globalPos(),
                               m_buttons[static_cast<std::size_t>(idx)]->toolTip(),
                               this);
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QWidget::event(event);
}

void QuickActionMenu::mouseMoveEvent(QMouseEvent* event)
{
    const int hit = hitTestButton(event->position().toPoint());
    setHoveredIndex(hit);
    QWidget::mouseMoveEvent(event);
}

void QuickActionMenu::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const int hit = hitTestButton(event->position().toPoint());
        setHoveredIndex(hit);
        m_pressedIndex = hit;
        if (m_pressedIndex >= 0 && m_buttons[static_cast<std::size_t>(m_pressedIndex)]) {
            m_buttons[static_cast<std::size_t>(m_pressedIndex)]->setPressedVisual(true);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void QuickActionMenu::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressedIndex >= 0) {
        const int pressed = m_pressedIndex;
        m_pressedIndex = -1;
        if (m_buttons[static_cast<std::size_t>(pressed)]) {
            m_buttons[static_cast<std::size_t>(pressed)]->setPressedVisual(false);
        }
        const int hit = hitTestButton(event->position().toPoint());
        if (hit == pressed) {
            triggerAction(pressed);
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void QuickActionMenu::leaveEvent(QEvent* event)
{
    m_pressedIndex = -1;
    setHoveredIndex(-1);
    QWidget::leaveEvent(event);
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
    const QPoint localPos = mapFromGlobal(globalPos);
    return hitTestButton(localPos) >= 0;
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
