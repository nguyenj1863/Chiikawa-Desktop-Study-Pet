#include "SettingsPanel.h"
#include "AppSettings.h"
#include "StudyTimer.h"
#include "StartupManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>

namespace {

class PastelCheckBox final : public QCheckBox {
public:
    explicit PastelCheckBox(const QString& text, QWidget* parent = nullptr)
        : QCheckBox(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFocusPolicy(Qt::StrongFocus);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const int w = 6 + kBoxSize + kSpacing + fm.horizontalAdvance(text()) + 10;
        const int h = std::max(26, fm.height() + 8);
        return {w, h};
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        update();
        QCheckBox::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        update();
        QCheckBox::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect();
        if (m_hovered && isEnabled()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xF9, 0xEC, 0xE2));
            p.drawRoundedRect(r.adjusted(0.5, 1.0, -0.5, -1.0), 7.0, 7.0);
        }

        const qreal boxY = (r.height() - kBoxSize) / 2.0;
        const QRectF boxRect(5.0, boxY, kBoxSize, kBoxSize);

        if (isChecked()) {
            QColor fill = isEnabled()
                ? (isDown() ? QColor(0xDE, 0x87, 0x7A)
                            : (m_hovered ? QColor(0xEA, 0x97, 0x8B) : QColor(0xF2, 0xA6, 0x9B)))
                : QColor(0xE0, 0xCE, 0xC4);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(boxRect, 5.0, 5.0);

            QPen checkPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(checkPen);
            QPainterPath check;
            check.moveTo(boxRect.left() + 3.8, boxRect.center().y() + 0.3);
            check.lineTo(boxRect.left() + 6.8, boxRect.bottom() - 4.0);
            check.lineTo(boxRect.right() - 3.5, boxRect.top() + 4.2);
            p.drawPath(check);
        } else {
            const QColor fill = m_hovered && isEnabled() ? QColor(0xFF, 0xF3, 0xEB) : QColor(0xFF, 0xFD, 0xFA);
            const QColor border = m_hovered && isEnabled() ? QColor(0xF2, 0xA6, 0x9B) : QColor(0xD8, 0xC2, 0xB4);
            p.setBrush(fill);
            p.setPen(QPen(border, 1.5));
            p.drawRoundedRect(boxRect.adjusted(0.75, 0.75, -0.75, -0.75), 5.0, 5.0);
        }

        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0xF2, 0xA6, 0x9B, 180), 1.2, Qt::DotLine));
            p.drawRoundedRect(boxRect.adjusted(-2.0, -2.0, 2.0, 2.0), 6.5, 6.5);
        }

        const QRectF textRect(boxRect.right() + kSpacing, 0.0,
                              r.width() - boxRect.right() - kSpacing - 4.0, r.height());
        p.setPen(isEnabled() ? QColor(0x60, 0x4C, 0x43) : QColor(0xB2, 0x9F, 0x94));
        p.setFont(font());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    static constexpr int kBoxSize = 16;
    static constexpr int kSpacing = 9;
    bool m_hovered = false;
};

void configureClickableButton(QPushButton* button)
{
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

} // namespace

SettingsPanel::SettingsPanel(AppSettings* settings, StudyTimer* timer, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_settings(settings)
    , m_timer(timer)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setMinimumWidth(316);
    setMaximumWidth(356);
    setWindowTitle(QStringLiteral("Study Pet Settings"));

    buildUi();
    applyStyle();
    syncFromSettings();
    syncFromTimer();
    updateLayoutSize();

    if (m_timer) {
        connect(m_timer, &StudyTimer::stateChanged, this, [this](StudyTimer::State) {
            refreshTimerButtons();
        });
    }
}

void SettingsPanel::buildUi()
{
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(20, 18, 20, 18);
    shell->setSpacing(10);

    // --- Header ---
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("header"));
    auto* titleRow = new QHBoxLayout(m_headerWidget);
    titleRow->setContentsMargins(0, 0, 0, 2);
    titleRow->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Settings"), m_headerWidget);
    title->setObjectName(QStringLiteral("title"));
    auto* quitTopBtn = new QPushButton(QStringLiteral("Quit"), m_headerWidget);
    quitTopBtn->setObjectName(QStringLiteral("danger"));
    quitTopBtn->setToolTip(QStringLiteral("Stop Chiikawa and exit the app"));
    quitTopBtn->setCursor(Qt::PointingHandCursor);
    quitTopBtn->setFixedWidth(64);
    connect(quitTopBtn, &QPushButton::clicked, this, &SettingsPanel::exitApplicationRequested);

    titleRow->addWidget(title, 1);
    titleRow->addWidget(quitTopBtn);
    shell->addWidget(m_headerWidget);

    // --- Scrollable body (scrollbar shown only when screen height is genuinely too small) ---
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("bodyScroll"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->viewport()->setAutoFillBackground(false);

    m_contentWidget = new QWidget(m_scrollArea);
    m_contentWidget->setObjectName(QStringLiteral("scrollContent"));
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(m_contentWidget);
    root->setContentsMargins(0, 2, 0, 2);
    root->setSpacing(10);

    // --- Study Timer ---
    auto* timerHeader = new QLabel(QStringLiteral("Study Timer"), m_contentWidget);
    timerHeader->setObjectName(QStringLiteral("section"));
    root->addWidget(timerHeader);

    auto* timeRow = new QHBoxLayout();
    timeRow->setSpacing(8);
    m_hours = new QSpinBox(m_contentWidget);
    m_hours->setRange(0, 23);
    m_hours->setSuffix(QStringLiteral(" h"));
    m_hours->setAlignment(Qt::AlignCenter);
    m_hours->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_minutes = new QSpinBox(m_contentWidget);
    m_minutes->setRange(0, 59);
    m_minutes->setSuffix(QStringLiteral(" m"));
    m_minutes->setAlignment(Qt::AlignCenter);
    m_minutes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_seconds = new QSpinBox(m_contentWidget);
    m_seconds->setRange(0, 59);
    m_seconds->setSuffix(QStringLiteral(" s"));
    m_seconds->setAlignment(Qt::AlignCenter);
    m_seconds->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(m_hours, &QSpinBox::valueChanged, this, &SettingsPanel::updatePresetSelection);
    connect(m_minutes, &QSpinBox::valueChanged, this, &SettingsPanel::updatePresetSelection);
    connect(m_seconds, &QSpinBox::valueChanged, this, &SettingsPanel::updatePresetSelection);

    timeRow->addWidget(m_hours);
    timeRow->addWidget(m_minutes);
    timeRow->addWidget(m_seconds);
    root->addLayout(timeRow);

    auto* presets = new QHBoxLayout();
    presets->setSpacing(8);
    m_preset25 = new QPushButton(QStringLiteral("25 min"), m_contentWidget);
    m_preset45 = new QPushButton(QStringLiteral("45 min"), m_contentWidget);
    m_preset60 = new QPushButton(QStringLiteral("60 min"), m_contentWidget);
    for (QPushButton* btn : {m_preset25, m_preset45, m_preset60}) {
        btn->setObjectName(QStringLiteral("preset"));
        btn->setProperty("active", false);
        configureClickableButton(btn);
    }
    connect(m_preset25, &QPushButton::clicked, this, &SettingsPanel::onPreset25);
    connect(m_preset45, &QPushButton::clicked, this, &SettingsPanel::onPreset45);
    connect(m_preset60, &QPushButton::clicked, this, &SettingsPanel::onPreset60);
    presets->addWidget(m_preset25);
    presets->addWidget(m_preset45);
    presets->addWidget(m_preset60);
    root->addLayout(presets);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(6);
    m_startBtn = new QPushButton(QStringLiteral("Start"), m_contentWidget);
    m_startBtn->setObjectName(QStringLiteral("primary"));
    m_pauseBtn = new QPushButton(QStringLiteral("Pause"), m_contentWidget);
    m_resetBtn = new QPushButton(QStringLiteral("Reset"), m_contentWidget);
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), m_contentWidget);
    for (QPushButton* btn : {m_startBtn, m_pauseBtn, m_resetBtn, m_cancelBtn}) {
        configureClickableButton(btn);
    }
    connect(m_startBtn, &QPushButton::clicked, this, &SettingsPanel::onStart);
    connect(m_pauseBtn, &QPushButton::clicked, this, &SettingsPanel::onPauseResume);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsPanel::onReset);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SettingsPanel::onCancel);
    controls->addWidget(m_startBtn);
    controls->addWidget(m_pauseBtn);
    controls->addWidget(m_resetBtn);
    controls->addWidget(m_cancelBtn);
    root->addLayout(controls);

    // --- Pet Preferences ---
    auto* petHeader = new QLabel(QStringLiteral("Pet Preferences"), m_contentWidget);
    petHeader->setObjectName(QStringLiteral("section"));
    root->addWidget(petHeader);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(10);
    auto* sizeLabel = new QLabel(QStringLiteral("Size"), m_contentWidget);
    sizeLabel->setFixedWidth(44);
    m_sizeSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_sizeSlider->setRange(AppSettings::kMinPetSize, AppSettings::kMaxPetSize);
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setFocusPolicy(Qt::StrongFocus);
    m_sizeValue = new QLabel(m_contentWidget);
    m_sizeValue->setObjectName(QStringLiteral("valueBadge"));
    m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sizeValue->setFixedWidth(42);
    sizeRow->addWidget(sizeLabel);
    sizeRow->addWidget(m_sizeSlider, 1);
    sizeRow->addWidget(m_sizeValue);
    root->addLayout(sizeRow);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sizeValue->setText(QStringLiteral("%1px").arg(v));
    });

    auto* speedRow = new QHBoxLayout();
    speedRow->setSpacing(10);
    auto* speedLabel = new QLabel(QStringLiteral("Speed"), m_contentWidget);
    speedLabel->setFixedWidth(44);
    m_speedSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_speedSlider->setRange(25, 300); // 0.25x – 3.00x * 100
    m_speedSlider->setCursor(Qt::PointingHandCursor);
    m_speedSlider->setFocusPolicy(Qt::StrongFocus);
    m_speedValue = new QLabel(m_contentWidget);
    m_speedValue->setObjectName(QStringLiteral("valueBadge"));
    m_speedValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_speedValue->setFixedWidth(42);
    speedRow->addWidget(speedLabel);
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedValue);
    root->addLayout(speedRow);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_speedValue->setText(QStringLiteral("%1x").arg(v / 100.0, 0, 'f', 2));
    });

    auto* togglesBox = new QVBoxLayout();
    togglesBox->setContentsMargins(0, 2, 0, 2);
    togglesBox->setSpacing(2);
    m_moveCheck = new PastelCheckBox(QStringLiteral("Autonomous movement"), m_contentWidget);
    m_animCheck = new PastelCheckBox(QStringLiteral("Random animations"), m_contentWidget);
    m_topCheck = new PastelCheckBox(QStringLiteral("Always on top"), m_contentWidget);
    m_startupCheck = new PastelCheckBox(QStringLiteral("Launch on Windows startup"), m_contentWidget);
    m_soundCheck = new PastelCheckBox(QStringLiteral("Completion sound"), m_contentWidget);
    togglesBox->addWidget(m_moveCheck);
    togglesBox->addWidget(m_animCheck);
    togglesBox->addWidget(m_topCheck);
    togglesBox->addWidget(m_startupCheck);
    togglesBox->addWidget(m_soundCheck);
    root->addLayout(togglesBox);

    auto* applyBtn = new QPushButton(QStringLiteral("Apply Pet Settings"), m_contentWidget);
    applyBtn->setObjectName(QStringLiteral("primary"));
    configureClickableButton(applyBtn);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsPanel::onApplyPetSettings);
    root->addWidget(applyBtn);

    // --- App ---
    auto* appHeader = new QLabel(QStringLiteral("Application"), m_contentWidget);
    appHeader->setObjectName(QStringLiteral("section"));
    root->addWidget(appHeader);

    auto* closeBtn = new QPushButton(QStringLiteral("Close Settings"), m_contentWidget);
    auto* quitBtn = new QPushButton(QStringLiteral("Quit Chiikawa"), m_contentWidget);
    quitBtn->setObjectName(QStringLiteral("danger"));
    configureClickableButton(closeBtn);
    configureClickableButton(quitBtn);
    connect(closeBtn, &QPushButton::clicked, this, &SettingsPanel::onClosePanel);
    connect(quitBtn, &QPushButton::clicked, this, &SettingsPanel::exitApplicationRequested);
    root->addWidget(closeBtn);
    root->addWidget(quitBtn);

    m_scrollArea->setWidget(m_contentWidget);
    shell->addWidget(m_scrollArea, 1);
}

void SettingsPanel::applyStyle()
{
    setStyleSheet(QStringLiteral(R"(
        QWidget#header, QWidget#scrollContent, QScrollArea#bodyScroll {
            background: transparent;
        }
        QLabel {
            color: #604C43;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            background: transparent;
        }
        QLabel#title {
            color: #604C43;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#section {
            color: #927A6E;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.3px;
            margin-top: 4px;
        }
        QLabel#valueBadge {
            color: #927A6E;
            font-size: 11px;
            font-weight: 600;
        }
        QSpinBox {
            background: #FFFFFF;
            border: 1px solid #EADBD1;
            border-radius: 8px;
            padding: 4px 6px;
            color: #604C43;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            font-weight: 600;
            min-height: 22px;
            selection-background-color: #F9DED3;
            selection-color: #604C43;
        }
        QSpinBox:hover {
            border-color: #DFC3B3;
            background: #FFFCFA;
        }
        QSpinBox:focus {
            border-color: #F2A69B;
            background: #FFFFFF;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 16px;
            border: none;
            background: transparent;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background: #F9DED3;
            border-radius: 4px;
        }
        QSlider {
            background: transparent;
            min-height: 22px;
        }
        QSlider::groove:horizontal {
            height: 6px;
            background: #EFE2D8;
            border-radius: 3px;
        }
        QSlider::sub-page:horizontal {
            background: #F2A69B;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #FFF8F2;
            border: 2px solid #F2A69B;
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 9px;
        }
        QSlider::handle:horizontal:hover {
            background: #F9DED3;
            border-color: #E58C7E;
        }
        QSlider::handle:horizontal:pressed {
            background: #F2A69B;
            border-color: #DC8073;
        }
        QPushButton {
            background: #F5E9E0;
            color: #604C43;
            border: 1px solid #EADBD1;
            border-radius: 10px;
            padding: 6px 8px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
            font-weight: 600;
            min-height: 20px;
        }
        QPushButton:hover {
            background: #F9DED3;
            border-color: #E4C2B3;
        }
        QPushButton:pressed {
            background: #F2C9BC;
            border-color: #DDAFA0;
        }
        QPushButton:disabled {
            background: #F5EFEA;
            color: #B8A79D;
            border-color: #EFE5DE;
        }
        QPushButton#preset {
            background: #F7ECE4;
            color: #604C43;
            border: 1px solid #EADBD1;
            border-radius: 12px;
            padding: 5px 10px;
        }
        QPushButton#preset:hover {
            background: #F9DED3;
            border-color: #E8B8A8;
        }
        QPushButton#preset:pressed {
            background: #F2C9BC;
        }
        QPushButton#preset[active="true"] {
            background: #F2A69B;
            color: #FFFFFF;
            border-color: #E58F83;
        }
        QPushButton#preset[active="true"]:hover {
            background: #EA988C;
            border-color: #DC8073;
        }
        QPushButton#primary {
            background: #F2A69B;
            color: #FFFFFF;
            border: 1px solid #E69488;
        }
        QPushButton#primary:hover {
            background: #EB9689;
            border-color: #DD8476;
        }
        QPushButton#primary:pressed {
            background: #E08779;
        }
        QPushButton#danger {
            background: #FBE3E0;
            color: #8A3E3B;
            border: 1px solid #ECC5BF;
        }
        QPushButton#danger:hover {
            background: #F6CEC9;
            border-color: #E2B0A8;
        }
        QPushButton#danger:pressed {
            background: #EEB8B1;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 6px;
            margin: 2px 0 2px 2px;
        }
        QScrollBar::handle:vertical {
            background: #DFC9BC;
            min-height: 28px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: #F2A69B;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
            border: none;
            background: none;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    )"));
}

void SettingsPanel::syncFromSettings()
{
    if (!m_settings) {
        return;
    }
    m_sizeSlider->setValue(m_settings->petSize());
    m_sizeValue->setText(QStringLiteral("%1px").arg(m_settings->petSize()));
    m_speedSlider->setValue(static_cast<int>(m_settings->movementSpeed() * 100.0));
    m_speedValue->setText(QStringLiteral("%1x").arg(m_settings->movementSpeed(), 0, 'f', 2));
    m_moveCheck->setChecked(m_settings->movementEnabled());
    m_animCheck->setChecked(m_settings->autoAnimationEnabled());
    m_topCheck->setChecked(m_settings->alwaysOnTop());
    m_startupCheck->setChecked(m_settings->launchOnStartup());
    m_soundCheck->setChecked(m_settings->soundEnabled());
    setDurationInputs(m_settings->lastTimerSeconds());
}

void SettingsPanel::syncFromTimer()
{
    refreshTimerButtons();
}

void SettingsPanel::setDurationInputs(int totalSeconds)
{
    totalSeconds = std::max(0, totalSeconds);
    m_hours->setValue(totalSeconds / 3600);
    m_minutes->setValue((totalSeconds % 3600) / 60);
    m_seconds->setValue(totalSeconds % 60);
    updatePresetSelection();
}

int SettingsPanel::durationFromInputs() const
{
    return m_hours->value() * 3600 + m_minutes->value() * 60 + m_seconds->value();
}

void SettingsPanel::updatePresetSelection()
{
    const int secs = durationFromInputs();
    auto setActive = [](QPushButton* btn, bool active) {
        if (!btn) {
            return;
        }
        if (btn->property("active").toBool() != active) {
            btn->setProperty("active", active);
            btn->style()->unpolish(btn);
            btn->style()->polish(btn);
            btn->update();
        }
    };
    setActive(m_preset25, secs == 25 * 60);
    setActive(m_preset45, secs == 45 * 60);
    setActive(m_preset60, secs == 60 * 60);
}

void SettingsPanel::updateLayoutSize(const QScreen* screen)
{
    if (!m_scrollArea || !m_contentWidget || !m_headerWidget) {
        return;
    }

    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    m_contentWidget->adjustSize();
    const QMargins margins = layout()->contentsMargins();
    const int spacing = layout()->spacing();

    const int contentW = std::max(m_contentWidget->sizeHint().width(), m_headerWidget->sizeHint().width());
    const int targetW = std::clamp(contentW + margins.left() + margins.right(), minimumWidth(), maximumWidth());

    const int headerH = m_headerWidget->sizeHint().height();
    const int contentH = m_contentWidget->sizeHint().height();
    const int neededH = margins.top() + headerH + spacing + contentH + margins.bottom() + 2;

    const int maxH = screen ? std::max(300, screen->availableGeometry().height() - 32) : 640;

    if (neededH <= maxH) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFixedSize(targetW, neededH);
    } else {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFixedSize(std::min(targetW + 8, maximumWidth()), maxH);
    }
}

void SettingsPanel::showNear(const QRect& petGlobalGeometry)
{
    syncFromSettings();
    syncFromTimer();

    QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    updateLayoutSize(screen);

    QPoint pos(petGlobalGeometry.right() + 12, petGlobalGeometry.top());
    clampToScreen(pos);
    move(pos);
    show();
    raise();
    activateWindow();
}

void SettingsPanel::clampToScreen(QPoint& pos) const
{
    QScreen* screen = QGuiApplication::screenAt(pos);
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

void SettingsPanel::onPreset25() { setDurationInputs(25 * 60); }
void SettingsPanel::onPreset45() { setDurationInputs(45 * 60); }
void SettingsPanel::onPreset60() { setDurationInputs(60 * 60); }

void SettingsPanel::onStart()
{
    if (!m_timer || !m_settings) {
        return;
    }
    const int secs = std::max(1, durationFromInputs());
    m_settings->setLastTimerSeconds(secs);
    m_settings->save();
    m_timer->configure(std::chrono::seconds{secs});
    m_timer->start();
    refreshTimerButtons();
}

void SettingsPanel::onPauseResume()
{
    if (!m_timer) {
        return;
    }
    if (m_timer->isRunning()) {
        m_timer->pause();
    } else if (m_timer->isPaused()) {
        m_timer->resume();
    }
    refreshTimerButtons();
}

void SettingsPanel::onReset()
{
    if (!m_timer) {
        return;
    }
    const int secs = std::max(1, durationFromInputs());
    m_timer->configure(std::chrono::seconds{secs});
    m_timer->reset();
    refreshTimerButtons();
}

void SettingsPanel::onCancel()
{
    if (!m_timer) {
        return;
    }
    m_timer->cancel();
    refreshTimerButtons();
}

void SettingsPanel::refreshTimerButtons()
{
    if (!m_timer) {
        return;
    }
    const bool active = m_timer->isActive();
    if (m_timer->isPaused()) {
        m_pauseBtn->setText(QStringLiteral("Resume"));
        m_pauseBtn->setEnabled(true);
    } else if (m_timer->isRunning()) {
        m_pauseBtn->setText(QStringLiteral("Pause"));
        m_pauseBtn->setEnabled(true);
    } else {
        m_pauseBtn->setText(QStringLiteral("Pause"));
        m_pauseBtn->setEnabled(false);
    }
    m_pauseBtn->setCursor(m_pauseBtn->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);

    m_cancelBtn->setEnabled(active);
    m_cancelBtn->setCursor(active ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void SettingsPanel::onApplyPetSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->setPetSize(m_sizeSlider->value());
    m_settings->setMovementSpeed(m_speedSlider->value() / 100.0);
    m_settings->setMovementEnabled(m_moveCheck->isChecked());
    m_settings->setAutoAnimationEnabled(m_animCheck->isChecked());
    m_settings->setAlwaysOnTop(m_topCheck->isChecked());
    m_settings->setSoundEnabled(m_soundCheck->isChecked());

    const bool wantStartup = m_startupCheck->isChecked();
    m_settings->setLaunchOnStartup(wantStartup);
    if (wantStartup) {
        // Allow build-tree paths for local testing; prefer deployed installs for daily use
        (void)StartupManager::enable(QCoreApplication::applicationFilePath());
    } else {
        (void)StartupManager::disable();
    }

    m_settings->save();
    emit settingsApplied();
}

void SettingsPanel::onClosePanel()
{
    hide();
    emit closedByUser();
}

void SettingsPanel::closeEvent(QCloseEvent* event)
{
    event->ignore();
    hide();
    emit closedByUser();
}

void SettingsPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
}

void SettingsPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
    QPainterPath path;
    path.addRoundedRect(card, 16.0, 16.0);

    // Subtle soft drop shadow outside the opaque card
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(96, 76, 67, 16));
    p.drawPath(path.translated(0, 3));
    p.setBrush(QColor(96, 76, 67, 22));
    p.drawPath(path.translated(0, 1.5));

    // Opaque warm cream background so desktop wallpaper never bleeds through
    p.setBrush(QColor(0xFF, 0xF8, 0xF2));
    p.setPen(QPen(QColor(0xEA, 0xDB, 0xD1), 1.2));
    p.drawPath(path);
}
