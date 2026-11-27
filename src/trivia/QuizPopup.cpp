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
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>

namespace {

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
    case QuestionType::OpenTrivia:
        break;
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
    setStyleSheet(PixelTheme::styleSheet()
                  + QStringLiteral("QScrollArea#quizScroll, QWidget#quizBody { background: transparent; border: none; }"));
    setAccessibleName(QStringLiteral("Lock-In Trivia question"));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

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
    root->addLayout(header);

    // Scrollable body: everything between the header and the buttons.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("quizScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setWidgetResizable(false);
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

    m_hintLabel = makeLabel("muted", m_body);
    body->addWidget(m_hintLabel);

    m_optionsBox = new QWidget(m_body);
    m_optionsBox->setObjectName(QStringLiteral("quizBody"));
    m_optionsLayout = new QVBoxLayout(m_optionsBox);
    m_optionsLayout->setContentsMargins(0, 0, 0, 0);
    m_optionsLayout->setSpacing(3);
    m_optionsBox->hide();
    body->addWidget(m_optionsBox);

    m_answerRow = new QWidget(m_body);
    m_answerRow->setObjectName(QStringLiteral("quizBody"));
    auto* answerLayout = new QHBoxLayout(m_answerRow);
    answerLayout->setContentsMargins(0, 0, 0, 0);
    answerLayout->setSpacing(6);
    m_answerEdit = new QLineEdit(m_answerRow);
    m_answerEdit->setObjectName(QStringLiteral("triviaAnswerEdit"));
    m_answerEdit->setPlaceholderText(QStringLiteral("Type an answer…"));
    m_answerEdit->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_answerEdit->setFocusPolicy(Qt::ClickFocus);
    m_answerEdit->setMaxLength(80);
    m_answerEdit->installEventFilter(this);
    answerLayout->addWidget(m_answerEdit, 1);
    body->addWidget(m_answerRow);

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

    m_resultLabel = makeLabel("triviaResult", m_body);
    body->addWidget(m_resultLabel);
    m_feedbackLabel = makeLabel("sectionSummary", m_body);
    body->addWidget(m_feedbackLabel);
    m_expectedLabel = makeLabel("muted", m_body);
    m_expectedLabel->setTextFormat(Qt::PlainText);
    body->addWidget(m_expectedLabel);
    m_explanationLabel = makeLabel("muted", m_body);
    body->addWidget(m_explanationLabel);
    m_sourceLabel = makeLabel("muted", m_body);
    body->addWidget(m_sourceLabel);

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
    connect(m_answerEdit, &QLineEdit::returnPressed, this, &QuizPopup::submit);
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

    const bool study = question.isDocumentQuestion();
    setFixedWidth(study ? kStudyWidth : kWidth);
    if (study) {
        m_categoryLabel->setText(question.category.isEmpty() ? QStringLiteral("Lock-In Trivia")
                                                             : QStringLiteral("Lock-In · %1").arg(question.category));
    } else {
        m_categoryLabel->setText(question.category.isEmpty() ? QStringLiteral("Trivia check-in")
                                                             : QStringLiteral("Trivia · %1").arg(question.category));
    }
    m_promptLabel->setText(question.prompt);
    m_hintLabel->setText(formatHint(question.type));
    m_hintLabel->setVisible(study);

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

    m_answerEdit->clear();
    m_answerEdit->setEnabled(true);
    m_answerRow->setVisible(question.type == QuestionType::OpenTrivia);
    m_writtenEdit->clear();
    m_writtenEdit->setReadOnly(false);
    m_writtenEdit->setEnabled(true);
    m_writtenEdit->setVisible(question.type == QuestionType::Written);
    m_suggestionRow->hide();
    for (QLabel* label : {m_resultLabel, m_feedbackLabel, m_expectedLabel, m_explanationLabel, m_sourceLabel}) {
        label->hide();
    }

    m_actionRow->show();
    m_selfRow->hide();
    m_skipBtn->show();
    m_laterBtn->setVisible(true);
    m_revealBtn->setVisible(question.type == QuestionType::Written);
    m_revealBtn->setEnabled(true);
    placeActionButtons(question.type == QuestionType::Written);
    m_submitBtn->setProperty("noticeAction", false);
    switch (question.type) {
    case QuestionType::OpenTrivia:
        m_submitBtn->setText(QStringLiteral("Submit"));
        break;
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
    m_categoryLabel->setText(QStringLiteral("Lock-In Trivia"));
    m_promptLabel->setText(title);
    m_hintLabel->hide();
    clearOptions();
    m_optionsBox->hide();
    m_answerRow->hide();
    m_writtenEdit->hide();
    m_suggestionRow->hide();
    for (QLabel* label : {m_resultLabel, m_expectedLabel, m_explanationLabel, m_sourceLabel}) {
        label->hide();
    }
    m_feedbackLabel->setText(body);
    m_feedbackLabel->setVisible(!body.isEmpty());
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
    case QuestionType::OpenTrivia:
        m_submitBtn->setEnabled(true);
        break;
    }
}

void QuizPopup::setResultText(const QString& text, const QColor& color, bool displayFont)
{
    m_resultLabel->setText(text);
    m_resultLabel->show();
    // Subtle feedback: the result text fades in with the same 180 ms timing as the popup.
    const QString family = displayFont ? PixelTheme::displayFontFamily() : QStringLiteral("Segoe UI");
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
}

void QuizPopup::setSourceText()
{
    if (m_question.sources.isEmpty() || m_question.sources.first().title.isEmpty()) {
        m_sourceLabel->hide();
        return;
    }
    const SourceRef& source = m_question.sources.first();
    m_sourceLabel->setText(source.location.isEmpty()
                               ? QStringLiteral("Source: %1").arg(source.title)
                               : QStringLiteral("Source: %1 · %2").arg(source.title, source.location));
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

void QuizPopup::showResult(const AnswerResult& result, const QString& explanation)
{
    m_showingResult = true;
    m_phase = Phase::Result;
    m_answerEdit->setEnabled(false);
    m_answerRow->hide();
    m_skipBtn->hide();
    m_laterBtn->hide();
    m_revealBtn->hide();
    m_submitBtn->setEnabled(true);
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
    setResultText(text, rarityColor(result.rarity),
                  result.rarity == Rarity::Legendary || result.rarity == Rarity::Rare);

    m_explanationLabel->setText(explanation);
    m_explanationLabel->setVisible(!explanation.isEmpty());

    reposition(m_lastPetRect);
    update();
    startAutoHide(kResultDisplayMs);
}

void QuizPopup::showEvaluation(const Evaluation& evaluation, int xp)
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
    m_submitBtn->setText(QStringLiteral("Done"));

    QString headline;
    if (evaluation.kind == AttemptKind::SelfAssessed) {
        headline = QStringLiteral("%1 · +%2 XP").arg(StudyEval::selfRatingLabel(evaluation.selfRating)).arg(xp);
    } else if (evaluation.verdict == Verdict::NeedsReview) {
        headline = StudyEval::verdictLabel(evaluation.verdict);
    } else {
        headline = QStringLiteral("%1 · +%2 XP").arg(StudyEval::verdictLabel(evaluation.verdict)).arg(xp);
    }
    setResultText(headline, verdictColor(evaluation.verdict), evaluation.verdict == Verdict::Correct);

    // Mark every option: correct ones get a check, wrong picks a cross, missed correct ones a hollow check.
    const bool multi = m_question.type == QuestionType::MultiSelect;
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
    update();
    startAutoHide(kStudyResultDisplayMs);
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
    m_resultLabel->setStyleSheet(QStringLiteral("QLabel#triviaResult { color: #F3F6FC; font-size: 12px; font-weight: 700; }"));
    m_resultLabel->show();
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
    if (!isVisible() || m_fadingOut || !(m_showingResult || m_phase == Phase::Notice)) {
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
    if (m_question.type == QuestionType::OpenTrivia && m_answerEdit->isEnabled()) {
        activateWindow();
        raise();
        m_answerEdit->setFocus(Qt::MouseFocusReason);
    } else if (m_question.type == QuestionType::Written && !m_writtenEdit->isReadOnly()) {
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
    case QuestionType::OpenTrivia: {
        const QString text = m_answerEdit->text();
        if (text.trimmed().isEmpty()) {
            engage();
            return;
        }
        emit answerSubmitted(text);
        return;
    }
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
    for (QWidget* input : {static_cast<QWidget*>(m_answerEdit), static_cast<QWidget*>(m_writtenEdit)}) {
        if (input->hasFocus()) {
            input->clearFocus();
        }
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
        closeAsSkip();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool QuizPopup::eventFilter(QObject* watched, QEvent* event)
{
    const bool written = watched == m_writtenEdit || watched == m_writtenEdit->viewport();
    if (watched == m_answerEdit || written) {
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
