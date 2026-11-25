#include "CompletionToast.h"

#ifdef Q_OS_MACOS
#  include "MacWindowHelper.h"
#endif
#include "PixelTheme.h"

#include <QEnterEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMovie>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

CompletionToast::CompletionToast(QWidget* parent)
#ifdef Q_OS_MACOS
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
#else
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
#endif
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    setFixedWidth(292);
    setStyleSheet(PixelTheme::styleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 20, 18);
    root->setSpacing(10);

    auto* top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);

    m_gifLabel = new QLabel(this);
    m_gifLabel->setFixedSize(52, 52);
    m_gifLabel->setAlignment(Qt::AlignCenter);
    m_movie = new QMovie(QStringLiteral(":/assets/chiikawa_twirl.gif"), QByteArray(), this);
    m_movie->setCacheMode(QMovie::CacheAll);
    m_movie->setScaledSize(QSize(52, 52));
    m_gifLabel->setMovie(m_movie);
    top->addWidget(m_gifLabel, 0, Qt::AlignVCenter);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);
    textCol->addStretch(1);

    m_title = new QLabel(QStringLiteral("Study session complete!"), this);
    m_title->setObjectName(QStringLiteral("toastTitle"));
    m_title->setWordWrap(true);

    m_subtitle = new QLabel(QStringLiteral("Time for a break."), this);
    m_subtitle->setObjectName(QStringLiteral("muted"));
    m_subtitle->setWordWrap(true);

    textCol->addWidget(m_title);
    textCol->addWidget(m_subtitle);
    textCol->addStretch(1);
    top->addLayout(textCol, 1);
    root->addLayout(top);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addStretch(1);

    m_dismissBtn = new QPushButton(QStringLiteral("Dismiss"), this);
    m_againBtn = new QPushButton(QStringLiteral("Start Again"), this);
    m_againBtn->setObjectName(QStringLiteral("primary"));

    m_dismissBtn->setCursor(Qt::PointingHandCursor);
    m_againBtn->setCursor(Qt::PointingHandCursor);
    m_dismissBtn->setFocusPolicy(Qt::NoFocus);
    m_againBtn->setFocusPolicy(Qt::NoFocus);

    actions->addWidget(m_dismissBtn);
    actions->addWidget(m_againBtn);
    root->addLayout(actions);

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);

    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    connect(m_fade, &QPropertyAnimation::finished, this, &CompletionToast::finishFadeOut);

    m_slide = new QPropertyAnimation(this, "pos", this);
    m_slide->setDuration(kFadeInMs);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);

    m_autoHide.setSingleShot(true);
    connect(&m_autoHide, &QTimer::timeout, this, &CompletionToast::dismiss);
    connect(m_dismissBtn, &QPushButton::clicked, this, &CompletionToast::onDismissClicked);
    connect(m_againBtn, &QPushButton::clicked, this, &CompletionToast::onStartAgainClicked);

    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::primaryScreenChanged, this, [this](QScreen* screen) {
            bindScreenSignals(screen);
            if (isVisible()) {
                repositionToPrimaryScreen();
            }
        });
        connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen* removed) {
            if (removed == m_trackedScreen) {
                bindScreenSignals(targetScreen());
                if (isVisible()) {
                    repositionToPrimaryScreen();
                }
            }
        });
    }
    bindScreenSignals(targetScreen());

    hide();
}

CompletionToast::~CompletionToast()
{
    m_autoHide.stop();
    if (m_movie) {
        m_movie->stop();
    }
}

QPoint CompletionToast::calculateBottomRightPosition(const QRect& availableGeometry,
                                                     const QSize& toastSize,
                                                     int margin)
{
    const int safeMargin = std::max(0, margin);
    const int rawX = availableGeometry.right() - toastSize.width() - safeMargin + 1;
    const int rawY = availableGeometry.bottom() - toastSize.height() - safeMargin + 1;

    const int maxX = std::max(availableGeometry.left(), availableGeometry.right() - toastSize.width() + 1);
    const int maxY = std::max(availableGeometry.top(), availableGeometry.bottom() - toastSize.height() + 1);

    return QPoint(
        std::clamp(rawX, availableGeometry.left(), maxX),
        std::clamp(rawY, availableGeometry.top(), maxY));
}

QScreen* CompletionToast::targetScreen() const
{
    if (QScreen* primary = QGuiApplication::primaryScreen()) {
        return primary;
    }
    const auto all = QGuiApplication::screens();
    return all.isEmpty() ? nullptr : all.first();
}

void CompletionToast::bindScreenSignals(QScreen* screen)
{
    if (m_trackedScreen == screen) {
        return;
    }
    if (m_trackedScreen) {
        disconnect(m_trackedScreen, nullptr, this, nullptr);
    }
    m_trackedScreen = screen;
    if (m_trackedScreen) {
        connect(m_trackedScreen, &QScreen::availableGeometryChanged,
                this, [this](const QRect&) {
                    if (isVisible()) {
                        repositionToPrimaryScreen();
                    }
                });
        connect(m_trackedScreen, &QScreen::geometryChanged,
                this, [this](const QRect&) {
                    if (isVisible()) {
                        repositionToPrimaryScreen();
                    }
                });
    }
}

void CompletionToast::repositionToPrimaryScreen()
{
    QScreen* screen = targetScreen();
    bindScreenSignals(screen);
    if (!screen) {
        return;
    }
    adjustSize();
    const QPoint target = calculateBottomRightPosition(
        screen->availableGeometry(), size(), kScreenEdgeMarginPx);
    m_slide->stop();
    move(target);
}

void CompletionToast::showNotification(int durationMs)
{
    m_fadingOut = false;
    m_hovered = false;
    m_autoHide.stop();
    m_remainingMs = std::max(100, durationMs);

    adjustSize();
    QScreen* screen = targetScreen();
    bindScreenSignals(screen);

    QPoint finalPos = pos();
    if (screen) {
        finalPos = calculateBottomRightPosition(
            screen->availableGeometry(), size(), kScreenEdgeMarginPx);
    }

    if (m_movie && m_movie->isValid() && m_movie->state() != QMovie::Running) {
        m_movie->start();
    }

    show();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, false);
#endif
    raise();
    startFadeIn(finalPos);

    m_remainingClock.start();
    m_autoHide.start(m_remainingMs);
}

void CompletionToast::showNear(const QRect& petGlobalGeometry)
{
    Q_UNUSED(petGlobalGeometry);
    showNotification(kDefaultDisplayDurationMs);
}

int CompletionToast::remainingDismissMs() const
{
    if (!isVisible() || m_fadingOut) {
        return 0;
    }
    if (m_hovered || !m_autoHide.isActive()) {
        return std::max(0, m_remainingMs);
    }
    const qint64 elapsed = m_remainingClock.isValid() ? m_remainingClock.elapsed() : 0;
    return static_cast<int>(std::max<qint64>(0, m_remainingMs - elapsed));
}

void CompletionToast::enterEvent(QEnterEvent* event)
{
    if (isVisible() && !m_fadingOut && !m_hovered) {
        m_hovered = true;
        if (m_autoHide.isActive()) {
            const qint64 elapsed = m_remainingClock.isValid() ? m_remainingClock.elapsed() : 0;
            m_remainingMs = static_cast<int>(std::max<qint64>(0, m_remainingMs - elapsed));
            m_autoHide.stop();
        }
    }
    QWidget::enterEvent(event);
}

void CompletionToast::leaveEvent(QEvent* event)
{
    if (m_hovered) {
        m_hovered = false;
        if (isVisible() && !m_fadingOut) {
            if (m_remainingMs <= 0) {
                dismiss();
            } else {
                m_remainingClock.start();
                m_autoHide.start(m_remainingMs);
            }
        }
    }
    QWidget::leaveEvent(event);
}

void CompletionToast::startFadeIn(const QPoint& finalPos)
{
    m_fade->stop();
    m_fade->setDuration(kFadeInMs);
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(1.0);
    m_fade->start();

    m_slide->stop();
    const QPoint startPos = finalPos + QPoint(0, kEntranceSlidePx);
    move(startPos);
    m_slide->setStartValue(startPos);
    m_slide->setEndValue(finalPos);
    m_slide->start();
}

void CompletionToast::startFadeOut()
{
    m_fadingOut = true;
    m_autoHide.stop();
    m_slide->stop();
    m_fade->stop();
    m_fade->setDuration(kFadeOutMs);
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(0.0);
    m_fade->start();
}

void CompletionToast::finishFadeOut()
{
    if (!m_fadingOut) {
        return;
    }
    m_fadingOut = false;
    m_hovered = false;
    if (m_movie) {
        m_movie->stop();
    }
    hide();
    emit dismissed();
}

void CompletionToast::dismiss()
{
    if (!isVisible() || m_fadingOut) {
        return;
    }
    startFadeOut();
}

void CompletionToast::onDismissClicked()
{
    emit buttonActivated();
    dismiss();
}

void CompletionToast::onStartAgainClicked()
{
    emit buttonActivated();
    m_autoHide.stop();
    emit startAgainRequested();
    dismiss();
}

void CompletionToast::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintPixelCard(p, rect(), PixelTheme::kBackground, PixelTheme::kBorder, 4);
}
