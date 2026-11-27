#include "trivia/QuizPopup.h"

#ifdef Q_OS_MACOS
#  include "platform/MacWindowHelper.h"
#endif
#include "ui/PixelTheme.h"

#include <QEnterEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>

namespace {

// Same tokens as the shared QSpinBox/QTimeEdit/QComboBox inputs in PixelTheme::styleSheet().
const QString kAnswerEditStyle = QStringLiteral(
    "QLineEdit {"
    "  background: rgba(0, 0, 0, 72);"
    "  border: 1px solid rgba(255, 255, 255, 12);"
    "  border-radius: 8px;"
    "  padding: 2px 8px;"
    "  color: #F3F6FC;"
    "  font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;"
    "  font-size: 12px;"
    "  font-weight: 600;"
    "  min-height: 22px;"
    "  selection-background-color: rgba(85, 150, 245, 115);"
    "  selection-color: #FFFFFF;"
    "}"
    "QLineEdit:hover {"
    "  border-color: rgba(138, 185, 255, 85);"
    "  background: rgba(255, 255, 255, 18);"
    "}"
    "QLineEdit:focus {"
    "  border-color: rgba(110, 168, 255, 165);"
    "  background: rgba(255, 255, 255, 22);"
    "}");

QColor rarityColor(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return PixelTheme::kSecondaryText;
    case Rarity::Uncommon:
        return PixelTheme::kPrimaryAccent;
    case Rarity::Rare:
        return PixelTheme::kPrimaryAccentHover;
    case Rarity::Legendary:
        return PixelTheme::kPrimaryText;
    case Rarity::Incorrect:
        return PixelTheme::kDangerText;
    }
    return PixelTheme::kPrimaryText;
}

bool reducedMotion()
{
#ifdef Q_OS_MACOS
    return MacWindowHelper::prefersReducedMotion();
#else
    return false;
#endif
}

} // namespace

QuizPopup::QuizPopup(QWidget* parent)
#ifdef Q_OS_MACOS
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
#else
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
#endif
{
    PixelTheme::ensureFontsLoaded();
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(kWidth);
    setStyleSheet(PixelTheme::styleSheet());
    setAccessibleName(QStringLiteral("Trivia check-in"));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    m_categoryLabel = new QLabel(this);
    m_categoryLabel->setObjectName(QStringLiteral("sectionSummary"));
    header->addWidget(m_categoryLabel, 1, Qt::AlignVCenter);
    m_closeBtn = new PixelIconButton(PixelIcon::Close, 26, 11, false, this);
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    m_closeBtn->setToolTip(QStringLiteral("Dismiss"));
    m_closeBtn->setAccessibleName(QStringLiteral("Dismiss trivia question"));
    header->addWidget(m_closeBtn, 0, Qt::AlignTop);
    root->addLayout(header);

    m_promptLabel = new QLabel(this);
    m_promptLabel->setObjectName(QStringLiteral("toastTitle"));
    m_promptLabel->setWordWrap(true);
    root->addWidget(m_promptLabel);

    m_answerRow = new QWidget(this);
    auto* answerLayout = new QHBoxLayout(m_answerRow);
    answerLayout->setContentsMargins(0, 0, 0, 0);
    answerLayout->setSpacing(6);
    m_answerEdit = new QLineEdit(m_answerRow);
    m_answerEdit->setObjectName(QStringLiteral("triviaAnswerEdit"));
    m_answerEdit->setPlaceholderText(QStringLiteral("Type an answer…"));
    m_answerEdit->setStyleSheet(kAnswerEditStyle);
    m_answerEdit->setFocusPolicy(Qt::ClickFocus);
    m_answerEdit->setMaxLength(80);
    m_answerEdit->installEventFilter(this);
    answerLayout->addWidget(m_answerEdit, 1);
    root->addWidget(m_answerRow);

    m_resultLabel = new QLabel(this);
    m_resultLabel->setObjectName(QStringLiteral("triviaResult"));
    m_resultLabel->setWordWrap(true);
    m_resultLabel->hide();
    root->addWidget(m_resultLabel);

    m_explanationLabel = new QLabel(this);
    m_explanationLabel->setObjectName(QStringLiteral("muted"));
    m_explanationLabel->setWordWrap(true);
    m_explanationLabel->hide();
    root->addWidget(m_explanationLabel);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addStretch(1);
    m_skipBtn = new QPushButton(QStringLiteral("Skip"), this);
    m_submitBtn = new QPushButton(QStringLiteral("Submit"), this);
    m_submitBtn->setObjectName(QStringLiteral("primary"));
    for (QPushButton* btn : {m_skipBtn, m_submitBtn}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        actions->addWidget(btn);
    }
    root->addLayout(actions);

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);

    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    connect(m_fade, &QPropertyAnimation::finished, this, &QuizPopup::finishFadeOut);

    m_slide = new QPropertyAnimation(this, "pos", this);
    m_slide->setDuration(kFadeInMs);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);

    m_autoHide.setSingleShot(true);
    connect(&m_autoHide, &QTimer::timeout, this, &QuizPopup::dismiss);

    connect(m_submitBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        if (m_showingResult) {
            dismiss();
        } else {
            submit();
        }
    });
    connect(m_skipBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit skipRequested();
        dismiss();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        if (!m_showingResult) {
            emit skipRequested();
        }
        dismiss();
    });
    connect(m_answerEdit, &QLineEdit::returnPressed, this, &QuizPopup::submit);

    hide();
}

QuizPopup::~QuizPopup()
{
    m_autoHide.stop();
}

QPushButton* QuizPopup::closeButton() const
{
    return m_closeBtn;
}

QMargins QuizPopup::bubbleMargins() const
{
    // 5px glass inset + content padding; the pet-facing side adds the tail height.
    constexpr int kSide = 16;
    constexpr int kTop = 13;
    constexpr int kBottom = 14;
    QMargins m(kSide, kTop, kSide, kBottom);
    switch (m_placement) {
    case Placement::Above:
        m.setBottom(m.bottom() + kTailHeight);
        break;
    case Placement::Below:
        m.setTop(m.top() + kTailHeight);
        break;
    case Placement::Left:
        m.setRight(m.right() + kTailHeight);
        break;
    case Placement::Right:
        m.setLeft(m.left() + kTailHeight);
        break;
    }
    return m;
}

QuizPopup::Layout QuizPopup::calculateLayout(const QRect& petRect,
                                             const QSize& popupSize,
                                             const QRect& available,
                                             int gap)
{
    const int w = popupSize.width();
    const int h = popupSize.height();
    const int maxX = std::max(available.left(), available.right() - w + 1);
    const int maxY = std::max(available.top(), available.bottom() - h + 1);
    const auto clampPoint = [&](int x, int y) {
        return QPoint(std::clamp(x, available.left(), maxX), std::clamp(y, available.top(), maxY));
    };

    const int centeredX = petRect.center().x() - w / 2;
    const int centeredY = petRect.center().y() - h / 2;

    if (petRect.top() - gap - h >= available.top()) {
        return {clampPoint(centeredX, petRect.top() - gap - h), Placement::Above};
    }
    if (petRect.right() + 1 + gap + w - 1 <= available.right()) {
        return {clampPoint(petRect.right() + 1 + gap, centeredY), Placement::Right};
    }
    if (petRect.left() - gap - w >= available.left()) {
        return {clampPoint(petRect.left() - gap - w, centeredY), Placement::Left};
    }
    return {clampPoint(centeredX, petRect.bottom() + 1 + gap), Placement::Below};
}

void QuizPopup::showQuestion(const Question& question, const QRect& petGlobalGeometry)
{
    m_fadingOut = false;
    m_showingResult = false;
    m_hovered = false;
    m_autoHide.stop();

    m_categoryLabel->setText(question.category.isEmpty()
                                 ? QStringLiteral("Trivia check-in")
                                 : QStringLiteral("Trivia · %1").arg(question.category));
    m_promptLabel->setText(question.prompt);
    m_answerEdit->clear();
    m_answerEdit->setEnabled(true);
    m_answerRow->show();
    m_resultLabel->hide();
    m_explanationLabel->hide();
    m_skipBtn->show();
    m_submitBtn->setText(QStringLiteral("Submit"));

    m_lastPetRect = petGlobalGeometry;
    reposition(petGlobalGeometry);
    const QPoint finalPos = pos();

    show();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, false);
#endif
    raise();
    startFadeIn(finalPos);
}

void QuizPopup::showResult(const AnswerResult& result, const QString& explanation)
{
    m_showingResult = true;
    m_answerEdit->setEnabled(false);
    m_answerRow->hide();
    m_skipBtn->hide();
    m_submitBtn->setText(QStringLiteral("Done"));

    QString text;
    if (result.empty) {
        text = QStringLiteral("No answer given.");
    } else if (result.duplicate) {
        text = QStringLiteral("%1 was already counted · +0").arg(result.matchedCanonical);
    } else if (result.isCorrect()) {
        text = QStringLiteral("%1 · %2 · +%3")
                   .arg(result.matchedCanonical, Trivia::rarityName(result.rarity))
                   .arg(result.points);
    } else {
        text = QStringLiteral("Not on Chiikawa's list · +0");
    }
    m_resultLabel->setText(text);
    m_resultLabel->show();

    // Subtle feedback: the rank text fades in with the same 180 ms timing as the popup.
    const QColor color = rarityColor(result.rarity);
    const QString family = result.rarity == Rarity::Legendary || result.rarity == Rarity::Rare
        ? PixelTheme::displayFontFamily()
        : QStringLiteral("Segoe UI");
    auto* reveal = new QVariantAnimation(this);
    reveal->setDuration(reducedMotion() ? 0 : kFadeInMs);
    reveal->setStartValue(0);
    reveal->setEndValue(255);
    reveal->setEasingCurve(QEasingCurve::OutCubic);
    const auto applyAlpha = [this, color, family](int alpha) {
        QColor c = color;
        c.setAlpha(std::clamp(alpha, 0, 255));
        m_resultLabel->setStyleSheet(QStringLiteral(
            "QLabel#triviaResult { color: rgba(%1, %2, %3, %4); font-family: '%5', 'Segoe UI', sans-serif;"
            " font-size: 13px; font-weight: 700; background: transparent; }")
                                         .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
                                         .arg(family));
    };
    applyAlpha(0);
    connect(reveal, &QVariantAnimation::valueChanged, this,
            [applyAlpha](const QVariant& v) { applyAlpha(v.toInt()); });
    reveal->start(QAbstractAnimation::DeleteWhenStopped);

    m_explanationLabel->setText(explanation);
    m_explanationLabel->setVisible(!explanation.isEmpty());

    reposition(m_lastPetRect);
    update();

    m_remainingMs = kResultDisplayMs;
    m_remainingClock.start();
    if (!m_hovered) {
        m_autoHide.start(m_remainingMs);
    }
}

void QuizPopup::followPet(const QRect& petGlobalGeometry)
{
    m_lastPetRect = petGlobalGeometry;
    if (isVisible() && !m_fadingOut && m_slide->state() != QAbstractAnimation::Running) {
        reposition(petGlobalGeometry);
    }
}

void QuizPopup::reposition(const QRect& petGlobalGeometry)
{
    QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    const auto fitHeight = [this]() {
        layout()->setContentsMargins(bubbleMargins());
        layout()->activate();
        const int h = layout()->heightForWidth(kWidth);
        resize(kWidth, h > 0 ? h : sizeHint().height());
    };

    // Margins depend on which side carries the tail, so size once, place, then resize for that side.
    fitHeight();
    m_placement = calculateLayout(petGlobalGeometry, size(), avail).placement;
    fitHeight();
    move(calculateLayout(petGlobalGeometry, size(), avail).pos);
    update();
}

void QuizPopup::dismiss()
{
    if (!isVisible() || m_fadingOut) {
        return;
    }
    startFadeOut();
}

int QuizPopup::remainingDismissMs() const
{
    if (!isVisible() || m_fadingOut || !m_showingResult) {
        return 0;
    }
    if (m_hovered || !m_autoHide.isActive()) {
        return std::max(0, m_remainingMs);
    }
    const qint64 elapsed = m_remainingClock.isValid() ? m_remainingClock.elapsed() : 0;
    return static_cast<int>(std::max<qint64>(0, m_remainingMs - elapsed));
}

void QuizPopup::engage()
{
    if (m_showingResult || !m_answerEdit->isEnabled()) {
        return;
    }
    activateWindow();
    raise();
    m_answerEdit->setFocus(Qt::MouseFocusReason);
}

void QuizPopup::submit()
{
    if (m_showingResult) {
        return;
    }
    const QString text = m_answerEdit->text();
    if (text.trimmed().isEmpty()) {
        engage();
        return;
    }
    emit answerSubmitted(text);
}

void QuizPopup::startFadeIn(const QPoint& finalPos)
{
    m_fade->stop();
    m_fade->setDuration(kFadeInMs);
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(1.0);
    m_fade->start();

    m_slide->stop();
    if (reducedMotion()) {
        move(finalPos);
        return;
    }
    const int dy = m_placement == Placement::Below ? -kEntranceSlidePx : kEntranceSlidePx;
    const QPoint startPos = finalPos + QPoint(0, dy);
    move(startPos);
    m_slide->setStartValue(startPos);
    m_slide->setEndValue(finalPos);
    m_slide->start();
}

void QuizPopup::startFadeOut()
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

void QuizPopup::finishFadeOut()
{
    if (!m_fadingOut) {
        return;
    }
    m_fadingOut = false;
    m_hovered = false;
    m_showingResult = false;
    if (m_answerEdit->hasFocus()) {
        m_answerEdit->clearFocus();
    }
    hide();
    emit closed();
}

void QuizPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // One combined bubble + tail path so the tail never double-darkens where it meets the body.
    constexpr int kInset = 5;
    QRectF body = QRectF(rect()).adjusted(kInset, kInset, -kInset, -kInset);
    QPolygonF tail;
    const qreal halfBase = 7.0;
    switch (m_placement) {
    case Placement::Above: {
        body.adjust(0, 0, 0, -kTailHeight);
        const qreal cx = std::clamp<qreal>(m_lastPetRect.center().x() - x(), body.left() + 18, body.right() - 18);
        tail << QPointF(cx - halfBase, body.bottom() - 1) << QPointF(cx, body.bottom() + kTailHeight)
             << QPointF(cx + halfBase, body.bottom() - 1);
        break;
    }
    case Placement::Below: {
        body.adjust(0, kTailHeight, 0, 0);
        const qreal cx = std::clamp<qreal>(m_lastPetRect.center().x() - x(), body.left() + 18, body.right() - 18);
        tail << QPointF(cx - halfBase, body.top() + 1) << QPointF(cx, body.top() - kTailHeight)
             << QPointF(cx + halfBase, body.top() + 1);
        break;
    }
    case Placement::Left: {
        body.adjust(0, 0, -kTailHeight, 0);
        const qreal cy = std::clamp<qreal>(m_lastPetRect.center().y() - y(), body.top() + 18, body.bottom() - 18);
        tail << QPointF(body.right() - 1, cy - halfBase) << QPointF(body.right() + kTailHeight, cy)
             << QPointF(body.right() - 1, cy + halfBase);
        break;
    }
    case Placement::Right: {
        body.adjust(kTailHeight, 0, 0, 0);
        const qreal cy = std::clamp<qreal>(m_lastPetRect.center().y() - y(), body.top() + 18, body.bottom() - 18);
        tail << QPointF(body.left() + 1, cy - halfBase) << QPointF(body.left() - kTailHeight, cy)
             << QPointF(body.left() + 1, cy + halfBase);
        break;
    }
    }

    QPainterPath bubble;
    bubble.addRoundedRect(body, PixelTheme::kRadiusBubble, PixelTheme::kRadiusBubble);
    QPainterPath tailPath;
    tailPath.addPolygon(tail);
    tailPath.closeSubpath();
    bubble = bubble.united(tailPath);

    // Mirrors PixelTheme::paintGlassSurface(GlassLevel::Compact): faint shadow, compact glass fill, hairline.
    p.setPen(Qt::NoPen);
    for (int i = 2; i >= 1; --i) {
        const qreal spread = static_cast<qreal>(i) * 1.1;
        p.setBrush(QColor(0, 0, 0, 4 + (3 - i) * 3));
        p.drawPath(bubble.translated(0, spread * 0.6));
    }
    QColor fill = PixelTheme::kGlassCompactTint;
    fill.setAlpha(PixelTheme::kGlassCompactAlpha);
    p.setBrush(fill);
    p.drawPath(bubble);
    p.setPen(QPen(PixelTheme::kBorderSubtle, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(bubble);
}

void QuizPopup::mousePressEvent(QMouseEvent* event)
{
    engage();
    QWidget::mousePressEvent(event);
}

void QuizPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_showingResult) {
            emit skipRequested();
        }
        dismiss();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool QuizPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_answerEdit) {
        if (event->type() == QEvent::MouseButtonPress) {
            engage();
        } else if (event->type() == QEvent::KeyPress
                   && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QuizPopup::enterEvent(QEnterEvent* event)
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

void QuizPopup::leaveEvent(QEvent* event)
{
    if (m_hovered) {
        m_hovered = false;
        if (isVisible() && !m_fadingOut && m_showingResult) {
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
