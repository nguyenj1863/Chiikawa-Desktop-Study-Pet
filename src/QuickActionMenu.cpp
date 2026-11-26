#include "QuickActionMenu.h"

#include "AnimatedQuickActionButton.h"
#include "OnboardingPreview.h"
#include "PixelTheme.h"
#include "WindowCoordinator.h"

#ifdef Q_OS_MACOS
#  include "MacWindowHelper.h"
#endif

#include <QEasingCurve>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kBtnSize = AnimatedQuickActionButton::kWidgetSize;
constexpr int kBtnHalf = kBtnSize / 2;

const QColor kSpotifyGreen{0x1D, 0xB9, 0x54};
const QColor kSpotifyGreenHover{0x24, 0xD8, 0x64};

QRect screenBoundsForPet(const QRect& petRect)
{
    return WindowCoordinator::availableGeometryFor(petRect.center());
}

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
                positionButtonAt(m_settingsBtn, m_layout.settingsCurve, m_settingsProgress);
            });
    connect(m_settingsPathAnim, &QVariantAnimation::finished,
            this, &QuickActionMenu::checkAnimationCompletion);

    m_spotifyPathAnim = new QVariantAnimation(this);
    connect(m_spotifyPathAnim, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                m_spotifyProgress = value.toReal();
                positionButtonAt(m_spotifyBtn, m_layout.spotifyCurve, m_spotifyProgress);
            });
    connect(m_spotifyPathAnim, &QVariantAnimation::finished,
            this, &QuickActionMenu::checkAnimationCompletion);

    m_settingsOpacityAnim = new QPropertyAnimation(m_settingsBtn, "menuOpacity", this);
    connect(m_settingsOpacityAnim, &QPropertyAnimation::finished,
            this, &QuickActionMenu::checkAnimationCompletion);

    m_spotifyOpacityAnim = new QPropertyAnimation(m_spotifyBtn, "menuOpacity", this);
    connect(m_spotifyOpacityAnim, &QPropertyAnimation::finished,
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
}

QPointF QuickActionMenu::cubicBezier(const QPointF& p0,
                                     const QPointF& p1,
                                     const QPointF& p2,
                                     const QPointF& p3,
                                     qreal t)
{
    const qreal u = 1.0 - t;
    return u * u * u * p0
         + 3.0 * u * u * t * p1
         + 3.0 * u * t * t * p2
         + t * t * t * p3;
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

    // Default upper-right arc: Settings hugs the familiar upper-right corner;
    // Spotify curves gently above-left along Chiikawa's upper silhouette.
    QPointF origin(petLeft + petW * 0.62, petTop + petH * 0.34);
    QPointF settingsEnd(petRight - 8.0, petTop + 14.0);
    QPointF spotifyEnd(petRight - 48.0, petTop - 10.0);

    // Mirror to upper-left if near the right edge of the active screen.
    if (screenBounds.isValid() && (settingsEnd.x() + kBtnHalf + 2.0 > screenBounds.right() + 1)) {
        layout.mirroredLeft = true;
        origin = QPointF(petLeft + petW * 0.38, petTop + petH * 0.34);
        settingsEnd = QPointF(petLeft + 8.0, petTop + 14.0);
        spotifyEnd = QPointF(petLeft + 48.0, petTop - 10.0);
    }

    // Adapt along the side if near the top edge of the active screen so icons never clip.
    if (screenBounds.isValid() && (spotifyEnd.y() - kBtnHalf - 2.0 < screenBounds.top())) {
        layout.sideAdapted = true;
        if (!layout.mirroredLeft) {
            settingsEnd = QPointF(petRight + 8.0, petTop + 18.0);
            spotifyEnd = QPointF(petRight + 14.0, petTop + 64.0);
            if (spotifyEnd.x() + kBtnHalf + 2.0 > screenBounds.right() + 1) {
                layout.mirroredLeft = true;
                origin = QPointF(petLeft + petW * 0.38, petTop + petH * 0.34);
                settingsEnd = QPointF(petLeft - 8.0, petTop + 18.0);
                spotifyEnd = QPointF(petLeft - 14.0, petTop + 64.0);
            }
        } else {
            settingsEnd = QPointF(petLeft - 8.0, petTop + 18.0);
            spotifyEnd = QPointF(petLeft - 14.0, petTop + 64.0);
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
        clampPt(origin);
    }

    auto makeCurve = [&](const QPointF& p0, const QPointF& p3, qreal arcLift) -> BezierCurve {
        const QPointF delta = p3 - p0;
        if (!layout.sideAdapted) {
            // Upward curved arc emerging from behind Chiikawa
            const QPointF p1 = p0 + QPointF(delta.x() * 0.16, -arcLift);
            const QPointF p2 = p3 + QPointF(-delta.x() * 0.22, -arcLift * 0.55);
            return {p0, p1, p2, p3};
        }
        // Outward side arc when near the top of the screen
        const qreal sideSign = layout.mirroredLeft ? -1.0 : 1.0;
        const QPointF p1 = p0 + QPointF(sideSign * 22.0, delta.y() * 0.20);
        const QPointF p2 = p3 + QPointF(sideSign * 14.0, -delta.y() * 0.14);
        return {p0, p1, p2, p3};
    };

    const BezierCurve globalSettings = makeCurve(origin, settingsEnd, 24.0);
    const BezierCurve globalSpotify = makeCurve(origin, spotifyEnd, 28.0);

    // Tight bounding box covering both curved trajectories + button widget size.
    qreal minX = globalSettings.p0.x();
    qreal maxX = globalSettings.p0.x();
    qreal minY = globalSettings.p0.y();
    qreal maxY = globalSettings.p0.y();
    auto expand = [&](const QPointF& pt) {
        minX = std::min(minX, pt.x());
        maxX = std::max(maxX, pt.x());
        minY = std::min(minY, pt.y());
        maxY = std::max(maxY, pt.y());
    };
    for (const BezierCurve* c : {&globalSettings, &globalSpotify}) {
        expand(c->p0);
        expand(c->p1);
        expand(c->p2);
        expand(c->p3);
    }

    const int pad = kBtnHalf + 4;
    const int left = static_cast<int>(std::floor(minX)) - pad;
    const int top = static_cast<int>(std::floor(minY)) - pad;
    const int right = static_cast<int>(std::ceil(maxX)) + pad;
    const int bottom = static_cast<int>(std::ceil(maxY)) + pad;
    layout.overlayRect = QRect(left, top, std::max(kBtnSize, right - left), std::max(kBtnSize, bottom - top));

    const QPointF offset(layout.overlayRect.topLeft());
    auto toLocal = [&](const BezierCurve& g) -> BezierCurve {
        return {g.p0 - offset, g.p1 - offset, g.p2 - offset, g.p3 - offset};
    };
    layout.settingsCurve = toLocal(globalSettings);
    layout.spotifyCurve = toLocal(globalSpotify);

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

void QuickActionMenu::positionButtonAt(AnimatedQuickActionButton* button,
                                       const BezierCurve& curve,
                                       qreal t) const
{
    if (!button) {
        return;
    }
    const QPointF center = cubicBezier(curve.p0, curve.p1, curve.p2, curve.p3, t);
    button->move(qRound(center.x()) - kBtnHalf, qRound(center.y()) - kBtnHalf);
}

void QuickActionMenu::updateAnchor(const QRect& petGlobalGeometry)
{
    m_lastPetRect = petGlobalGeometry;
    if (m_state == State::Hidden) {
        return;
    }

    const Layout next = computeLayout(petGlobalGeometry, screenBoundsForPet(petGlobalGeometry));
    const bool geometryChanged = (next.overlayRect.size() != size())
                              || (next.mirroredLeft != m_layout.mirroredLeft)
                              || (next.sideAdapted != m_layout.sideAdapted);
    m_layout = next;
    if (geometryChanged) {
        setGeometry(m_layout.overlayRect);
        positionButtonAt(m_settingsBtn, m_layout.settingsCurve, m_settingsProgress);
        positionButtonAt(m_spotifyBtn, m_layout.spotifyCurve, m_spotifyProgress);
    } else if (pos() != m_layout.overlayRect.topLeft()) {
        // Pure translation: moving the overlay shifts both icons smoothly with Chiikawa
        // without interrupting or overwriting any running Bézier animation.
        move(m_layout.overlayRect.topLeft());
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

    // If already fully revealed, just keep icons steady and reset the auto-hide timer.
    if (m_state == State::Visible) {
        positionButtonAt(m_settingsBtn, m_layout.settingsCurve, 1.0);
        positionButtonAt(m_spotifyBtn, m_layout.spotifyCurve, 1.0);
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        return;
    }

    if (isReducedMotionActive()) {
        m_settingsPathAnim->stop();
        m_spotifyPathAnim->stop();
        m_settingsOpacityAnim->stop();
        m_spotifyOpacityAnim->stop();
        m_settingsProgress = 1.0;
        m_spotifyProgress = 1.0;
        positionButtonAt(m_settingsBtn, m_layout.settingsCurve, 1.0);
        positionButtonAt(m_spotifyBtn, m_layout.spotifyCurve, 1.0);
        m_settingsBtn->setMenuOpacity(1.0);
        m_spotifyBtn->setMenuOpacity(1.0);
        m_state = State::Visible;
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        emit revealed();
        return;
    }

    const bool freshReveal = (m_state == State::Hidden);
    m_state = State::Revealing;

    if (freshReveal) {
        m_settingsProgress = 0.0;
        m_spotifyProgress = 0.0;
        m_settingsBtn->resetVisualState();
        m_spotifyBtn->resetVisualState();
        m_settingsBtn->setMenuOpacity(0.0);
        m_spotifyBtn->setMenuOpacity(0.0);
        positionButtonAt(m_settingsBtn, m_layout.settingsCurve, 0.0);
        positionButtonAt(m_spotifyBtn, m_layout.spotifyCurve, 0.0);
    }

    m_settingsPathAnim->stop();
    m_settingsPathAnim->setDuration(std::max(100, qRound(kSettingsRevealMs * (1.0 - m_settingsProgress))));
    m_settingsPathAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_settingsPathAnim->setStartValue(m_settingsProgress);
    m_settingsPathAnim->setEndValue(1.0);
    m_settingsPathAnim->start();

    m_settingsOpacityAnim->stop();
    m_settingsOpacityAnim->setDuration(kOpacityRevealMs);
    m_settingsOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_settingsOpacityAnim->setStartValue(m_settingsBtn->menuOpacity());
    m_settingsOpacityAnim->setEndValue(1.0);
    m_settingsOpacityAnim->start();

    if (freshReveal) {
        m_spotifyPathAnim->stop();
        m_spotifyOpacityAnim->stop();
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
    if (m_state != State::Revealing) {
        return;
    }
    m_spotifyPathAnim->stop();
    m_spotifyPathAnim->setDuration(std::max(100, qRound(kSpotifyRevealMs * (1.0 - m_spotifyProgress))));
    m_spotifyPathAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_spotifyPathAnim->setStartValue(m_spotifyProgress);
    m_spotifyPathAnim->setEndValue(1.0);
    m_spotifyPathAnim->start();

    m_spotifyOpacityAnim->stop();
    m_spotifyOpacityAnim->setDuration(kOpacityRevealMs);
    m_spotifyOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_spotifyOpacityAnim->setStartValue(m_spotifyBtn->menuOpacity());
    m_spotifyOpacityAnim->setEndValue(1.0);
    m_spotifyOpacityAnim->start();
}

void QuickActionMenu::hideMenu()
{
    m_autoHideTimer.stop();
    m_spotifyStaggerTimer.stop();

    if (m_state == State::Hidden || m_state == State::Dismissing) {
        return;
    }

    if (isReducedMotionActive()) {
        finishDismissal();
        return;
    }

    m_state = State::Dismissing;

    m_settingsPathAnim->stop();
    m_settingsPathAnim->setDuration(std::max(80, qRound(kDismissMs * std::max<qreal>(0.25, m_settingsProgress))));
    m_settingsPathAnim->setEasingCurve(QEasingCurve::InCubic);
    m_settingsPathAnim->setStartValue(m_settingsProgress);
    m_settingsPathAnim->setEndValue(0.0);
    m_settingsPathAnim->start();

    m_settingsOpacityAnim->stop();
    m_settingsOpacityAnim->setDuration(kOpacityDismissMs);
    m_settingsOpacityAnim->setEasingCurve(QEasingCurve::InCubic);
    m_settingsOpacityAnim->setStartValue(m_settingsBtn->menuOpacity());
    m_settingsOpacityAnim->setEndValue(0.0);
    m_settingsOpacityAnim->start();

    m_spotifyPathAnim->stop();
    m_spotifyPathAnim->setDuration(std::max(80, qRound(kDismissMs * std::max<qreal>(0.25, m_spotifyProgress))));
    m_spotifyPathAnim->setEasingCurve(QEasingCurve::InCubic);
    m_spotifyPathAnim->setStartValue(m_spotifyProgress);
    m_spotifyPathAnim->setEndValue(0.0);
    m_spotifyPathAnim->start();

    m_spotifyOpacityAnim->stop();
    m_spotifyOpacityAnim->setDuration(kOpacityDismissMs);
    m_spotifyOpacityAnim->setEasingCurve(QEasingCurve::InCubic);
    m_spotifyOpacityAnim->setStartValue(m_spotifyBtn->menuOpacity());
    m_spotifyOpacityAnim->setEndValue(0.0);
    m_spotifyOpacityAnim->start();
}

void QuickActionMenu::hideImmediately()
{
    m_autoHideTimer.stop();
    m_spotifyStaggerTimer.stop();
    finishDismissal();
}

void QuickActionMenu::finishDismissal()
{
    m_settingsPathAnim->stop();
    m_spotifyPathAnim->stop();
    m_settingsOpacityAnim->stop();
    m_spotifyOpacityAnim->stop();

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

    const bool wasActive = (m_state != State::Hidden);
    m_state = State::Hidden;
    hide();
    if (wasActive) {
        emit dismissed();
    }
}

void QuickActionMenu::resetAutoHideTimer()
{
    if ((m_state == State::Visible || m_state == State::Revealing) && !isAnyButtonHovered()) {
        m_autoHideTimer.start(m_autoHideMs);
    }
}

void QuickActionMenu::checkAnimationCompletion()
{
    const bool anyRunning = (m_settingsPathAnim->state() == QAbstractAnimation::Running)
                         || (m_spotifyPathAnim->state() == QAbstractAnimation::Running)
                         || (m_settingsOpacityAnim->state() == QAbstractAnimation::Running)
                         || (m_spotifyOpacityAnim->state() == QAbstractAnimation::Running)
                         || m_spotifyStaggerTimer.isActive();
    if (anyRunning) {
        return;
    }

    if (m_state == State::Revealing) {
        m_settingsProgress = 1.0;
        m_spotifyProgress = 1.0;
        m_state = State::Visible;
        emit revealed();
    } else if (m_state == State::Dismissing) {
        finishDismissal();
    }
}

void QuickActionMenu::onButtonHoveredChanged(bool hovered)
{
    if (hovered) {
        m_autoHideTimer.stop();
        if (m_state == State::Dismissing && m_lastPetRect.isValid()) {
            // Smoothly restore the menu if the user re-enters an icon while dismissing.
            showMenu(m_lastPetRect);
        }
        return;
    }

    if (!isAnyButtonHovered() && (m_state == State::Visible || m_state == State::Revealing)) {
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
