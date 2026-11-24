#include "OnboardingWindow.h"
#include "OnboardingPreview.h"
#include "PixelTheme.h"
#include "UISoundManager.h"

#include <QCloseEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

OnboardingWindow::OnboardingWindow(UISoundManager* uiSound, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_uiSound(uiSound)
{
    PixelTheme::ensureFontsLoaded();
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("Chiikawa — Welcome"));
    setStyleSheet(PixelTheme::styleSheet());

    buildUi();
    goToPage(Welcome, false);
    hide();
}

OnboardingWindow::~OnboardingWindow()
{
    stopAllPreviews();
}

void OnboardingWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 18, 26, 22);
    root->setSpacing(10);

    // Top bar: fixed-height header with compact progress indicator + close button
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("header"));
    m_headerWidget->setFixedHeight(28);
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_progressContainer = new QWidget(m_headerWidget);
    auto* progLayout = new QHBoxLayout(m_progressContainer);
    progLayout->setContentsMargins(0, 0, 0, 0);
    progLayout->setSpacing(7);

    for (int i = 0; i < kStepCount; ++i) {
        auto* dot = new QWidget(m_progressContainer);
        dot->setFixedSize(10, 10);
        m_progressDots[static_cast<std::size_t>(i)] = dot;
        progLayout->addWidget(dot, 0, Qt::AlignVCenter);
    }

    m_progressLabel = new QLabel(QStringLiteral("1 / 4"), m_progressContainer);
    m_progressLabel->setObjectName(QStringLiteral("onboardingProgress"));
    m_progressLabel->setContentsMargins(6, 0, 0, 0);
    progLayout->addWidget(m_progressLabel, 0, Qt::AlignVCenter);

    m_closeBtn = new PixelIconButton(PixelIcon::Close, 28, 12, false, m_headerWidget);
    m_closeBtn->setToolTip(QStringLiteral("Skip tutorial (Esc)"));
    m_closeBtn->setAccessibleName(QStringLiteral("Skip tutorial"));
    connect(m_closeBtn, &QPushButton::clicked, this, &OnboardingWindow::onSkipClicked);

    headerLayout->addWidget(m_progressContainer, 0, Qt::AlignVCenter);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);
    root->addWidget(m_headerWidget);

    // Stacked pages
    m_stack = new QStackedWidget(this);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_previews[Welcome] = new OnboardingPreview(OnboardingPreview::Mode::Welcome, m_stack);
    m_previews[Interact] = new OnboardingPreview(OnboardingPreview::Mode::Interact, m_stack);
    m_previews[Drag] = new OnboardingPreview(OnboardingPreview::Mode::Drag, m_stack);
    m_previews[StudyTimer] = new OnboardingPreview(OnboardingPreview::Mode::StudyTimer, m_stack);
    m_previews[Alarm] = new OnboardingPreview(OnboardingPreview::Mode::Alarm, m_stack);
    m_previews[Complete] = new OnboardingPreview(OnboardingPreview::Mode::Complete, m_stack);

    m_stack->addWidget(createPageWidget(
        Welcome,
        QStringLiteral("Welcome!"),
        QStringLiteral("Study Pet"),
        QStringLiteral("Chiikawa is ready to keep you company while you study."),
        m_previews[Welcome]));

    m_stack->addWidget(createPageWidget(
        Interact,
        QStringLiteral("Say hello!"),
        QString(),
        QStringLiteral("<b style='color:#593E35;'>Click</b> to change animations.<br>"
                       "<b style='color:#593E35;'>Double-click</b> to open settings."),
        m_previews[Interact]));

    m_stack->addWidget(createPageWidget(
        Drag,
        QStringLiteral("Take me anywhere!"),
        QString(),
        QStringLiteral("<b style='color:#593E35;'>Drag</b> Chiikawa anywhere on your desktop."),
        m_previews[Drag]));

    m_stack->addWidget(createPageWidget(
        StudyTimer,
        QStringLiteral("Time to focus!"),
        QString(),
        QStringLiteral("Set a <b style='color:#593E35;'>timer</b> and <b style='color:#593E35;'>hover</b> over Chiikawa to check your progress."),
        m_previews[StudyTimer]));

    m_stack->addWidget(createPageWidget(
        Alarm,
        QStringLiteral("Don't Forget!"),
        QString(),
        QStringLiteral("Set an <b style='color:#593E35;'>alarm</b> and <b style='color:#593E35;'>snooze</b> it if you need a little more time."),
        m_previews[Alarm]));

    m_stack->addWidget(createPageWidget(
        Complete,
        QStringLiteral("All set!"),
        QString(),
        QStringLiteral("Chiikawa is ready. Happy studying!"),
        m_previews[Complete]));

    m_stackOpacity = new QGraphicsOpacityEffect(m_stack);
    m_stackOpacity->setOpacity(1.0);
    m_stack->setGraphicsEffect(m_stackOpacity);

    m_fadeAnim = new QPropertyAnimation(m_stackOpacity, "opacity", this);
    m_fadeAnim->setDuration(kTransitionMs);
    m_fadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    root->addWidget(m_stack, 1);

    // Footer navigation bar (consistent positioning across all screens)
    auto* nav = new QHBoxLayout();
    nav->setContentsMargins(0, 6, 0, 0);
    nav->setSpacing(10);

    m_skipBtn = new QPushButton(QStringLiteral("Skip"), this);
    m_skipBtn->setObjectName(QStringLiteral("onboardingSkip"));
    m_skipBtn->setCursor(Qt::PointingHandCursor);
    m_skipBtn->setToolTip(QStringLiteral("Skip tutorial and go straight to Chiikawa"));
    connect(m_skipBtn, &QPushButton::clicked, this, &OnboardingWindow::onSkipClicked);

    m_backBtn = new QPushButton(QStringLiteral("Back"), this);
    m_backBtn->setObjectName(QStringLiteral("onboardingSecondary"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setToolTip(QStringLiteral("Return to previous step"));
    connect(m_backBtn, &QPushButton::clicked, this, &OnboardingWindow::onBackClicked);

    m_primaryBtn = new QPushButton(QStringLiteral("Get Started"), this);
    m_primaryBtn->setObjectName(QStringLiteral("onboardingPrimary"));
    m_primaryBtn->setCursor(Qt::PointingHandCursor);
    m_primaryBtn->setDefault(true);
    m_primaryBtn->setAutoDefault(true);
    connect(m_primaryBtn, &QPushButton::clicked, this, &OnboardingWindow::onPrimaryClicked);

    nav->addWidget(m_skipBtn);
    nav->addStretch(1);
    nav->addWidget(m_backBtn);
    nav->addWidget(m_primaryBtn);
    root->addLayout(nav);

    // Subtle personal signature footer on the Welcome screen only
    m_footerLabel = new QLabel(QStringLiteral("\u00A9 2026 John Nguyen \u2661"), this);
    m_footerLabel->setObjectName(QStringLiteral("onboardingFooter"));
    m_footerLabel->setAlignment(Qt::AlignCenter);
    m_footerLabel->setContentsMargins(0, 4, 0, 0);
    root->addWidget(m_footerLabel);

    // Logical tab order
    setTabOrder(m_skipBtn, m_backBtn);
    setTabOrder(m_backBtn, m_primaryBtn);
    setTabOrder(m_primaryBtn, m_closeBtn);
}

QWidget* OnboardingWindow::createPageWidget(int pageIndex,
                                            const QString& heading,
                                            const QString& appTitle,
                                            const QString& description,
                                            OnboardingPreview* preview)
{
    auto* page = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(appTitle.isEmpty() ? 6 : 2);

    const bool isHero = (pageIndex == Welcome || pageIndex == Complete);

    auto* titleLbl = new QLabel(heading, page);
    titleLbl->setObjectName(isHero ? QStringLiteral("onboardingWelcomeHeading")
                                   : QStringLiteral("onboardingHeading"));
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setWordWrap(true);
    layout->addWidget(titleLbl);

    if (!appTitle.isEmpty()) {
        auto* appTitleLbl = new QLabel(appTitle, page);
        appTitleLbl->setObjectName(QStringLiteral("onboardingAppTitle"));
        appTitleLbl->setAlignment(Qt::AlignCenter);
        appTitleLbl->setWordWrap(true);
        layout->addWidget(appTitleLbl);
        layout->addSpacing(4);
    }

    auto* bodyLbl = new QLabel(description, page);
    bodyLbl->setObjectName(QStringLiteral("onboardingBody"));
    bodyLbl->setTextFormat(Qt::RichText);
    bodyLbl->setAlignment(Qt::AlignCenter);
    bodyLbl->setWordWrap(true);

    layout->addWidget(bodyLbl);
    layout->addSpacing(8);
    layout->addWidget(preview);
    return page;
}

void OnboardingWindow::openOnboarding(bool replay)
{
    m_isReplay = replay;
    m_outcomeEmitted = false;

    int targetWidth = kDefaultWidth;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        targetWidth = std::clamp(kDefaultWidth, 340, std::max(340, avail.width() - 32));
    }
    setFixedWidth(targetWidth);

    goToPage(Welcome, false);
    adjustSize();
    centerOnPrimaryScreen();

    show();
    raise();
    activateWindow();
    updateActivePreview();
    if (m_primaryBtn) {
        m_primaryBtn->setFocus();
    }
}

int OnboardingWindow::currentPage() const
{
    return m_stack ? m_stack->currentIndex() : 0;
}

QString OnboardingWindow::progressText() const
{
    return m_progressLabel ? m_progressLabel->text() : QString();
}

OnboardingPreview* OnboardingWindow::previewForPage(int page) const
{
    if (page < 0 || page >= PageCount) {
        return nullptr;
    }
    return m_previews[static_cast<std::size_t>(page)];
}

void OnboardingWindow::goToPage(int page, bool animate)
{
    if (!m_stack) {
        return;
    }
    const int clamped = std::clamp(page, static_cast<int>(Welcome), static_cast<int>(Complete));
    m_stack->setCurrentIndex(clamped);
    updateNavigationState();
    updateActivePreview();

    if (animate && m_fadeAnim && m_stackOpacity && !OnboardingPreview::systemPrefersReducedMotion()) {
        m_fadeAnim->stop();
        m_stackOpacity->setOpacity(0.25);
        m_fadeAnim->setStartValue(0.25);
        m_fadeAnim->setEndValue(1.0);
        m_fadeAnim->start();
    } else if (m_stackOpacity) {
        m_stackOpacity->setOpacity(1.0);
    }
}

void OnboardingWindow::updateNavigationState()
{
    const int page = currentPage();
    const bool isStep = (page >= Interact && page <= Alarm);

    if (m_progressContainer) {
        m_progressContainer->setVisible(isStep);
    }
    if (isStep) {
        const int stepIndex = page - Interact; // 0..3
        if (m_progressLabel) {
            m_progressLabel->setText(QStringLiteral("%1 / %2").arg(stepIndex + 1).arg(kStepCount));
        }
        for (int i = 0; i < kStepCount; ++i) {
            QWidget* dot = m_progressDots[static_cast<std::size_t>(i)];
            if (!dot) {
                continue;
            }
            const bool active = (i == stepIndex);
            const bool done = (i < stepIndex);
            const QString bg = active ? QStringLiteral("#F3A6A0")
                                      : (done ? QStringLiteral("#F7C2B5") : QStringLiteral("#FFF0E6"));
            const QString border = active ? QStringLiteral("#DC8981") : QStringLiteral("#DDBFB3");
            dot->setStyleSheet(QStringLiteral(
                "background: %1; border: 2px solid %2; border-radius: 5px;").arg(bg, border));
        }
    }

    if (page == Welcome) {
        m_skipBtn->setVisible(true);
        m_backBtn->setVisible(false);
        m_primaryBtn->setText(QStringLiteral("Get Started"));
        m_primaryBtn->setToolTip(QStringLiteral("Start the quick visual tutorial"));
    } else if (page == Complete) {
        m_skipBtn->setVisible(false);
        m_backBtn->setVisible(true);
        m_primaryBtn->setText(QStringLiteral("Meet Chiikawa"));
        m_primaryBtn->setToolTip(QStringLiteral("Finish tutorial and reveal Chiikawa"));
    } else {
        m_skipBtn->setVisible(true);
        m_backBtn->setVisible(true);
        m_primaryBtn->setText(QStringLiteral("Next"));
        m_primaryBtn->setToolTip(QStringLiteral("Continue to next step"));
    }

    if (m_footerLabel) {
        m_footerLabel->setVisible(page == Welcome);
    }
}

void OnboardingWindow::updateActivePreview()
{
    const int activePage = currentPage();
    const bool windowShown = isVisible();
    for (int i = 0; i < PageCount; ++i) {
        if (OnboardingPreview* p = m_previews[static_cast<std::size_t>(i)]) {
            p->setActive(windowShown && (i == activePage));
        }
    }
}

void OnboardingWindow::stopAllPreviews()
{
    for (OnboardingPreview* p : m_previews) {
        if (p) {
            p->setActive(false);
        }
    }
}

void OnboardingWindow::centerOnPrimaryScreen()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        const auto all = QGuiApplication::screens();
        screen = all.isEmpty() ? nullptr : all.first();
    }
    if (!screen) {
        return;
    }
    const QRect avail = screen->availableGeometry();
    const int x = avail.center().x() - width() / 2;
    const int y = avail.center().y() - height() / 2;
    move(std::clamp(x, avail.left(), std::max(avail.left(), avail.right() - width() + 1)),
         std::clamp(y, avail.top(), std::max(avail.top(), avail.bottom() - height() + 1)));
}

void OnboardingWindow::playUiClick()
{
    if (m_uiSound) {
        m_uiSound->playClick();
    }
}

void OnboardingWindow::onPrimaryClicked()
{
    playUiClick();
    const int page = currentPage();
    if (page >= Complete) {
        finishAndClose(true);
        return;
    }
    goToPage(page + 1, true);
}

void OnboardingWindow::onBackClicked()
{
    playUiClick();
    const int page = currentPage();
    if (page > Welcome) {
        goToPage(page - 1, true);
    }
}

void OnboardingWindow::onSkipClicked()
{
    playUiClick();
    finishAndClose(false);
}

void OnboardingWindow::finishAndClose(bool completed)
{
    if (m_outcomeEmitted) {
        return;
    }
    m_outcomeEmitted = true;
    stopAllPreviews();
    hide();
    if (completed) {
        emit onboardingCompleted(m_isReplay);
    } else {
        emit onboardingSkipped(m_isReplay);
    }
}

void OnboardingWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        onSkipClicked();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onPrimaryClicked();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right && currentPage() < Complete) {
        onPrimaryClicked();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left && currentPage() > Welcome) {
        onBackClicked();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void OnboardingWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_headerWidget
        && m_headerWidget->geometry().contains(event->position().toPoint())) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void OnboardingWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void OnboardingWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void OnboardingWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    finishAndClose(false);
}

void OnboardingWindow::hideEvent(QHideEvent* event)
{
    stopAllPreviews();
    QWidget::hideEvent(event);
}

void OnboardingWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintPixelCard(p, rect(), PixelTheme::kBackground, PixelTheme::kBorder, 4);
}
