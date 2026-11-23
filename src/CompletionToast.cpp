#include "CompletionToast.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

CompletionToast::CompletionToast(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    setFixedWidth(260);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    auto* top = new QHBoxLayout();
    top->setSpacing(12);

    m_gifLabel = new QLabel(this);
    m_gifLabel->setFixedSize(56, 56);
    m_gifLabel->setAlignment(Qt::AlignCenter);
    m_gifLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    m_movie = new QMovie(QStringLiteral(":/assets/chiikawa_twirl.gif"), QByteArray(), this);
    m_movie->setCacheMode(QMovie::CacheAll);
    m_movie->setScaledSize(QSize(56, 56));
    m_gifLabel->setMovie(m_movie);
    top->addWidget(m_gifLabel, 0, Qt::AlignTop);

    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(2);
    m_title = new QLabel(QStringLiteral("Study session complete!"), this);
    m_title->setWordWrap(true);
    m_title->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #5c4a3a;"
        "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "}"));
    m_subtitle = new QLabel(QStringLiteral("Time for a break."), this);
    m_subtitle->setWordWrap(true);
    m_subtitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #8a7464;"
        "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
        "  font-size: 11px;"
        "  background: transparent;"
        "}"));
    textCol->addWidget(m_title);
    textCol->addWidget(m_subtitle);
    textCol->addStretch(1);
    top->addLayout(textCol, 1);
    root->addLayout(top);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    actions->addStretch(1);

    m_dismissBtn = new QPushButton(QStringLiteral("Dismiss"), this);
    m_againBtn = new QPushButton(QStringLiteral("Start again"), this);
    m_againBtn->setObjectName(QStringLiteral("primary"));

    const QString buttonStyle = QStringLiteral(
        "QPushButton {"
        "  background: #f3e2d4;"
        "  color: #5c4a3a;"
        "  border: none;"
        "  border-radius: 10px;"
        "  padding: 5px 12px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
        "}"
        "QPushButton:hover { background: #ebd2c0; }"
        "QPushButton:pressed { background: #e0c4ae; }"
        "QPushButton#primary {"
        "  background: #f7b7a3;"
        "  color: #4a3428;"
        "}"
        "QPushButton#primary:hover { background: #f0a790; }");
    m_dismissBtn->setStyleSheet(buttonStyle);
    m_againBtn->setStyleSheet(buttonStyle);
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
    m_fade->setDuration(220);
    connect(m_fade, &QPropertyAnimation::finished, this, &CompletionToast::finishFadeOut);

    m_autoHide.setSingleShot(true);
    connect(&m_autoHide, &QTimer::timeout, this, &CompletionToast::dismiss);
    connect(m_dismissBtn, &QPushButton::clicked, this, &CompletionToast::onDismissClicked);
    connect(m_againBtn, &QPushButton::clicked, this, &CompletionToast::onStartAgainClicked);

    hide();
}

CompletionToast::~CompletionToast()
{
    m_autoHide.stop();
    if (m_movie) {
        m_movie->stop();
    }
}

void CompletionToast::clampToScreen(QPoint& pos) const
{
    QScreen* screen = QGuiApplication::screenAt(pos + QPoint(width() / 2, height() / 2));
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect avail = screen->availableGeometry();
    pos.setX(std::clamp(pos.x(), avail.left(), avail.right() - width() + 1));
    pos.setY(std::clamp(pos.y(), avail.top(), avail.bottom() - height() + 1));
}

void CompletionToast::showNear(const QRect& petGlobalGeometry)
{
    m_fadingOut = false;
    m_autoHide.stop();
    adjustSize();

    // Prefer above the pet; fall back below/ beside if needed via clamp
    QPoint pos(petGlobalGeometry.center().x() - width() / 2,
               petGlobalGeometry.top() - height() - 10);
    clampToScreen(pos);
    move(pos);

    if (m_movie && m_movie->isValid()) {
        m_movie->start();
    }

    show();
    raise();
    fadeIn();
    m_autoHide.start(8000);
}

void CompletionToast::fadeIn()
{
    m_fade->stop();
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(1.0);
    m_fade->start();
}

void CompletionToast::fadeOut()
{
    m_fadingOut = true;
    m_autoHide.stop();
    m_fade->stop();
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
    fadeOut();
}

void CompletionToast::onDismissClicked()
{
    dismiss();
}

void CompletionToast::onStartAgainClicked()
{
    m_autoHide.stop();
    emit startAgainRequested();
    dismiss();
}

void CompletionToast::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF card = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
    QPainterPath path;
    path.addRoundedRect(card, 16.0, 16.0);

    // Soft drop shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 32));
    p.drawPath(path.translated(0, 2));

    // Cream fill + coral-tinted border
    p.setBrush(QColor(255, 250, 242, 245));
    p.setPen(QPen(QColor(247, 183, 163, 200), 1.2));
    p.drawPath(path);

    // Tiny accent bar along the top edge
    QPainterPath accent;
    accent.addRoundedRect(QRectF(card.left() + 18, card.top() + 5, card.width() - 36, 3), 1.5, 1.5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(247, 183, 163, 180));
    p.drawPath(accent);
}
