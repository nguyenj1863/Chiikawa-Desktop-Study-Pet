#include "TimerBubble.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

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
        "  color: #4a3f35;"
        "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  background: transparent;"
        "  padding: 6px 12px;"
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
    resize(m_label->sizeHint() + QSize(8, 4));
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
    path.addRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 12.0, 12.0);

    // Soft shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 28));
    p.drawPath(path.translated(0, 1.5));

    // Off-white translucent fill
    p.setBrush(QColor(255, 252, 245, 230));
    p.setPen(QPen(QColor(220, 205, 190, 180), 1.0));
    p.drawPath(path);
}
