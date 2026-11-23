#include "SettingsPanel.h"
#include "AppSettings.h"
#include "StudyTimer.h"
#include "StartupManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <chrono>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

SettingsPanel::SettingsPanel(AppSettings* settings, StudyTimer* timer, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_settings(settings)
    , m_timer(timer)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFixedWidth(280);
    setWindowTitle(QStringLiteral("Study Pet Settings"));

    buildUi();
    applyStyle();
    syncFromSettings();
    syncFromTimer();

    if (m_timer) {
        connect(m_timer, &StudyTimer::stateChanged, this, [this](StudyTimer::State) {
            refreshTimerButtons();
        });
    }
}

void SettingsPanel::buildUi()
{
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(18, 18, 18, 18);
    shell->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("Settings"), this);
    title->setObjectName(QStringLiteral("title"));
    auto* quitTopBtn = new QPushButton(QStringLiteral("Quit"), this);
    quitTopBtn->setObjectName(QStringLiteral("danger"));
    quitTopBtn->setToolTip(QStringLiteral("Stop Chiikawa and exit the app"));
    quitTopBtn->setFixedWidth(64);
    connect(quitTopBtn, &QPushButton::clicked, this, &SettingsPanel::exitApplicationRequested);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(quitTopBtn);
    shell->addLayout(titleRow);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    auto* content = new QWidget(scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 4, 0);
    root->setSpacing(12);

    // --- Timer ---
    auto* timerHeader = new QLabel(QStringLiteral("Study Timer"), content);
    timerHeader->setObjectName(QStringLiteral("section"));
    root->addWidget(timerHeader);

    auto* timeRow = new QHBoxLayout();
    m_hours = new QSpinBox(content);
    m_hours->setRange(0, 23);
    m_hours->setSuffix(QStringLiteral(" h"));
    m_minutes = new QSpinBox(content);
    m_minutes->setRange(0, 59);
    m_minutes->setSuffix(QStringLiteral(" m"));
    m_seconds = new QSpinBox(content);
    m_seconds->setRange(0, 59);
    m_seconds->setSuffix(QStringLiteral(" s"));
    timeRow->addWidget(m_hours);
    timeRow->addWidget(m_minutes);
    timeRow->addWidget(m_seconds);
    root->addLayout(timeRow);

    auto* presets = new QHBoxLayout();
    auto* p25 = new QPushButton(QStringLiteral("25 min"), content);
    auto* p45 = new QPushButton(QStringLiteral("45 min"), content);
    auto* p60 = new QPushButton(QStringLiteral("60 min"), content);
    connect(p25, &QPushButton::clicked, this, &SettingsPanel::onPreset25);
    connect(p45, &QPushButton::clicked, this, &SettingsPanel::onPreset45);
    connect(p60, &QPushButton::clicked, this, &SettingsPanel::onPreset60);
    presets->addWidget(p25);
    presets->addWidget(p45);
    presets->addWidget(p60);
    root->addLayout(presets);

    auto* controls = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("Start"), content);
    m_pauseBtn = new QPushButton(QStringLiteral("Pause"), content);
    m_resetBtn = new QPushButton(QStringLiteral("Reset"), content);
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), content);
    connect(m_startBtn, &QPushButton::clicked, this, &SettingsPanel::onStart);
    connect(m_pauseBtn, &QPushButton::clicked, this, &SettingsPanel::onPauseResume);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsPanel::onReset);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SettingsPanel::onCancel);
    controls->addWidget(m_startBtn);
    controls->addWidget(m_pauseBtn);
    controls->addWidget(m_resetBtn);
    controls->addWidget(m_cancelBtn);
    root->addLayout(controls);

    // --- Pet ---
    auto* petHeader = new QLabel(QStringLiteral("Pet"), content);
    petHeader->setObjectName(QStringLiteral("section"));
    root->addWidget(petHeader);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(QStringLiteral("Size"), content));
    m_sizeSlider = new QSlider(Qt::Horizontal, content);
    m_sizeSlider->setRange(AppSettings::kMinPetSize, AppSettings::kMaxPetSize);
    m_sizeValue = new QLabel(content);
    sizeRow->addWidget(m_sizeSlider, 1);
    sizeRow->addWidget(m_sizeValue);
    root->addLayout(sizeRow);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sizeValue->setText(QString::number(v));
    });

    auto* speedRow = new QHBoxLayout();
    speedRow->addWidget(new QLabel(QStringLiteral("Speed"), content));
    m_speedSlider = new QSlider(Qt::Horizontal, content);
    m_speedSlider->setRange(25, 300); // 0.25x – 3.00x * 100
    m_speedValue = new QLabel(content);
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedValue);
    root->addLayout(speedRow);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_speedValue->setText(QStringLiteral("%1x").arg(v / 100.0, 0, 'f', 2));
    });

    m_moveCheck = new QCheckBox(QStringLiteral("Autonomous movement"), content);
    m_animCheck = new QCheckBox(QStringLiteral("Random animations"), content);
    m_topCheck = new QCheckBox(QStringLiteral("Always on top"), content);
    m_startupCheck = new QCheckBox(QStringLiteral("Launch on Windows startup"), content);
    m_soundCheck = new QCheckBox(QStringLiteral("Completion sound"), content);
    root->addWidget(m_moveCheck);
    root->addWidget(m_animCheck);
    root->addWidget(m_topCheck);
    root->addWidget(m_startupCheck);
    root->addWidget(m_soundCheck);

    auto* applyBtn = new QPushButton(QStringLiteral("Apply Pet Settings"), content);
    applyBtn->setObjectName(QStringLiteral("primary"));
    connect(applyBtn, &QPushButton::clicked, this, &SettingsPanel::onApplyPetSettings);
    root->addWidget(applyBtn);

    // --- App ---
    auto* appHeader = new QLabel(QStringLiteral("Application"), content);
    appHeader->setObjectName(QStringLiteral("section"));
    root->addWidget(appHeader);

    auto* closeBtn = new QPushButton(QStringLiteral("Close Settings"), content);
    auto* quitBtn = new QPushButton(QStringLiteral("Quit Chiikawa"), content);
    quitBtn->setObjectName(QStringLiteral("danger"));
    connect(closeBtn, &QPushButton::clicked, this, &SettingsPanel::onClosePanel);
    connect(quitBtn, &QPushButton::clicked, this, &SettingsPanel::exitApplicationRequested);
    root->addWidget(closeBtn);
    root->addWidget(quitBtn);
    root->addStretch(1);

    scroll->setWidget(content);
    shell->addWidget(scroll, 1);
}

void SettingsPanel::applyStyle()
{
    setStyleSheet(QStringLiteral(R"(
        QLabel#title {
            color: #5c4a3a;
            font-size: 16px;
            font-weight: 700;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
        }
        QLabel#section {
            color: #7a6555;
            font-size: 12px;
            font-weight: 600;
            margin-top: 4px;
        }
        QLabel {
            color: #5c4a3a;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
        }
        QSpinBox, QSlider {
            background: #fff8f0;
            border: 1px solid #e8d5c4;
            border-radius: 6px;
            padding: 2px;
            color: #5c4a3a;
        }
        QCheckBox {
            color: #5c4a3a;
            font-size: 11px;
            spacing: 6px;
        }
        QPushButton {
            background: #f3e2d4;
            color: #5c4a3a;
            border: none;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 11px;
            font-weight: 600;
        }
        QPushButton:hover { background: #ebd2c0; }
        QPushButton:pressed { background: #e0c4ae; }
        QPushButton#primary {
            background: #f7b7a3;
            color: #4a3428;
        }
        QPushButton#primary:hover { background: #f0a790; }
        QPushButton#danger {
            background: #f0c4c4;
            color: #6a3030;
        }
        QPushButton#danger:hover { background: #e8b0b0; }
    )"));
}

void SettingsPanel::syncFromSettings()
{
    if (!m_settings) {
        return;
    }
    m_sizeSlider->setValue(m_settings->petSize());
    m_speedSlider->setValue(static_cast<int>(m_settings->movementSpeed() * 100.0));
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
}

int SettingsPanel::durationFromInputs() const
{
    return m_hours->value() * 3600 + m_minutes->value() * 60 + m_seconds->value();
}

void SettingsPanel::showNear(const QRect& petGlobalGeometry)
{
    syncFromSettings();
    syncFromTimer();

    QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const int maxH = screen ? std::max(320, screen->availableGeometry().height() - 48) : 560;
    setFixedHeight(std::min(560, maxH));
    adjustSize();

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
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 16.0, 16.0);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 35));
    p.drawPath(path.translated(0, 2));

    p.setBrush(QColor(255, 246, 238, 245));
    p.setPen(QPen(QColor(232, 210, 190, 200), 1.0));
    p.drawPath(path);
}
