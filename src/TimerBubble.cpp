#include "TimerBubble.h"

#ifdef Q_OS_MACOS
#  include "MacWindowHelper.h"
#endif

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <algorithm>

TimerBubble::TimerBubble(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #593E35;"
        "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  padding: 8px 16px;"
        "}"));

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);

    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    m_fade->setDuration(180);
    connect(m_fade, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadingOut) {
            hide();
            m_fadingOut = false;
        }
    });

    hide();
}

void TimerBubble::updateText(const QString& text)
{
    if (text == m_lastText) {
        return;
    }
    m_lastText = text;
    m_label->setText(text);
    m_label->adjustSize();
    resize(m_label->sizeHint() + QSize(12, 6));
    m_label->setGeometry(rect());
}

void TimerBubble::reposition(const QRect& petGlobalGeometry)
{
    adjustSize();
    const int x = petGlobalGeometry.center().x() - width() / 2;
    const int y = petGlobalGeometry.top() - height() - 8;

    QPoint pos(x, y);
    if (QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center())) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left(), avail.right() - width() + 1));
        pos.setY(std::clamp(pos.y(), avail.top(), avail.bottom() - height() + 1));
    }
    move(pos);
}

void TimerBubble::showNear(const QRect& petGlobalGeometry)
{
    reposition(petGlobalGeometry);
    fadeIn();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, false);
#endif
}

void TimerBubble::fadeIn()
{
    m_fadingOut = false;
    show();
    raise();
    m_fade->stop();
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(0.95);
    m_fade->start();
}

void TimerBubble::fadeOut()
{
    m_fadingOut = true;
    m_fade->stop();
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(0.0);
    m_fade->start();
}

void TimerBubble::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 15.0, 15.0);

    // Soft shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(89, 62, 53, 30));
    p.drawPath(path.translated(0, 1.5));

    // Warm cream pill fill + soft border
    p.setBrush(QColor(255, 249, 242, 242));
    p.setPen(QPen(QColor(221, 191, 179, 210), 1.5));
    p.drawPath(path);
}
