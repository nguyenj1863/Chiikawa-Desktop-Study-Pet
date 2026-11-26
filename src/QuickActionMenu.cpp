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

    m_settingsBtn = new AnimatedQuickActionButton(
        PixelIcon::Gear,
        PixelTheme::kPrimaryText,
        QColor(Qt::white),
        6.0,
        this);
    m_settingsBtn->setObjectName(QStringLiteral("quickActionSettings"));
    m_settingsBtn->setToolTip(QStringLiteral("Settings"));
    m_settingsBtn->setAccessibleName(QStringLiteral("Open settings"));

    m_spotifyBtn = new AnimatedQuickActionButton(
        PixelIcon::Spotify,
        kSpotifyGreen,
        kSpotifyGreenHover,
        -6.0,
        this);
    m_spotifyBtn->setObjectName(QStringLiteral("quickActionSpotify"));
    m_spotifyBtn->setToolTip(QStringLiteral("Open Spotify"));
    m_spotifyBtn->setAccessibleName(QStringLiteral("Open Spotify"));

    connect(m_settingsBtn, &AnimatedQuickActionButton::hoveredChanged,
            this, &QuickActionMenu::onButtonHoveredChanged);
    connect(m_spotifyBtn, &AnimatedQuickActionButton::hoveredChanged,
            this, &QuickActionMenu::onButtonHoveredChanged);
    connect(m_settingsBtn, &QAbstractButton::clicked,
            this, &QuickActionMenu::onSettingsClicked);
    connect(m_spotifyBtn, &QAbstractButton::clicked,
            this, &QuickActionMenu::onSpotifyClicked);

    m_settingsPathAnim = new QVariantAnimation(this);
    connect(m_settingsPathAnim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                m_settingsProgress = value.toReal();
                applyIconProgress(m_settingsBtn, m_layout.settingsArc, m_settingsProgress);
            });
    connect(m_settingsPathAnim, &QVariantAnimation::finished,
            this, &QuickActionMenu::checkAnimationCompletion);

    m_spotifyPathAnim = new QVariantAnimation(this);
    connect(m_spotifyPathAnim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                m_spotifyProgress = value.toReal();
                applyIconProgress(m_spotifyBtn, m_layout.spotifyArc, m_spotifyProgress);
            });
    connect(m_spotifyPathAnim, &QVariantAnimation::finished,
            this, &QuickActionMenu::checkAnimationCompletion);

    m_spotifyStaggerTimer.setSingleShot(true);
    connect(&m_spotifyStaggerTimer, &QTimer::timeout,
            this, &QuickActionMenu::startSpotifyReveal);

    m_autoHideTimer.setSingleShot(true);
    connect(&m_autoHideTimer, &QTimer::timeout,
            this, &QuickActionMenu::hideMenu);

    syncReducedMotion();
    hide();
}

QuickActionMenu::~QuickActionMenu()
{
    m_autoHideTimer.stop();
    m_spotifyStaggerTimer.stop();
    removeOutsideDismissObservers();
}

void QuickActionMenu::setAnchorWidget(QWidget* petWidget)
{
    m_anchorWidget = petWidget;
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
    // Smooth synchronized opacity: 0% -> 0%, 25% -> ~28%, 50% -> ~57%, 75% -> ~83%, 100% -> 100%.
    return std::clamp(x * (1.0 + 0.55 * x - 0.55 * x * x), 0.0, 1.0);
}

QuickActionMenu::Layout QuickActionMenu::computeLayout(const QRect& petGlobalGeometry,
                                                       const QRect& screenBounds)
{
    Layout layout;

    const int petW = std::max(40, petGlobalGeometry.width());
    const int petH = std::max(40, petGlobalGeometry.height());
    const qreal petLeft = petGlobalGeometry.left();
    const qreal petTop = petGlobalGeometry.top();
    const qreal petRight = petLeft + petW;

    // Orbit center aligned with Chiikawa's body center.
    const QPointF orbitCenter(petLeft + petW * 0.50, petTop + petH * 0.52);

    // Preserve the familiar upper-right final arrangement (spaced >= kBtnSize so 48x48 widgets never overlap).
    QPointF settingsEnd(petRight - 8.0, petTop + 14.0);
    QPointF spotifyEnd(petRight - 50.0, petTop - 12.0);

    // Mirror horizontally if near the right edge of the active screen.
    if (screenBounds.isValid() && (settingsEnd.x() + kBtnHalf + 2.0 > screenBounds.right() + 1)) {
        layout.mirroredLeft = true;
        settingsEnd = QPointF(petLeft + 8.0, petTop + 14.0);
        spotifyEnd = QPointF(petLeft + 50.0, petTop - 12.0);
    }

    // Adapt along the side if near the top edge of the active screen so icons never clip.
    if (screenBounds.isValid() && (spotifyEnd.y() - kBtnHalf - 2.0 < screenBounds.top())) {
        layout.sideAdapted = true;
        if (!layout.mirroredLeft) {
            settingsEnd = QPointF(petRight + 8.0, petTop + 18.0);
            spotifyEnd = QPointF(petRight + 14.0, petTop + 68.0);
            if (spotifyEnd.x() + kBtnHalf + 2.0 > screenBounds.right() + 1) {
                layout.mirroredLeft = true;
                settingsEnd = QPointF(petLeft - 8.0, petTop + 18.0);
                spotifyEnd = QPointF(petLeft - 14.0, petTop + 68.0);
            }
        } else {
            settingsEnd = QPointF(petLeft - 8.0, petTop + 18.0);
            spotifyEnd = QPointF(petLeft - 14.0, petTop + 68.0);
        }
    }

    // Clamp final centers safely inside the screen's available geometry.
    if (screenBounds.isValid()) {
        const qreal minX = screenBounds.left() + kBtnHalf + 2.0;
        const qreal maxX = std::max(minX, static_cast<qreal>(screenBounds.right() + 1) - kBtnHalf - 2.0);
        const qreal minY = screenBounds.top() + kBtnHalf + 2.0;
        const qreal maxY = std::max(minY, static_cast<qreal>(screenBounds.bottom() + 1) - kBtnHalf - 2.0);

        auto clampPt = [&](QPointF& pt) {
            pt.setX(std::clamp(pt.x(), minX, maxX));
            pt.setY(std::clamp(pt.y(), minY, maxY));
        };
        clampPt(settingsEnd);
        clampPt(spotifyEnd);
    }

    // Build exact circular arcs from the lower sides of Chiikawa around opposite contours
    // so evaluateArc(arc, 1.0) lands at the exact final icon centers.
    auto buildArc = [&](const QPointF& endPt, bool sweepLeftSide, qreal startAngleRad) -> ArcPath {
        const qreal dx = endPt.x() - orbitCenter.x();
        const qreal dy = endPt.y() - orbitCenter.y();
        const qreal radius = std::max<qreal>(12.0, std::hypot(dx, dy));
        qreal endAngle = std::atan2(dy, dx); // in [-pi, pi]

        if (sweepLeftSide) {
            // Sweep clockwise in screen coordinates: lower-left (~+118 deg) -> 180 deg (left) -> upper-left/top.
            while (endAngle <= startAngleRad) {
                endAngle += kTwoPi;
            }
        } else {
            // Sweep counter-clockwise in screen coordinates: lower-right (~+62 deg) -> 0 deg (right) -> upper-right.
            while (endAngle >= startAngleRad) {
                endAngle -= kTwoPi;
            }
        }

        return ArcPath{orbitCenter, radius, radius, startAngleRad, endAngle};
    };

    ArcPath globalSettings;
    ArcPath globalSpotify;

    if (!layout.mirroredLeft && !layout.sideAdapted) {
        // Default: Settings sweeps around the right side from lower-right (+62 deg),
        // Spotify sweeps around the left side from lower-left (+118 deg).
        globalSettings = buildArc(settingsEnd, false, 62.0 * kDegToRad);
        globalSpotify = buildArc(spotifyEnd, true, 118.0 * kDegToRad);
    } else if (layout.mirroredLeft && !layout.sideAdapted) {
        // Near right edge: both sweep up the open left side so neither clips off the right edge.
        globalSettings = buildArc(settingsEnd, true, 118.0 * kDegToRad);
        globalSpotify = buildArc(spotifyEnd, true, 102.0 * kDegToRad);
    } else if (!layout.mirroredLeft && layout.sideAdapted) {
        // Near top edge (open right): sweep upward around the right side from lower-right.
        globalSettings = buildArc(settingsEnd, false, 70.0 * kDegToRad);
        globalSpotify = buildArc(spotifyEnd, false, 88.0 * kDegToRad);
    } else {
        // Near top-right corner (open left): sweep upward around the left side from lower-left.
        globalSettings = buildArc(settingsEnd, true, 110.0 * kDegToRad);
        globalSpotify = buildArc(spotifyEnd, true, 92.0 * kDegToRad);
    }

    // Compute a tight bounding box covering the entire sweep of both circular arcs.
    constexpr int kSamples = 24;
    qreal minX = settingsEnd.x();
    qreal maxX = settingsEnd.x();
    qreal minY = settingsEnd.y();
    qreal maxY = settingsEnd.y();
    auto expand = [&](const QPointF& pt) {
        minX = std::min(minX, pt.x());
        maxX = std::max(maxX, pt.x());
        minY = std::min(minY, pt.y());
        maxY = std::max(maxY, pt.y());
    };
    for (int i = 0; i <= kSamples; ++i) {
        const qreal t = static_cast<qreal>(i) / static_cast<qreal>(kSamples);
        expand(evaluateArc(globalSettings, t));
        expand(evaluateArc(globalSpotify, t));
    }

    const int pad = kBtnHalf + 4;
    const int left = static_cast<int>(std::floor(minX)) - pad;
    const int top = static_cast<int>(std::floor(minY)) - pad;
    const int right = static_cast<int>(std::ceil(maxX)) + pad;
    const int bottom = static_cast<int>(std::ceil(maxY)) + pad;
    layout.overlayRect = QRect(left, top, std::max(kBtnSize, right - left), std::max(kBtnSize, bottom - top));

    const QPointF offset(layout.overlayRect.topLeft());
    layout.settingsArc = globalSettings;
    layout.settingsArc.center -= offset;
    layout.spotifyArc = globalSpotify;
    layout.spotifyArc.center -= offset;

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
    if (m_settingsBtn) {
        m_settingsBtn->setReducedMotion(reduced);
    }
    if (m_spotifyBtn) {
        m_spotifyBtn->setReducedMotion(reduced);
    }
}

bool QuickActionMenu::isAnyButtonHovered() const
{
    return (m_settingsBtn && m_settingsBtn->isHovered())
        || (m_spotifyBtn && m_spotifyBtn->isHovered());
}

void QuickActionMenu::applyIconProgress(AnimatedQuickActionButton* button,
                                        const ArcPath& arc,
                                        qreal t) const
{
    if (!button) {
        return;
    }
    const QPointF center = evaluateArc(arc, t);
    button->move(qRound(center.x()) - kBtnHalf, qRound(center.y()) - kBtnHalf);
    button->setMenuOpacity(opacityForProgress(t));
}

void QuickActionMenu::updateAnchor(const QRect& petGlobalGeometry)
{
    m_lastPetRect = petGlobalGeometry;
    if (m_state == State::Closed) {
        return;
    }

    const Layout next = computeLayout(petGlobalGeometry, screenBoundsForPet(petGlobalGeometry));
    const bool geometryChanged = (next.overlayRect.size() != size())
                              || (next.mirroredLeft != m_layout.mirroredLeft)
                              || (next.sideAdapted != m_layout.sideAdapted);
    m_layout = next;
    if (geometryChanged) {
        setGeometry(m_layout.overlayRect);
        applyIconProgress(m_settingsBtn, m_layout.settingsArc, m_settingsProgress);
        applyIconProgress(m_spotifyBtn, m_layout.spotifyArc, m_spotifyProgress);
    } else if (pos() != m_layout.overlayRect.topLeft()) {
        // Pure translation: moving the overlay shifts both icons smoothly with Chiikawa
        // without interrupting or overwriting any running circular arc animation.
        move(m_layout.overlayRect.topLeft());
    }
}

void QuickActionMenu::toggleMenu(const QRect& petGlobalGeometry)
{
    switch (m_state) {
    case State::Closed:
    case State::Closing:
        showMenu(petGlobalGeometry);
        break;
    case State::Opening:
    case State::Open:
        hideMenu();
        break;
    }
}

void QuickActionMenu::showMenu(const QRect& petGlobalGeometry)
{
    m_lastPetRect = petGlobalGeometry;
    syncReducedMotion();

    m_autoHideTimer.stop();
    m_spotifyStaggerTimer.stop();

    m_layout = computeLayout(petGlobalGeometry, screenBoundsForPet(petGlobalGeometry));
    setGeometry(m_layout.overlayRect);

    m_settingsBtn->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_spotifyBtn->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_settingsBtn->show();
    m_spotifyBtn->show();

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
        applyIconProgress(m_settingsBtn, m_layout.settingsArc, 1.0);
        applyIconProgress(m_spotifyBtn, m_layout.spotifyArc, 1.0);
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        return;
    }

    if (isReducedMotionActive()) {
        m_settingsPathAnim->stop();
        m_spotifyPathAnim->stop();
        m_settingsProgress = 1.0;
        m_spotifyProgress = 1.0;
        applyIconProgress(m_settingsBtn, m_layout.settingsArc, 1.0);
        applyIconProgress(m_spotifyBtn, m_layout.spotifyArc, 1.0);
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
        m_settingsProgress = 0.0;
        m_spotifyProgress = 0.0;
        m_settingsBtn->resetVisualState();
        m_spotifyBtn->resetVisualState();
        applyIconProgress(m_settingsBtn, m_layout.settingsArc, 0.0);
        applyIconProgress(m_spotifyBtn, m_layout.spotifyArc, 0.0);
    }

    const qreal settingsStart = m_settingsProgress;
    {
        QSignalBlocker blocker(m_settingsPathAnim);
        m_settingsPathAnim->stop();
        m_settingsPathAnim->setDuration(std::max(120, qRound(kSettingsRevealMs * (1.0 - settingsStart))));
        m_settingsPathAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_settingsPathAnim->setStartValue(settingsStart);
        m_settingsPathAnim->setEndValue(1.0);
        m_settingsPathAnim->setCurrentTime(0);
    }
    m_settingsPathAnim->start();

    if (freshReveal) {
        m_spotifyPathAnim->stop();
        m_spotifyStaggerTimer.start(kSpotifyStaggerMs);
    } else {
        startSpotifyReveal();
    }

    if (!isAnyButtonHovered()) {
        m_autoHideTimer.start(m_autoHideMs);
    }
}

void QuickActionMenu::startSpotifyReveal()
{
    if (m_state != State::Opening) {
        return;
    }
    const qreal spotifyStart = m_spotifyProgress;
    {
        QSignalBlocker blocker(m_spotifyPathAnim);
        m_spotifyPathAnim->stop();
        m_spotifyPathAnim->setDuration(std::max(120, qRound(kSpotifyRevealMs * (1.0 - spotifyStart))));
        m_spotifyPathAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_spotifyPathAnim->setStartValue(spotifyStart);
        m_spotifyPathAnim->setEndValue(1.0);
        m_spotifyPathAnim->setCurrentTime(0);
    }
    m_spotifyPathAnim->start();
}

void QuickActionMenu::hideMenu()
{
    m_autoHideTimer.stop();
    m_spotifyStaggerTimer.stop();
    removeOutsideDismissObservers();

    if (m_state == State::Closed || m_state == State::Closing) {
        return;
    }

    if (isReducedMotionActive()) {
        finishDismissal();
        return;
    }

    m_state = State::Closing;

    // Retrace the exact same circular arcs in reverse (current progress -> 0.0).
    const qreal settingsStart = m_settingsProgress;
    {
        QSignalBlocker blocker(m_settingsPathAnim);
        m_settingsPathAnim->stop();
        m_settingsPathAnim->setDuration(std::max(90, qRound(kDismissMs * std::max<qreal>(0.25, settingsStart))));
        m_settingsPathAnim->setEasingCurve(QEasingCurve::InCubic);
        m_settingsPathAnim->setStartValue(settingsStart);
        m_settingsPathAnim->setEndValue(0.0);
        m_settingsPathAnim->setCurrentTime(0);
    }
    m_settingsPathAnim->start();

    const qreal spotifyStart = m_spotifyProgress;
    {
        QSignalBlocker blocker(m_spotifyPathAnim);
        m_spotifyPathAnim->stop();
        m_spotifyPathAnim->setDuration(std::max(90, qRound(kDismissMs * std::max<qreal>(0.25, spotifyStart))));
        m_spotifyPathAnim->setEasingCurve(QEasingCurve::InCubic);
        m_spotifyPathAnim->setStartValue(spotifyStart);
        m_spotifyPathAnim->setEndValue(0.0);
        m_spotifyPathAnim->setCurrentTime(0);
    }
    m_spotifyPathAnim->start();
}

void QuickActionMenu::hideImmediately()
{
    m_autoHideTimer.stop();
    m_spotifyStaggerTimer.stop();
    removeOutsideDismissObservers();
    finishDismissal();
}

void QuickActionMenu::finishDismissal()
{
    m_settingsPathAnim->stop();
    m_spotifyPathAnim->stop();

    m_settingsProgress = 0.0;
    m_spotifyProgress = 0.0;

    if (m_settingsBtn) {
        m_settingsBtn->resetVisualState();
        m_settingsBtn->setMenuOpacity(0.0);
        m_settingsBtn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_settingsBtn->hide();
    }
    if (m_spotifyBtn) {
        m_spotifyBtn->resetVisualState();
        m_spotifyBtn->setMenuOpacity(0.0);
        m_spotifyBtn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_spotifyBtn->hide();
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
    const bool anyRunning = (m_settingsPathAnim->state() == QAbstractAnimation::Running)
                         || (m_spotifyPathAnim->state() == QAbstractAnimation::Running)
                         || m_spotifyStaggerTimer.isActive();
    if (anyRunning) {
        return;
    }

    if (m_state == State::Opening) {
        m_settingsProgress = 1.0;
        m_spotifyProgress = 1.0;
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
            // Smoothly restore the menu if the user re-enters an icon while closing.
            showMenu(m_lastPetRect);
        }
        return;
    }

    if (!isAnyButtonHovered() && (m_state == State::Open || m_state == State::Opening)) {
        m_autoHideTimer.start(m_leaveHideMs);
    }
}

void QuickActionMenu::onSettingsClicked()
{
    m_autoHideTimer.stop();
    emit settingsRequested();
    hideMenu();
}

void QuickActionMenu::onSpotifyClicked()
{
    m_autoHideTimer.stop();
    emit spotifyRequested();
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
        // Out-of-context foreground observer (non-invasive, no input hook or click interception).
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
    if (m_settingsBtn && m_settingsBtn->isVisible()) {
        const QRect btnGlobal(m_settingsBtn->mapToGlobal(QPoint(0, 0)), m_settingsBtn->size());
        if (btnGlobal.contains(globalPos)) {
            return true;
        }
    }
    if (m_spotifyBtn && m_spotifyBtn->isVisible()) {
        const QRect btnGlobal(m_spotifyBtn->mapToGlobal(QPoint(0, 0)), m_spotifyBtn->size());
        if (btnGlobal.contains(globalPos)) {
            return true;
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
        // Never consume or block the user's outside click.
        return false;
    }

    return QWidget::eventFilter(watched, event);
}
