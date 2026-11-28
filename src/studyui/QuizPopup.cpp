#include "studyui/QuizPopup.h"

#ifdef Q_OS_MACOS
#  include "platform/MacWindowHelper.h"
#endif
#include "ui/PixelTheme.h"
#include "studyui/SupportView.h"

#include <QEnterEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>

namespace {

QColor verdictColor(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Correct:
        return PixelTheme::kPrimaryAccentHover;
    case Verdict::Partial:
        return PixelTheme::kPrimaryAccent;
    case Verdict::Incorrect:
        return PixelTheme::kDangerText;
    case Verdict::NeedsReview:
        return PixelTheme::kSecondaryText;
    }
    return PixelTheme::kPrimaryText;
}

QString formatHint(QuestionType type)
{
    switch (type) {
    case QuestionType::SingleChoice:
        return QStringLiteral("Choose one answer.");
    case QuestionType::MultiSelect:
        return QStringLiteral("Select all that apply. More than one may be correct.");
    case QuestionType::Written:
        return QStringLiteral("Answer in your own words. Ctrl+Enter to check.");
    }
    return QString();
}

bool reducedMotion()
{
#ifdef Q_OS_MACOS
    return MacWindowHelper::prefersReducedMotion();
#else
    return false;
#endif
}

QLabel* makeLabel(const char* objectName, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->hide();
    return label;
}

QPushButton* makeButton(const QString& text, QWidget* parent, const char* objectName = nullptr)
{
    auto* btn = new QPushButton(text, parent);
    if (objectName) {
        btn->setObjectName(QString::fromLatin1(objectName));
    }
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

} // namespace

namespace {

Qt::WindowFlags popupWindowFlags(bool embedded)
{
    if (embedded) {
        return {};
    }
#ifdef Q_OS_MACOS
    return Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
#else
    return Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
#endif
}

} // namespace

QuizPopup::QuizPopup(QWidget* parent, bool embedded)
    : QWidget(parent, popupWindowFlags(embedded))
{
    m_embedded = embedded;
    PixelTheme::ensureFontsLoaded();
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    const QString transparentBody = QStringLiteral("QScrollArea#quizScroll, QWidget#quizBody { background: transparent; border: none; }");
    if (m_embedded) {
        setStyleSheet(transparentBody);   // the hosting window already provides the theme
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFixedWidth(kStudyWidth);
        setStyleSheet(PixelTheme::styleSheet() + transparentBody);
    }
    setAccessibleName(QStringLiteral("Study question"));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    if (m_embedded) {
        root->setContentsMargins(0, 0, 0, 0);
    }

    // Header: category + close
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    m_categoryLabel = new QLabel(this);
    m_categoryLabel->setObjectName(QStringLiteral("sectionSummary"));
    header->addWidget(m_categoryLabel, 1, Qt::AlignVCenter);
    m_closeBtn = new PixelIconButton(PixelIcon::Close, 26, 11, false, this);
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    m_closeBtn->setToolTip(QStringLiteral("Dismiss"));
    m_closeBtn->setAccessibleName(QStringLiteral("Dismiss question"));
    header->addWidget(m_closeBtn, 0, Qt::AlignTop);
    m_closeBtn->setVisible(!m_embedded);
    root->addLayout(header);

    // Scrollable body: everything between the header and the buttons.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("quizScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setWidgetResizable(m_embedded);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_body = new QWidget();
    m_body->setObjectName(QStringLiteral("quizBody"));
    auto* body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);

    m_promptLabel = new QLabel(m_body);
    m_promptLabel->setObjectName(QStringLiteral("toastTitle"));
    m_promptLabel->setWordWrap(true);
    body->addWidget(m_promptLabel);

    // The figure, table or data the question depends on, right under the question.
    m_supportView = new SupportView(m_body);
    body->addWidget(m_supportView);
    connect(m_supportView, &SupportView::imageOpened, this, [this]() {
        emit buttonActivated();
        m_autoHide.stop();   // looking at a figure is not idling
    });

    m_hintLabel = makeLabel("muted", m_body);
    body->addWidget(m_hintLabel);

    m_optionsBox = new QWidget(m_body);
    m_optionsBox->setObjectName(QStringLiteral("quizBody"));
    m_optionsLayout = new QVBoxLayout(m_optionsBox);
    m_optionsLayout->setContentsMargins(0, 0, 0, 0);
    m_optionsLayout->setSpacing(3);
    m_optionsBox->hide();
    body->addWidget(m_optionsBox);

    m_writtenEdit = new QPlainTextEdit(m_body);
    m_writtenEdit->setObjectName(QStringLiteral("writtenAnswerEdit"));
    m_writtenEdit->setPlaceholderText(QStringLiteral("Type your answer…"));
    m_writtenEdit->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_writtenEdit->setFocusPolicy(Qt::ClickFocus);
    m_writtenEdit->setTabChangesFocus(true);
    m_writtenEdit->setFixedHeight(68);
    m_writtenEdit->installEventFilter(this);
    m_writtenEdit->viewport()->installEventFilter(this);
    m_writtenEdit->hide();
    body->addWidget(m_writtenEdit);

    m_suggestionRow = new QWidget(m_body);
    m_suggestionRow->setObjectName(QStringLiteral("quizBody"));
    auto* suggestionLayout = new QVBoxLayout(m_suggestionRow);
    suggestionLayout->setContentsMargins(0, 0, 0, 0);
    suggestionLayout->setSpacing(5);
    m_suggestionLabel = new QLabel(m_suggestionRow);
    m_suggestionLabel->setObjectName(QStringLiteral("sectionTitle"));
    m_suggestionLabel->setWordWrap(true);
    suggestionLayout->addWidget(m_suggestionLabel);
    auto* suggestionButtons = new QHBoxLayout();
    suggestionButtons->setSpacing(8);
    suggestionButtons->addStretch(1);
    m_keepAnswerBtn = makeButton(QStringLiteral("Keep mine"), m_suggestionRow);
    m_useSuggestionBtn = makeButton(QStringLiteral("Use suggestion"), m_suggestionRow, "primary");
    suggestionButtons->addWidget(m_keepAnswerBtn);
    suggestionButtons->addWidget(m_useSuggestionBtn);
    suggestionLayout->addLayout(suggestionButtons);
    m_suggestionRow->hide();
    body->addWidget(m_suggestionRow);

    // Notice text (no question): plain, outside the result card.
    m_noticeLabel = makeLabel("sectionSummary", m_body);
    body->addWidget(m_noticeLabel);

    // Result card: the verdict with its icon, then everything that explains it, in one tinted block.
    m_resultCard = new QFrame(m_body);
    m_resultCard->setObjectName(QStringLiteral("resultCard"));
    auto* card = new QVBoxLayout(m_resultCard);
    card->setContentsMargins(10, 8, 10, 9);
    card->setSpacing(5);
    auto* banner = new QHBoxLayout();
    banner->setContentsMargins(0, 0, 0, 0);
    banner->setSpacing(7);
    m_verdictIcon = new QLabel(m_resultCard);
    m_verdictIcon->setObjectName(QStringLiteral("verdictIcon"));
    m_verdictIcon->setFixedSize(18, 18);
    m_verdictIcon->setAlignment(Qt::AlignCenter);
    banner->addWidget(m_verdictIcon, 0, Qt::AlignVCenter);
    m_resultLabel = makeLabel("quizResult", m_resultCard);
    banner->addWidget(m_resultLabel, 1, Qt::AlignVCenter);
    card->addLayout(banner);
    m_verdictDetail = makeLabel("sectionSummary", m_resultCard);
    card->addWidget(m_verdictDetail);
    m_feedbackLabel = makeLabel("sectionSummary", m_resultCard);
    card->addWidget(m_feedbackLabel);
    m_expectedLabel = makeLabel("muted", m_resultCard);
    m_expectedLabel->setTextFormat(Qt::PlainText);
    card->addWidget(m_expectedLabel);
    m_explanationLabel = makeLabel("muted", m_resultCard);
    card->addWidget(m_explanationLabel);
    m_resultCard->hide();
    body->addSpacing(2);   // with the body spacing: a clear break between the answers and the result
    body->addWidget(m_resultCard);
    m_sourceLabel = makeLabel("muted", m_body);
    body->addWidget(m_sourceLabel);
    body->addStretch(1);   // embedded: keep the question at the top instead of spreading it out

    m_scroll->setWidget(m_body);
    root->addWidget(m_scroll);

    // Action rows
    m_actionRow = new QWidget(this);
    m_actionRow->setObjectName(QStringLiteral("quizBody"));
    auto* actionRows = new QVBoxLayout(m_actionRow);
    actionRows->setContentsMargins(0, 0, 0, 0);
    actionRows->setSpacing(6);
    m_actionTop = new QHBoxLayout();
    m_actionTop->setSpacing(8);
    m_actionBottom = new QHBoxLayout();
    m_actionBottom->setSpacing(8);
    actionRows->addLayout(m_actionTop);
    actionRows->addLayout(m_actionBottom);
    QHBoxLayout* actions = m_actionTop;
    m_laterBtn = makeButton(QStringLiteral("Later"), m_actionRow, "subtle");
    m_laterBtn->setToolTip(QStringLiteral("Ask me again in a few minutes"));
    m_skipBtn = makeButton(QStringLiteral("Skip"), m_actionRow);
    m_revealBtn = makeButton(QStringLiteral("Reveal Answer"), m_actionRow);
    m_submitBtn = makeButton(QStringLiteral("Submit"), m_actionRow, "primary");
    actions->addWidget(m_laterBtn);
    actions->addStretch(1);
    actions->addWidget(m_skipBtn);
    actions->addWidget(m_revealBtn);
    actions->addWidget(m_submitBtn);
    m_laterBtn->hide();
    m_revealBtn->hide();
    root->addWidget(m_actionRow);

    m_selfRow = new QWidget(this);
    m_selfRow->setObjectName(QStringLiteral("quizBody"));
    auto* self = new QHBoxLayout(m_selfRow);
    self->setContentsMargins(0, 0, 0, 0);
    self->setSpacing(6);
    m_needReviewBtn = makeButton(QStringLiteral("Need Review"), m_selfRow);
    m_partlyBtn = makeButton(QStringLiteral("Partially"), m_selfRow);
    m_gotItBtn = makeButton(QStringLiteral("Got It"), m_selfRow, "primary");
    m_partlyBtn->setToolTip(QStringLiteral("Partially got it"));
    self->addWidget(m_needReviewBtn, 1);
    self->addWidget(m_partlyBtn, 1);
    self->addWidget(m_gotItBtn, 1);
    m_selfRow->hide();
    root->addWidget(m_selfRow);

    if (!m_embedded) {
        m_opacity = new QGraphicsOpacityEffect(this);
        m_opacity->setOpacity(0.0);
        setGraphicsEffect(m_opacity);

        m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
        connect(m_fade, &QPropertyAnimation::finished, this, &QuizPopup::finishFadeOut);

        m_slide = new QPropertyAnimation(this, "pos", this);
        m_slide->setDuration(kFadeInMs);
        m_slide->setEasingCurve(QEasingCurve::OutCubic);
    }

    m_autoHide.setSingleShot(true);
    connect(&m_autoHide, &QTimer::timeout, this, &QuizPopup::dismiss);

    connect(m_submitBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        if (m_phase == Phase::Notice && !m_submitBtn->text().isEmpty() && m_submitBtn->property("noticeAction").toBool()) {
            emit noticeActionTriggered();
            dismiss();
        } else if (m_showingResult || m_phase == Phase::Notice) {
            dismiss();
        } else {
            submit();
        }
    });
    connect(m_skipBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        closeAsSkip();
    });
    connect(m_laterBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit postponeRequested();
        dismiss();
    });
    connect(m_revealBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit revealRequested();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        closeAsSkip();
    });
    connect(m_useSuggestionBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        m_writtenEdit->setPlainText(m_suggestion);
        emit writtenSubmitted(m_suggestion, false);
    });
    connect(m_keepAnswerBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit writtenSubmitted(m_originalWritten, false);
    });
    const QList<QPair<QPushButton*, SelfRating>> ratings = {
        {m_needReviewBtn, SelfRating::NeedReview}, {m_partlyBtn, SelfRating::PartlyGotIt}, {m_gotItBtn, SelfRating::GotIt}};
    for (const auto& pair : ratings) {
        const SelfRating rating = pair.second;
        connect(pair.first, &QPushButton::clicked, this, [this, rating]() {
            emit buttonActivated();
            emit selfAssessed(static_cast<int>(rating));
        });
    }
    connect(m_writtenEdit, &QPlainTextEdit::textChanged, this, &QuizPopup::updateSubmitEnabled);

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

QPushButton* QuizPopup::selfAssessButton(SelfRating rating) const
{
    switch (rating) {
    case SelfRating::GotIt:
        return m_gotItBtn;
    case SelfRating::PartlyGotIt:
        return m_partlyBtn;
    case SelfRating::NeedReview:
        return m_needReviewBtn;
    }
    return nullptr;
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

int QuizPopup::heightCap(const QRect& available)
{
    if (!available.isValid()) {
        return kMaxHeight;
    }
    return std::max(200, std::min(kMaxHeight, available.height() * 6 / 10));
}

void QuizPopup::clearOptions()
{
    for (PixelOptionButton* option : std::as_const(m_options)) {
        m_optionsLayout->removeWidget(option);
        option->hide();   // never painted again, even before the deferred delete runs
        option->deleteLater();
    }
    m_options.clear();
}

void QuizPopup::showQuestion(const Question& question, const QRect& petGlobalGeometry)
{
    m_fadingOut = false;
    m_showingResult = false;
    m_hovered = false;
    m_autoHide.stop();
    m_question = question;
    m_suggestion.clear();
    m_originalWritten.clear();
    m_phase = Phase::Asking;

    if (!m_embedded) {
        setFixedWidth(kStudyWidth);
    }
    m_categoryLabel->setText(question.category.isEmpty() ? QStringLiteral("Study")
                                                         : QStringLiteral("Study · %1").arg(question.category));
    m_promptLabel->setText(question.prompt);
    m_supportView->setSupport(question.support,
                              question.sources.isEmpty() ? QString() : question.sources.first().location);
    m_hintLabel->setText(formatHint(question.type));
    m_hintLabel->setVisible(true);

    clearOptions();
    const bool choice = question.type == QuestionType::SingleChoice || question.type == QuestionType::MultiSelect;
    if (choice) {
        const auto indicator = question.type == QuestionType::SingleChoice ? PixelOptionButton::Indicator::Radio
                                                                           : PixelOptionButton::Indicator::Check;
        for (const QuestionOption& option : question.options) {
            auto* button = new PixelOptionButton(option.text, indicator, m_optionsBox);
            button->setProperty("optionId", option.id);
            connect(button, &QAbstractButton::toggled, this, &QuizPopup::updateSubmitEnabled);
            connect(button, &QAbstractButton::clicked, this, [this]() {
                if (!isActiveWindow()) {
                    activateWindow();
                }
            });
            m_optionsLayout->addWidget(button);
            m_options.append(button);
        }
    }
    m_optionsBox->setVisible(choice);

    m_writtenEdit->clear();
    m_writtenEdit->setReadOnly(false);
    m_writtenEdit->setEnabled(true);
    m_writtenEdit->setVisible(question.type == QuestionType::Written);
    m_suggestionRow->hide();
    hideResult();

    m_actionRow->show();
    m_selfRow->hide();
    m_skipBtn->show();
    m_laterBtn->setVisible(!m_embedded);
    m_revealBtn->setVisible(question.type == QuestionType::Written);
    m_revealBtn->setEnabled(true);
    placeActionButtons(question.type == QuestionType::Written);
    m_submitBtn->setProperty("noticeAction", false);
    switch (question.type) {
    case QuestionType::SingleChoice:
    case QuestionType::MultiSelect:
        m_submitBtn->setText(QStringLiteral("Check Answer"));
        break;
    case QuestionType::Written:
        m_submitBtn->setText(QStringLiteral("Check My Answer"));
        break;
    }
    updateSubmitEnabled();
    m_scroll->verticalScrollBar()->setValue(0);
    present(petGlobalGeometry);
}

void QuizPopup::showNotice(const QString& title, const QString& body, const QString& actionText,
                           const QRect& petGlobalGeometry)
{
    m_fadingOut = false;
    m_showingResult = false;
    m_hovered = false;
    m_autoHide.stop();
    m_question = Question{};
    m_phase = Phase::Notice;
    setFixedWidth(kStudyWidth);
    m_categoryLabel->setText(QStringLiteral("Study"));
    m_promptLabel->setText(title);
    m_supportView->setSupport({});
    m_hintLabel->hide();
    clearOptions();
    m_optionsBox->hide();
    m_writtenEdit->hide();
    m_suggestionRow->hide();
    hideResult();
    m_noticeLabel->setText(body);
    m_noticeLabel->setVisible(!body.isEmpty());
    m_selfRow->hide();
    m_actionRow->show();
    m_laterBtn->hide();
    m_skipBtn->hide();
    m_revealBtn->hide();
    placeActionButtons(false);
    m_submitBtn->setEnabled(true);
    m_submitBtn->setText(actionText.isEmpty() ? QStringLiteral("OK") : actionText);
    m_submitBtn->setProperty("noticeAction", !actionText.isEmpty());
    present(petGlobalGeometry);
    startAutoHide(kStudyResultDisplayMs);
}

void QuizPopup::placeActionButtons(bool twoRows)
{
    for (QPushButton* b : {m_revealBtn, m_submitBtn}) {
        m_actionTop->removeWidget(b);
        m_actionBottom->removeWidget(b);
    }
    while (QLayoutItem* item = m_actionBottom->takeAt(0)) {
        delete item;   // old stretch
    }
    if (twoRows) {
        m_actionBottom->addStretch(1);
        m_actionBottom->addWidget(m_revealBtn);
        m_actionBottom->addWidget(m_submitBtn);
    } else {
        m_actionTop->addWidget(m_revealBtn);
        m_actionTop->addWidget(m_submitBtn);
    }
}

void QuizPopup::present(const QRect& petGlobalGeometry)
{
    if (m_embedded) {
        setVisible(true);   // laid out by the hosting page; nothing to place or fade
        return;
    }
    m_lastPetRect = petGlobalGeometry;
    reposition(petGlobalGeometry);
    const QPoint finalPos = pos();
    if (isVisible() && m_opacity->opacity() > 0.99) {
        return;   // already on screen: update in place without replaying the entrance
    }
    show();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, false);
#endif
    raise();
    startFadeIn(finalPos);
}

void QuizPopup::updateSubmitEnabled()
{
    if (m_phase != Phase::Asking) {
        return;
    }
    switch (m_question.type) {
    case QuestionType::SingleChoice:
    case QuestionType::MultiSelect: {
        bool any = false;
        for (const PixelOptionButton* option : std::as_const(m_options)) {
            any = any || option->isChecked();
        }
        m_submitBtn->setEnabled(any);
        break;
    }
    case QuestionType::Written:
        m_submitBtn->setEnabled(!m_writtenEdit->toPlainText().trimmed().isEmpty());
        break;
    }
}

void QuizPopup::hideResult()
{
    for (QLabel* label : {m_noticeLabel, m_resultLabel, m_verdictDetail, m_feedbackLabel, m_expectedLabel,
                          m_explanationLabel, m_sourceLabel}) {
        label->hide();
    }
    m_resultCard->hide();
}

void QuizPopup::setVerdictStyle(const QString& verdict, PixelIcon icon, const QColor& color)
{
    m_resultCard->setProperty("verdict", verdict);
    m_resultCard->style()->unpolish(m_resultCard);
    m_resultCard->style()->polish(m_resultCard);
    m_verdictIcon->setPixmap(PixelTheme::icon(icon, color, 16).pixmap(16, 16));
    m_verdictIcon->setProperty("pixelIcon", static_cast<int>(icon));
    m_resultCard->show();
}

void QuizPopup::setResultText(const QString& text, const QColor& color, bool displayFont)
{
    m_resultLabel->setText(text);
    m_resultLabel->show();
    // Subtle feedback: the result text fades in with the same 180 ms timing as the popup.
    const QString font = PixelTheme::fontCss(PixelTheme::kVerdictPx, QFont::Bold,
                                             displayFont ? PixelTheme::displayFontFamily() : QString());
    auto* reveal = new QVariantAnimation(this);
    reveal->setDuration(reducedMotion() ? 0 : kFadeInMs);
    reveal->setStartValue(0);
    reveal->setEndValue(255);
    reveal->setEasingCurve(QEasingCurve::OutCubic);
    const auto applyAlpha = [this, color, font](int alpha) {
        QColor c = color;
        c.setAlpha(std::clamp(alpha, 0, 255));
        m_resultLabel->setStyleSheet(QStringLiteral(
            "QLabel#quizResult { color: rgba(%1, %2, %3, %4); %5 background: transparent; }")
                                         .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
                                         .arg(font));
    };
    applyAlpha(0);
    connect(reveal, &QVariantAnimation::valueChanged, this,
            [applyAlpha](const QVariant& v) { applyAlpha(v.toInt()); });
    reveal->start(QAbstractAnimation::DeleteWhenStopped);
}

void QuizPopup::setSourceText()
{
    if (m_question.sources.isEmpty() || m_question.sources.first().title.isEmpty()) {
        m_sourceLabel->hide();
        return;
    }
    const SourceRef& source = m_question.sources.first();
    QString text = m_question.origin == QuestionOrigin::Written
        ? QStringLiteral("Written by you")
        : source.location.isEmpty() ? QStringLiteral("Source: %1").arg(source.title)
                                    : QStringLiteral("Source: %1 · %2").arg(source.title, source.location);
    if (m_question.origin == QuestionOrigin::Edited) {
        text += QStringLiteral(" · edited by you");
    }
    m_sourceLabel->setText(text);
    m_sourceLabel->setToolTip(source.excerpt);
    m_sourceLabel->show();
}

void QuizPopup::startAutoHide(int ms)
{
    m_remainingMs = ms;
    m_remainingClock.start();
    if (!m_hovered) {
        m_autoHide.start(m_remainingMs);
    }
}

void QuizPopup::showEvaluation(const Evaluation& evaluation)
{
    m_showingResult = true;
    m_phase = Phase::Result;
    m_suggestionRow->hide();
    m_hintLabel->hide();
    m_writtenEdit->setReadOnly(true);
    m_skipBtn->hide();
    m_laterBtn->hide();
    m_revealBtn->hide();
    m_selfRow->hide();
    m_actionRow->show();
    placeActionButtons(false);
    m_submitBtn->setEnabled(true);
    m_submitBtn->setText(m_embedded ? m_advanceText : QStringLiteral("Done"));

    const QString headline = evaluation.kind == AttemptKind::SelfAssessed ? StudyEval::selfRatingLabel(evaluation.selfRating)
                                                                          : StudyEval::verdictLabel(evaluation.verdict);
    setResultText(headline, verdictColor(evaluation.verdict), true);
    switch (evaluation.verdict) {
    case Verdict::Correct:
        setVerdictStyle(QStringLiteral("correct"), PixelIcon::Check, verdictColor(evaluation.verdict));
        break;
    case Verdict::Partial:
        setVerdictStyle(QStringLiteral("partial"), PixelIcon::Partial, verdictColor(evaluation.verdict));
        break;
    case Verdict::Incorrect:
        setVerdictStyle(QStringLiteral("incorrect"), PixelIcon::Close, verdictColor(evaluation.verdict));
        break;
    case Verdict::NeedsReview:
        setVerdictStyle(QString(), PixelIcon::Help, verdictColor(evaluation.verdict));
        break;
    }

    // Mark every option: correct ones get a check, wrong picks a cross, missed correct ones a hollow check.
    const bool multi = m_question.type == QuestionType::MultiSelect;
    if (multi && evaluation.kind != AttemptKind::SelfAssessed) {
        int correct = 0;
        int picked = 0;
        for (const OptionResult& r : evaluation.options) {
            correct += r.correct ? 1 : 0;
            picked += r.correct && r.selected ? 1 : 0;
        }
        m_verdictDetail->setText(QStringLiteral("%1 of %2 correct answers picked").arg(picked).arg(correct));
        m_verdictDetail->setVisible(correct > 0);
    }
    for (PixelOptionButton* button : std::as_const(m_options)) {
        const QString id = button->property("optionId").toString();
        for (const OptionResult& r : evaluation.options) {
            if (r.id != id) {
                continue;
            }
            PixelOptionButton::Mark mark = PixelOptionButton::Mark::None;
            if (r.correct && r.selected) {
                mark = PixelOptionButton::Mark::Correct;
            } else if (r.correct) {
                mark = multi ? PixelOptionButton::Mark::Missed : PixelOptionButton::Mark::Correct;
            } else if (r.selected) {
                mark = PixelOptionButton::Mark::Incorrect;
            }
            button->setMark(mark);
            // Multi-select explains every option; single choice explains the picked and the right one.
            if (multi || r.selected || r.correct) {
                button->setDetail(r.explanation);
            }
        }
        button->setEnabled(false);
    }

    QString feedback = evaluation.feedback;
    if (!evaluation.missingPoints.isEmpty() && evaluation.verdict != Verdict::Correct) {
        feedback += (feedback.isEmpty() ? QString() : QStringLiteral("\n"))
            + QStringLiteral("Missing: %1").arg(evaluation.missingPoints.join(QStringLiteral("; ")));
    }
    m_feedbackLabel->setText(feedback);
    // Single choice already explains the picked and the right option inline.
    m_feedbackLabel->setVisible(!feedback.isEmpty() && evaluation.kind != AttemptKind::SelfAssessed
                                && m_question.type != QuestionType::SingleChoice);

    if (m_question.type == QuestionType::Written && evaluation.kind == AttemptKind::Auto
        && evaluation.verdict != Verdict::Correct) {
        m_expectedLabel->setText(QStringLiteral("Expected answer: %1").arg(m_question.expectedAnswer));
        m_expectedLabel->show();
    }
    const bool showExplanation = !m_question.explanation.isEmpty() && m_question.explanation != evaluation.feedback;
    m_explanationLabel->setText(m_question.explanation);
    m_explanationLabel->setVisible(showExplanation);
    setSourceText();

    reposition(m_lastPetRect);
    m_scroll->ensureWidgetVisible(m_resultCard, 0, 0);   // the verdict is never below the fold
    update();
    if (!m_embedded) {
        startAutoHide(kStudyResultDisplayMs);
    }
}

void QuizPopup::showSuggestion(const QString& suggestion)
{
    m_phase = Phase::Suggesting;
    m_suggestion = suggestion;
    m_originalWritten = m_writtenEdit->toPlainText();
    const QString shown = suggestion.size() > 160 ? suggestion.left(157) + QStringLiteral("…") : suggestion;
    m_suggestionLabel->setText(QStringLiteral("Did you mean “%1”?").arg(shown));
    m_suggestionRow->show();
    m_submitBtn->setEnabled(false);
    reposition(m_lastPetRect);
    m_scroll->ensureWidgetVisible(m_suggestionRow);
}

void QuizPopup::showJudging()
{
    m_phase = Phase::Judging;
    m_suggestionRow->hide();
    m_writtenEdit->setReadOnly(true);
    m_submitBtn->setEnabled(false);
    m_submitBtn->setText(QStringLiteral("Checking…"));
    m_skipBtn->show();
    reposition(m_lastPetRect);
}

void QuizPopup::showReveal()
{
    m_phase = Phase::Revealed;
    m_autoHide.stop();
    m_suggestionRow->hide();
    m_hintLabel->hide();
    m_writtenEdit->setReadOnly(true);
    m_writtenEdit->setVisible(!m_writtenEdit->toPlainText().trimmed().isEmpty());
    QString expected = QStringLiteral("Expected answer: %1").arg(m_question.expectedAnswer);
    if (!m_question.keyConcepts.isEmpty()) {
        expected += QStringLiteral("\nKey points:");
        for (const QString& k : m_question.keyConcepts) {
            expected += QStringLiteral("\n• %1").arg(k);
        }
    }
    m_expectedLabel->setText(expected);
    m_expectedLabel->show();
    m_explanationLabel->setText(m_question.explanation);
    m_explanationLabel->setVisible(!m_question.explanation.isEmpty());
    setSourceText();
    m_resultLabel->setText(QStringLiteral("How did you do?"));
    m_resultLabel->setStyleSheet(QStringLiteral("QLabel#quizResult { color: #F3F6FC; %1 }")
                                     .arg(PixelTheme::fontCss(PixelTheme::kEmphasisPx, QFont::Bold)));
    m_resultLabel->show();
    setVerdictStyle(QString(), PixelIcon::Help, PixelTheme::kSecondaryText);
    m_actionRow->hide();
    m_selfRow->show();
    reposition(m_lastPetRect);
}

void QuizPopup::followPet(const QRect& petGlobalGeometry)
{
    m_lastPetRect = petGlobalGeometry;
    if (isVisible() && !m_fadingOut && m_slide->state() != QAbstractAnimation::Running) {
        reposition(petGlobalGeometry);
    }
}

void QuizPopup::fitToContent(const QRect& available)
{
    const QMargins m = bubbleMargins();
    layout()->setContentsMargins(m);
    const int bodyWidth = width() - m.left() - m.right();
    m_body->setFixedWidth(bodyWidth);
    m_body->layout()->invalidate();   // a reused bubble must not keep the last question's height
    m_body->layout()->activate();
    int bodyHeight = m_body->layout()->heightForWidth(bodyWidth);
    if (bodyHeight <= 0) {
        bodyHeight = m_body->sizeHint().height();
    }
    m_scroll->setFixedHeight(bodyHeight);
    layout()->activate();
    int total = layout()->sizeHint().height();
    const int cap = heightCap(available);
    if (total > cap) {
        // Too tall: keep header and buttons, scroll the body (narrowed for the scrollbar).
        const int chrome = total - bodyHeight;
        const int scrollbar = 8;
        m_body->setFixedWidth(bodyWidth - scrollbar);
        m_body->layout()->activate();
        bodyHeight = std::max(m_body->layout()->heightForWidth(bodyWidth - scrollbar), m_body->sizeHint().height());
        m_body->setFixedHeight(bodyHeight);
        m_scroll->setFixedHeight(std::max(60, cap - chrome));
        layout()->activate();
        total = layout()->sizeHint().height();
    } else {
        m_body->setFixedHeight(bodyHeight);
    }
    resize(width(), total);
}

void QuizPopup::reposition(const QRect& petGlobalGeometry)
{
    if (m_embedded) {
        return;   // the hosting layout sizes embedded questions
    }
    QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    // Margins depend on which side carries the tail, so size once, place, then resize for that side.
    m_body->setMinimumHeight(0);
    m_body->setMaximumHeight(QWIDGETSIZE_MAX);
    fitToContent(avail);
    m_placement = calculateLayout(petGlobalGeometry, size(), avail).placement;
    m_body->setMinimumHeight(0);
    m_body->setMaximumHeight(QWIDGETSIZE_MAX);
    fitToContent(avail);
    move(calculateLayout(petGlobalGeometry, size(), avail).pos);
    update();
    // New content measured while the bubble is on screen can report a stale height until its labels
    // are laid out; size it once more after that, so a reused bubble never keeps extra space.
    if (!m_refitting && !m_refitPending) {
        m_refitPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_refitPending = false;
            if (isVisible() && !m_fadingOut) {
                m_refitting = true;
                if (m_slide) {
                    m_slide->stop();   // the fade-in carries on; only the geometry is corrected
                }
                reposition(m_lastPetRect);
                m_refitting = false;
            }
        });
    }
}

void QuizPopup::dismiss()
{
    if (m_embedded) {
        // Embedded: no fade. closed() tells the host to move on to the next question.
        if (m_phase == Phase::Hidden) {
            return;
        }
        m_showingResult = false;
        m_phase = Phase::Hidden;
        if (m_writtenEdit->hasFocus()) {
            m_writtenEdit->clearFocus();
        }
        emit closed();
        return;
    }
    if (!isVisible() || m_fadingOut) {
        return;
    }
    startFadeOut();
}

int QuizPopup::remainingDismissMs() const
{
    if (m_embedded || !isVisible() || m_fadingOut || !(m_showingResult || m_phase == Phase::Notice)) {
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
    if (m_phase != Phase::Asking) {
        return;
    }
    if (m_question.type == QuestionType::Written && !m_writtenEdit->isReadOnly()) {
        activateWindow();
        raise();
        m_writtenEdit->setFocus(Qt::MouseFocusReason);
    }
}

void QuizPopup::submit()
{
    if (m_showingResult || m_phase != Phase::Asking) {
        return;
    }
    switch (m_question.type) {
    case QuestionType::SingleChoice:
        for (const PixelOptionButton* option : std::as_const(m_options)) {
            if (option->isChecked()) {
                emit choiceSubmitted(option->property("optionId").toString());
                return;
            }
        }
        return;
    case QuestionType::MultiSelect: {
        QStringList ids;
        for (const PixelOptionButton* option : std::as_const(m_options)) {
            if (option->isChecked()) {
                ids.append(option->property("optionId").toString());
            }
        }
        if (!ids.isEmpty()) {
            emit selectionSubmitted(ids);
        }
        return;
    }
    case QuestionType::Written: {
        const QString text = m_writtenEdit->toPlainText();
        if (text.trimmed().isEmpty()) {
            engage();
            return;
        }
        m_originalWritten = text;
        emit writtenSubmitted(text, true);
        return;
    }
    }
}

void QuizPopup::closeAsSkip()
{
    // Closing an unanswered question is a skip (no penalty); closing a result just dismisses it.
    if (!m_showingResult && m_phase != Phase::Notice) {
        emit skipRequested();
    }
    dismiss();
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
    m_phase = Phase::Hidden;
    if (m_writtenEdit->hasFocus()) {
        m_writtenEdit->clearFocus();
    }
    hide();
    emit closed();
}

void QuizPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_embedded) {
        return;   // the hosting window paints the glass
    }
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
        closeAsSkip();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool QuizPopup::eventFilter(QObject* watched, QEvent* event)
{
    const bool written = watched == m_writtenEdit || watched == m_writtenEdit->viewport();
    if (written) {
        if (event->type() == QEvent::MouseButtonPress) {
            engage();
        } else if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                keyPressEvent(key);
                return true;
            }
            if (written && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && (key->modifiers() & Qt::ControlModifier)) {
                submit();
                return true;
            }
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
        if (isVisible() && !m_fadingOut && (m_showingResult || m_phase == Phase::Notice)) {
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
