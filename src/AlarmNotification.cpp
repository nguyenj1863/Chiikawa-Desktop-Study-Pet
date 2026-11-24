#include "AlarmNotification.h"
#include "PixelTheme.h"
#include "SettingsDefaults.h"

#include <QCloseEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

AlarmNotification::AlarmNotification(QWidget* parent)
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
    setFixedWidth(284);
    setStyleSheet(PixelTheme::styleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    // Header row: pixel bell + "Time to go!" + pixel X close (acts as Dismiss)
    auto* header = new QHBoxLayout();
    header->setSpacing(8);

    m_bellIcon = new QLabel(this);
    m_bellIcon->setFixedSize(18, 18);
    m_bellIcon->setAlignment(Qt::AlignCenter);
    m_bellIcon->setPixmap(PixelTheme::icon(PixelIcon::Bell, PixelTheme::kPrimaryAccent, 16).pixmap(16, 16));

    m_titleLabel = new QLabel(QStringLiteral("Time to go!"), this);
    m_titleLabel->setObjectName(QStringLiteral("title"));

    m_closeBtn = new PixelIconButton(PixelIcon::Close, 24, 12, false, this);
    m_closeBtn->setToolTip(QStringLiteral("Dismiss alarm"));
    m_closeBtn->setAccessibleName(QStringLiteral("Dismiss alarm"));
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_closeBtn, &QPushButton::clicked, this, &AlarmNotification::onDismissClicked);

    header->addWidget(m_bellIcon);
    header->addWidget(m_titleLabel, 1);
    header->addWidget(m_closeBtn);
    root->addLayout(header);

    m_bodyLabel = new QLabel(QStringLiteral("Your alarm is ringing!"), this);
    m_bodyLabel->setObjectName(QStringLiteral("muted"));
    m_bodyLabel->setWordWrap(true);
    root->addWidget(m_bodyLabel);

    // Actions row: [ Snooze 5m ] [ Dismiss ]
    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);

    m_snoozeBtn = new QPushButton(PixelTheme::icon(PixelIcon::Snooze, Qt::white, 12),
                                  QStringLiteral(" Snooze 5m"), this);
    m_snoozeBtn->setObjectName(QStringLiteral("primary"));
    m_snoozeBtn->setIconSize(QSize(12, 12));
    m_snoozeBtn->setCursor(Qt::PointingHandCursor);
    m_snoozeBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_snoozeBtn, &QPushButton::clicked, this, &AlarmNotification::onSnoozeClicked);

    m_dismissBtn = new QPushButton(QStringLiteral("Dismiss"), this);
    m_dismissBtn->setCursor(Qt::PointingHandCursor);
    m_dismissBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_dismissBtn, &QPushButton::clicked, this, &AlarmNotification::onDismissClicked);

    actions->addWidget(m_snoozeBtn, 1);
    actions->addWidget(m_dismissBtn, 1);
    root->addLayout(actions);

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);

    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    m_fade->setDuration(180);

    hide();
}

void AlarmNotification::setSnoozeMinutes(int minutes)
{
    m_snoozeMinutes = SettingsDefaults::clampSnoozeMinutes(minutes);
    if (m_snoozeBtn) {
        m_snoozeBtn->setText(QStringLiteral(" Snooze %1m").arg(m_snoozeMinutes));
    }
}

void AlarmNotification::showAlarm(const QRect& petGlobalGeometry,
                                  const QTime& scheduledTime,
                                  int snoozeMinutes,
                                  bool fromSnooze)
{
    m_actionHandled = false;
    setSnoozeMinutes(snoozeMinutes);

    const QString timeText = scheduledTime.isValid()
        ? QLocale::system().toString(scheduledTime, QLocale::ShortFormat)
        : QString();

    if (fromSnooze) {
        m_bodyLabel->setText(timeText.isEmpty()
            ? QStringLiteral("Snooze finished - your alarm is ringing!")
            : QStringLiteral("Snooze finished (%1) - your alarm is ringing!").arg(timeText));
    } else {
        m_bodyLabel->setText(timeText.isEmpty()
            ? QStringLiteral("Your alarm is ringing!")
            : QStringLiteral("Your %1 alarm is ringing!").arg(timeText));
    }

    adjustSize();

    QPoint pos(petGlobalGeometry.center().x() - width() / 2,
               petGlobalGeometry.top() - height() - 10);
    clampToScreen(pos);
    move(pos);

    show();
    raise();

    m_fade->stop();
    m_fade->setStartValue(0.0);
    m_fade->setEndValue(1.0);
    m_fade->start();
}

void AlarmNotification::closeNotification()
{
    m_fade->stop();
    hide();
}

void AlarmNotification::clampToScreen(QPoint& pos) const
{
    QScreen* screen = QGuiApplication::screenAt(pos + QPoint(width() / 2, height() / 2));
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect avail = screen->availableGeometry();
    pos.setX(std::clamp(pos.x(), avail.left(), std::max(avail.left(), avail.right() - width() + 1)));
    pos.setY(std::clamp(pos.y(), avail.top(), std::max(avail.top(), avail.bottom() - height() + 1)));
}

void AlarmNotification::onSnoozeClicked()
{
    if (m_actionHandled) {
        return;
    }
    m_actionHandled = true;
    closeNotification();
    emit snoozeRequested(m_snoozeMinutes);
}

void AlarmNotification::onDismissClicked()
{
    if (m_actionHandled) {
        return;
    }
    m_actionHandled = true;
    closeNotification();
    emit dismissRequested();
}

void AlarmNotification::closeEvent(QCloseEvent* event)
{
    event->ignore();
    onDismissClicked();
}

void AlarmNotification::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintPixelCard(p, rect(), PixelTheme::kBackground, PixelTheme::kBorder, 4);
}
