#include "quickactions/QuickActionMenu.h"

#include "quickactions/AnimatedQuickActionButton.h"
#include "ui/OnboardingPreview.h"
#include "ui/PixelTheme.h"
#include "app/WindowCoordinator.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include "platform/MacWindowHelper.h"
#endif

#include <QApplication>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFileInfo>
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

AnimatedQuickActionButton* createButtonForShortcut(const QuickActionItem& item,
                                                   int index,
                                                   QWidget* parent)
{
    const qreal rot = (index % 2 == 0) ? -6.0 : 6.0;
    const bool hasCustomIcon = !item.customIconPath.isEmpty() && QFileInfo::exists(item.customIconPath);

    AnimatedQuickActionButton* btn = nullptr;
    if (!hasCustomIcon && item.type == QuickActionType::BuiltInApp) {
        if (item.applicationReference == QStringLiteral("spotify")) {
            btn = new AnimatedQuickActionButton(
                PixelIcon::Spotify, QColor(0x1D, 0xB9, 0x54), QColor(0x24, 0xD8, 0x64), -6.5, parent);
        } else if (item.applicationReference == QStringLiteral("discord")) {
            btn = new AnimatedQuickActionButton(
                PixelIcon::Discord, QColor(0x58, 0x65, 0xF2), QColor(0x79, 0x83, 0xF5), 6.0, parent);
        } else if (item.applicationReference == QStringLiteral("league")) {
            btn = new AnimatedQuickActionButton(
                PixelIcon::LeagueOfLegends, QColor(0xC8, 0xAA, 0x6E), QColor(0xE3, 0xC7, 0x89), -5.5, parent);
        } else if (item.applicationReference == QStringLiteral("cursor")) {
            btn = new AnimatedQuickActionButton(
                PixelIcon::Cursor, QColor(0xE4, 0xE8, 0xF0), QColor(0xFF, 0xFF, 0xFF), 6.5, parent);
        }
    } else if (!hasCustomIcon && item.type == QuickActionType::Settings) {
        btn = new AnimatedQuickActionButton(
            PixelIcon::Gear, PixelTheme::kPrimaryText, QColor(0xFF, 0xFF, 0xFF), -5.5, parent);
    } else if (!hasCustomIcon && item.type == QuickActionType::Exit) {
        btn = new AnimatedQuickActionButton(
            PixelIcon::Exit, PixelTheme::kPrimaryText, PixelTheme::kDangerText, 5.5, parent);
    }

    if (!btn) {
        btn = new AnimatedQuickActionButton(QuickActionItem::resolveIcon(item, 24), rot, parent);
    }

    btn->setObjectName(QStringLiteral("quickAction_") + item.id);
    if (item.type == QuickActionType::Exit) {
        btn->setToolTip(QStringLiteral("Exit Chiikawa"));
        btn->setAccessibleName(QStringLiteral("Exit application"));
    } else {
        const QString label = QStringLiteral("Open %1").arg(item.displayName);
        btn->setToolTip(label);
        btn->setAccessibleName(label);
    }
    btn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return btn;
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

    setShortcuts(QuickActionItem::defaultShortcuts());
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

void QuickActionMenu::setShortcuts(const QVector<QuickActionItem>& items)
{
    QVector<QuickActionItem> enabledItems;
    enabledItems.reserve(std::min(static_cast<int>(items.size()), QuickActionItem::kMaxVisibleRadialShortcuts));
    for (const QuickActionItem& item : QuickActionItem::sanitizeList(items)) {
        if (item.enabled) {
            enabledItems.push_back(item);
            if (enabledItems.size() >= QuickActionItem::kMaxVisibleRadialShortcuts) {
                break;
            }
        }
    }

    if (enabledItems == m_shortcuts && m_buttons.size() == enabledItems.size()) {
        return;
    }

    const State prevState = m_state;
    m_shortcuts = std::move(enabledItems);
    rebuildActionWidgets();

    if (prevState == State::Closed) {
        return;
    }

    if (m_shortcuts.isEmpty()) {
        hideImmediately();
        return;
    }

    if (m_lastPetRect.isValid()) {
        m_retargetAnim->stop();
        m_layout = computeLayout(m_lastPetRect, m_lastSpriteLocalRect,
                                 screenBoundsForPet(m_lastPetRect),
                                 m_iconScalePercent, actionCount());
        setGeometry(m_layout.overlayRect);
        syncOutwardDirection();
        const qreal targetProg = (prevState == State::Closing) ? 0.0 : 1.0;
        for (int i = 0; i < actionCount(); ++i) {
            m_progress[i] = targetProg;
            m_buttons[i]->show();
        }
        applyDefaultStackOrder();
        applyAllIconProgress();
        if (prevState == State::Opening) {
            m_state = State::Open;
        }
    }
}

void QuickActionMenu::rebuildActionWidgets()
{
    m_hoveredIndex = -1;
    m_pressedIndex = -1;
    unsetCursor();

    for (auto* t : m_staggerTimers) {
        if (t) { t->stop(); t->deleteLater(); }
    }
    for (auto* a : m_pathAnims) {
        if (a) { a->stop(); a->deleteLater(); }
    }
    for (auto* b : m_buttons) {
        if (b) { b->hide(); b->deleteLater(); }
    }
    m_staggerTimers.clear();
    m_pathAnims.clear();
    m_buttons.clear();
    m_progress.clear();

    const int n = static_cast<int>(m_shortcuts.size());
    m_buttons.reserve(n);
    m_pathAnims.reserve(n);
    m_staggerTimers.reserve(n);
    m_progress.fill(0.0, n);

    for (int i = 0; i < n; ++i) {
        AnimatedQuickActionButton* btn = createButtonForShortcut(m_shortcuts[i], i, this);
        btn->setScalePercent(m_iconScalePercent);
        btn->hide();
        m_buttons.push_back(btn);

        connect(btn, &QAbstractButton::clicked,
                this, [this, i]() { triggerAction(i); });
        connect(btn, &AnimatedQuickActionButton::hoveredChanged,
                this, &QuickActionMenu::onButtonHoveredChanged);

        auto* anim = new QVariantAnimation(this);
        connect(anim, &QVariantAnimation::valueChanged,
                this, [this, i](const QVariant& value) {
                    if (i < m_progress.size()) {
                        m_progress[i] = value.toReal();
                        applyIconProgress(i, m_progress[i]);
                    }
                });
        connect(anim, &QVariantAnimation::finished,
                this, &QuickActionMenu::checkAnimationCompletion);
        m_pathAnims.push_back(anim);

        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout,
                this, [this, i]() { startRevealForIndex(i); });
        m_staggerTimers.push_back(timer);
    }

    applyDefaultStackOrder();
    syncReducedMotion();
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

    if (m_state != State::Closed && m_lastPetRect.isValid() && actionCount() > 0) {
        const Layout next = computeLayout(
            m_lastPetRect, m_lastSpriteLocalRect, screenBoundsForPet(m_lastPetRect),
            m_iconScalePercent, actionCount());
        if (isReducedMotionActive() || m_layout.count() != next.count()) {
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
    if (index < 0 || index >= actionCount()) {
        return nullptr;
    }
    return m_buttons[index];
}

int QuickActionMenu::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_shortcuts.size(); ++i) {
        if (m_shortcuts[i].id == id) {
            return i;
        }
    }
    return -1;
}

AnimatedQuickActionButton* QuickActionMenu::findButtonById(const QString& id) const
{
    return buttonAt(indexOfId(id));
}

qreal QuickActionMenu::progressAt(int index) const
{
    if (index < 0 || index >= m_progress.size()) {
        return 0.0;
    }
    return m_progress[index];
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
    return std::clamp(x * (1.0 + 0.55 * x - 0.55 * x * x), 0.0, 1.0);
}

QuickActionMenu::Layout QuickActionMenu::computeLayout(const QRect& petGlobalGeometry,
                                                       const QRect& screenBounds,
                                                       int iconScalePercent,
                                                       int visibleCount)
{
    return computeLayout(petGlobalGeometry, QRect{}, screenBounds, iconScalePercent, visibleCount);
}

QuickActionMenu::Layout QuickActionMenu::computeLayout(const QRect& petGlobalGeometry,
                                                       const QRect& visibleSpriteLocalRect,
                                                       const QRect& screenBounds,
                                                       int iconScalePercent,
                                                       int visibleCount)
{
    Layout layout;
    const int count = std::clamp(visibleCount, 0, QuickActionItem::kMaxVisibleRadialShortcuts);
    if (count == 0) {
        return layout;
    }

    const int petW = std::max(40, petGlobalGeometry.width());
    const int petH = std::max(40, petGlobalGeometry.height());
    const int visualSize = AnimatedQuickActionButton::visualSizeForScale(iconScalePercent);
    const int btnSize = AnimatedQuickActionButton::widgetSizeForScale(iconScalePercent);
    const qreal btnHalf = btnSize * 0.5;
    const qreal visHalf = visualSize * 0.5;

    QRect spriteGlobal;
    if (visibleSpriteLocalRect.isValid() && !visibleSpriteLocalRect.isEmpty()) {
        spriteGlobal = visibleSpriteLocalRect.translated(petGlobalGeometry.topLeft());
    } else {
        const int insetX = std::max(4, petW / 8);
        const int insetY = std::max(4, petH / 8);
        spriteGlobal = petGlobalGeometry.adjusted(insetX, insetY, -insetX, -insetY);
    }

    qreal halfSpanDeg = 0.0;
    qreal stepAngle = 0.0;
    qreal radiusX = 0.0;
    qreal radiusY = 0.0;

    if (count == 1) {
        halfSpanDeg = 0.0;
        stepAngle = 0.0;
        radiusX = kSpriteEdgeGapPx + visHalf + 24.0;
        radiusY = 48.0;
    } else {
        halfSpanDeg = std::min<qreal>(kMaxArcHalfSpanDeg, (count - 1) * (kBaseStepDeg * 0.5));
        const qreal halfSpanRad = halfSpanDeg * kDegToRad;
        stepAngle = (2.0 * halfSpanRad) / static_cast<qreal>(count - 1);
        const qreal targetChord = visualSize * (1.0 - kTargetOverlapRatio);
        const qreal baseRadius = targetChord / (2.0 * std::sin(stepAngle * 0.5));
        radiusX = baseRadius * 1.06;
        radiusY = baseRadius * 0.96;
    }

    const qreal cosEdge = std::cos(halfSpanDeg * kDegToRad);
    const qreal centerY = spriteGlobal.top() + spriteGlobal.height() * 0.50;
    const qreal rightOrbitX = (spriteGlobal.right() + 1) + kSpriteEdgeGapPx + visHalf - cosEdge * radiusX;
    const qreal leftOrbitX = spriteGlobal.left() - kSpriteEdgeGapPx - visHalf + cosEdge * radiusX;

    if (screenBounds.isValid()
        && (rightOrbitX + radiusX + btnHalf + 2.0 > screenBounds.right() + 1)) {
        const qreal spaceRight = (screenBounds.right() + 1) - (spriteGlobal.right() + 1);
        const qreal spaceLeft = spriteGlobal.left() - screenBounds.left();
        if (spaceLeft > spaceRight) {
            layout.mirroredLeft = true;
        }
    }

    QPointF orbitCenter(layout.mirroredLeft ? leftOrbitX : rightOrbitX, centerY);

    QVector<qreal> endAngles(count);
    QVector<qreal> startAngles(count);
    const qreal halfSteps = (count - 1) * 0.5;
    const qreal startSpread = (count > 1) ? (20.0 / static_cast<qreal>(count - 1)) : 0.0;

    if (!layout.mirroredLeft) {
        for (int i = 0; i < count; ++i) {
            endAngles[i] = (i - halfSteps) * stepAngle;
            startAngles[i] = (84.0 + i * startSpread) * kDegToRad;
        }
    } else {
        constexpr qreal kMidAngle = 180.0 * kDegToRad;
        for (int i = 0; i < count; ++i) {
            endAngles[i] = kMidAngle - (i - halfSteps) * stepAngle;
            startAngles[i] = (96.0 - i * startSpread) * kDegToRad;
        }
    }

    if (screenBounds.isValid()) {
        qreal minEndY = orbitCenter.y();
        qreal maxEndY = orbitCenter.y();
        for (int i = 0; i < count; ++i) {
            const qreal y = pointOnArc(orbitCenter, radiusX, radiusY, endAngles[i]).y();
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

    QVector<ArcPath> globalArcs(count);
    for (int i = 0; i < count; ++i) {
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
    for (int a = 0; a < count; ++a) {
        for (int s = 0; s <= kSamples; ++s) {
            expand(evaluateArc(globalArcs[a], static_cast<qreal>(s) / static_cast<qreal>(kSamples)));
        }
    }

    const int pad = static_cast<int>(std::ceil(btnHalf)) + 4;
    const int left = static_cast<int>(std::floor(minX)) - pad;
    const int top = static_cast<int>(std::floor(minY)) - pad;
    const int right = static_cast<int>(std::ceil(maxX)) + pad;
    const int bottom = static_cast<int>(std::ceil(maxY)) + pad;
    layout.overlayRect = QRect(left, top, std::max(btnSize, right - left), std::max(btnSize, bottom - top));

    layout.arcs.resize(count);
    const QPointF offset(layout.overlayRect.topLeft());
    for (int i = 0; i < count; ++i) {
        ArcPath localArc = globalArcs[i];
        localArc.center -= offset;
        layout.arcs[i] = localArc;
    }

    return layout;
}

void QuickActionMenu::applyDefaultStackOrder()
{
    for (auto* btn : m_buttons) {
        if (btn) {
            btn->raise();
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

    if (m_hoveredIndex >= 0 && m_hoveredIndex < actionCount()) {
        const AnimatedQuickActionButton* active = m_buttons[m_hoveredIndex];
        if (active && active->isVisible()) {
            const QRectF elevatedRect = restingVisualRect(m_hoveredIndex)
                .translated(active->hoverOffset())
                .adjusted(-2.0, -2.0, 2.0, 2.0);
            if (elevatedRect.contains(p)) {
                return m_hoveredIndex;
            }
        }
    }

    for (int i = actionCount() - 1; i >= 0; --i) {
        const AnimatedQuickActionButton* btn = m_buttons[i];
        if (btn && btn->isVisible() && restingVisualRect(i).contains(p)) {
            return i;
        }
    }
    return -1;
}

void QuickActionMenu::setHoveredIndex(int index)
{
    if (index < -1 || index >= actionCount()) {
        index = -1;
    }
    if (m_hoveredIndex == index) {
        return;
    }

    const int previous = m_hoveredIndex;
    m_hoveredIndex = index;

    if (previous >= 0 && previous < actionCount() && m_buttons[previous]) {
        m_buttons[previous]->setHovered(false);
    }

    applyDefaultStackOrder();

    if (m_hoveredIndex >= 0 && m_buttons[m_hoveredIndex]) {
        AnimatedQuickActionButton* active = m_buttons[m_hoveredIndex];
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
    if (index < 0 || index >= actionCount() || index >= m_layout.count() || !m_buttons[index]) {
        return;
    }
    AnimatedQuickActionButton* btn = m_buttons[index];
    const int btnHalf = btn->widgetSizePx() / 2;
    const QPointF center = evaluateArc(m_layout.arcAt(index), t);
    btn->move(qRound(center.x()) - btnHalf, qRound(center.y()) - btnHalf);
    btn->setMenuOpacity(opacityForProgress(t));
}

void QuickActionMenu::applyAllIconProgress() const
{
    for (int i = 0; i < actionCount(); ++i) {
        applyIconProgress(i, m_progress[i]);
    }
}

void QuickActionMenu::applyRetargetBlend(qreal alpha)
{
    if (m_retargetFrom.count() != actionCount() || m_retargetTo.count() != actionCount()) {
        return;
    }
    const QRect unionRect = m_retargetFrom.overlayRect.united(m_retargetTo.overlayRect);
    if (geometry() != unionRect) {
        setGeometry(unionRect);
    }

    Layout blended;
    blended.overlayRect = unionRect;
    blended.mirroredLeft = m_retargetTo.mirroredLeft;
    blended.arcs.resize(actionCount());

    const QPointF fromOffset(m_retargetFrom.overlayRect.topLeft());
    const QPointF toOffset(m_retargetTo.overlayRect.topLeft());
    const QPointF unionOffset(unionRect.topLeft());

    for (int i = 0; i < actionCount(); ++i) {
        ArcPath gFrom = m_retargetFrom.arcAt(i);
        gFrom.center += fromOffset;
        ArcPath gTo = m_retargetTo.arcAt(i);
        gTo.center += toOffset;

        ArcPath gMid = lerpArc(gFrom, gTo, alpha);
        gMid.center -= unionOffset;
        blended.arcs[i] = gMid;
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
    if (m_state == State::Closed || actionCount() == 0) {
        return;
    }

    const Layout next = computeLayout(petGlobalGeometry, m_lastSpriteLocalRect,
                                      screenBoundsForPet(petGlobalGeometry),
                                      m_iconScalePercent, actionCount());
    if (isRetargeting() && m_retargetTo.count() == next.count()) {
        const QPoint delta = next.overlayRect.topLeft() - m_retargetTo.overlayRect.topLeft();
        m_retargetFrom.overlayRect.translate(delta);
        m_retargetTo = next;
        applyRetargetBlend(m_retargetAnim->currentValue().toReal());
        return;
    }

    const bool geometryChanged = (next.overlayRect.size() != size())
                              || (next.mirroredLeft != m_layout.mirroredLeft)
                              || (next.count() != m_layout.count());
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
    if (m_state == State::Closed || actionCount() == 0) {
        return;
    }

    const Layout next = computeLayout(petGlobalGeometry, visibleSpriteLocalRect,
                                      screenBoundsForPet(petGlobalGeometry),
                                      m_iconScalePercent, actionCount());

    const Layout& refLayout = isRetargeting() ? m_retargetTo : m_layout;
    if (refLayout.count() == next.count()) {
        const QPointF refOffset(refLayout.overlayRect.topLeft());
        const QPointF nextOffset(next.overlayRect.topLeft());
        qreal maxDelta = 0.0;
        for (int i = 0; i < actionCount(); ++i) {
            const QPointF a = refOffset + evaluateArc(refLayout.arcAt(i), 1.0);
            const QPointF b = nextOffset + evaluateArc(next.arcAt(i), 1.0);
            maxDelta = std::max(maxDelta, std::hypot(a.x() - b.x(), a.y() - b.y()));
        }
        if (maxDelta < kRetargetThresholdPx && next.mirroredLeft == refLayout.mirroredLeft) {
            return;
        }
    }

    if (isReducedMotionActive() || m_layout.count() != next.count()) {
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
    if (actionCount() == 0) {
        return;
    }

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

    m_layout = computeLayout(petGlobalGeometry, m_lastSpriteLocalRect,
                             screenBoundsForPet(petGlobalGeometry),
                             m_iconScalePercent, actionCount());
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
        for (int i = 0; i < actionCount(); ++i) {
            m_progress[i] = 1.0;
        }
        applyAllIconProgress();
        if (!isAnyButtonHovered()) {
            m_autoHideTimer.start(m_autoHideMs);
        }
        return;
    }

    if (isReducedMotionActive()) {
        for (int i = 0; i < actionCount(); ++i) {
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
        m_hoveredIndex = -1;
        m_pressedIndex = -1;
        for (int i = 0; i < actionCount(); ++i) {
            m_progress[i] = 0.0;
            m_buttons[i]->resetVisualState();
            applyIconProgress(i, 0.0);
        }
        applyDefaultStackOrder();
        const int staggerStep = (actionCount() > 1)
            ? std::min(26, kMaxTotalStaggerMs / (actionCount() - 1))
            : 0;
        startRevealForIndex(0);
        for (int i = 1; i < actionCount(); ++i) {
            m_staggerTimers[i]->start(staggerStep * i);
        }
    } else {
        for (int i = 0; i < actionCount(); ++i) {
            startRevealForIndex(i);
        }
    }

    if (!isAnyButtonHovered()) {
        m_autoHideTimer.start(m_autoHideMs);
    }
}

void QuickActionMenu::startRevealForIndex(int index)
{
    if (m_state != State::Opening || index < 0 || index >= actionCount()) {
        return;
    }
    const qreal startProg = m_progress[index];
    const int fullMs = kRevealBaseMs + index * 6;
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

    m_pressedIndex = -1;
    setHoveredIndex(-1);

    if (isReducedMotionActive() || actionCount() == 0) {
        finishDismissal();
        return;
    }

    m_state = State::Closing;

    for (int i = 0; i < actionCount(); ++i) {
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
    m_hoveredIndex = -1;
    m_pressedIndex = -1;
    unsetCursor();

    for (int i = 0; i < actionCount(); ++i) {
        m_pathAnims[i]->stop();
        m_progress[i] = 0.0;
        if (m_buttons[i]) {
            m_buttons[i]->resetVisualState();
            m_buttons[i]->setMenuOpacity(0.0);
            m_buttons[i]->hide();
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
    for (int i = 0; i < actionCount(); ++i) {
        if (m_pathAnims[i]->state() == QAbstractAnimation::Running
            || m_staggerTimers[i]->isActive()) {
            return;
        }
    }

    if (m_state == State::Opening) {
        for (int i = 0; i < actionCount(); ++i) {
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

void QuickActionMenu::triggerAction(int index)
{
    if (index < 0 || index >= actionCount()) {
        return;
    }
    m_autoHideTimer.stop();
    const QuickActionItem item = m_shortcuts[index];
    emit shortcutTriggered(item);

    switch (item.type) {
    case QuickActionType::BuiltInApp: {
        ExternalAppLauncher::AppId appId = ExternalAppLauncher::AppId::Spotify;
        if (ExternalAppLauncher::tryParseBuiltInAppId(item.applicationReference, appId)) {
            if (appId == ExternalAppLauncher::AppId::Spotify) {
                emit spotifyRequested();
            }
            emit externalAppRequested(appId);
        }
        break;
    }
    case QuickActionType::CustomApp:
        break;
    case QuickActionType::Settings:
        emit settingsRequested();
        break;
    case QuickActionType::Exit:
        emit exitRequested();
        break;
    }
    hideMenu();
}

bool QuickActionMenu::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto* helpEvent = static_cast<QHelpEvent*>(event);
        const int idx = hitTestButton(helpEvent->pos());
        if (idx >= 0 && idx < actionCount() && m_buttons[idx]) {
            QToolTip::showText(helpEvent->globalPos(), m_buttons[idx]->toolTip(), this);
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
        if (m_pressedIndex >= 0 && m_pressedIndex < actionCount() && m_buttons[m_pressedIndex]) {
            m_buttons[m_pressedIndex]->setPressedVisual(true);
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
        if (pressed < actionCount() && m_buttons[pressed]) {
            m_buttons[pressed]->setPressedVisual(false);
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
