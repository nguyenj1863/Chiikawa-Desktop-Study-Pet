#include "SettingsPanel.h"

#include "AlarmScheduler.h"
#include "AlarmSoundManager.h"
#include "AppSettings.h"
#include "CollapsibleSection.h"
#include "PixelTheme.h"
#include "SettingsDefaults.h"
#include "StartupManager.h"
#include "StudyTimer.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>

namespace {

QFrame* makeDivider(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("pixelDivider"));
    line->setFrameShape(QFrame::HLine);
    return line;
}

void styleChipButton(QPushButton* btn)
{
    btn->setObjectName(QStringLiteral("chip"));
    btn->setProperty("active", false);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

} // namespace

SettingsPanel::SettingsPanel(AppSettings* settings,
                             StudyTimer* timer,
                             AlarmScheduler* alarmScheduler,
                             AlarmSoundManager* soundManager,
                             QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_settings(settings)
    , m_timer(timer)
    , m_alarmScheduler(alarmScheduler)
    , m_soundManager(soundManager)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setMinimumWidth(318);
    setMaximumWidth(352);
    setWindowTitle(QStringLiteral("Study Pet Settings"));
    setStyleSheet(PixelTheme::styleSheet());

    buildUi();
    syncFromSettings();
    syncFromTimer();
    syncFromAlarm();
    updateLayoutSize();

    if (m_timer) {
        connect(m_timer, &StudyTimer::stateChanged, this, [this](StudyTimer::State) {
            refreshTimerUi();
        });
        connect(m_timer, &StudyTimer::remainingChanged, this, [this](std::chrono::milliseconds) {
            refreshTimerUi();
        });
    }

    if (m_alarmScheduler) {
        connect(m_alarmScheduler, &AlarmScheduler::scheduleUpdated,
                this, &SettingsPanel::syncFromAlarm);
        connect(m_alarmScheduler, &AlarmScheduler::stateChanged,
                this, [this](AlarmScheduler::State) { syncFromAlarm(); });
    }
}

void SettingsPanel::buildUi()
{
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(18, 16, 18, 16);
    shell->setSpacing(8);

    // --- Header (draggable) ---
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("header"));
    m_headerWidget->setCursor(Qt::OpenHandCursor);
    m_headerWidget->setToolTip(QStringLiteral("Drag to move settings and Chiikawa"));

    auto* titleRow = new QHBoxLayout(m_headerWidget);
    titleRow->setContentsMargins(2, 0, 0, 2);
    titleRow->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Settings"), m_headerWidget);
    title->setObjectName(QStringLiteral("title"));

    auto* closeBtn = new PixelIconButton(PixelIcon::Close, 26, 12, false, m_headerWidget);
    closeBtn->setToolTip(QStringLiteral("Close settings"));
    closeBtn->setAccessibleName(QStringLiteral("Close settings"));
    connect(closeBtn, &QPushButton::clicked, this, &SettingsPanel::onClosePanel);

    titleRow->addWidget(title, 1);
    titleRow->addWidget(closeBtn);
    shell->addWidget(m_headerWidget);

    // --- Body scroll area (vertical scrollbar shown only if screen height is genuinely too small) ---
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

    // =========================================================================
    // SECTION 1: STUDY TIMER (Expanded by default)
    // =========================================================================
    auto* timerCard = new QFrame(m_contentWidget);
    timerCard->setObjectName(QStringLiteral("timerCard"));
    auto* timerCardLayout = new QVBoxLayout(timerCard);
    timerCardLayout->setContentsMargins(12, 10, 12, 12);
    timerCardLayout->setSpacing(8);

    auto* timerTitleRow = new QHBoxLayout();
    timerTitleRow->setContentsMargins(0, 0, 0, 0);
    auto* timerTitle = new QLabel(QStringLiteral("Study Timer"), timerCard);
    timerTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_editDurationBtn = new PixelIconButton(PixelIcon::Edit, 24, 12, false, timerCard);
    m_editDurationBtn->setToolTip(QStringLiteral("Edit custom duration"));
    m_editDurationBtn->setAccessibleName(QStringLiteral("Edit custom duration"));
    connect(m_editDurationBtn, &QPushButton::clicked, this, &SettingsPanel::onToggleDurationEditor);
    timerTitleRow->addWidget(timerTitle, 1);
    timerTitleRow->addWidget(m_editDurationBtn);
    timerCardLayout->addLayout(timerTitleRow);

    // Prominent central countdown display (click-to-edit when idle)
    m_countdownBtn = new QPushButton(QStringLiteral("25:00"), timerCard);
    m_countdownBtn->setObjectName(QStringLiteral("countdownDisplay"));
    m_countdownBtn->setCursor(Qt::PointingHandCursor);
    m_countdownBtn->setToolTip(QStringLiteral("Click to set custom duration"));
    m_countdownBtn->setAccessibleName(QStringLiteral("Countdown duration"));
    m_countdownBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_countdownBtn, &QPushButton::clicked, this, &SettingsPanel::onToggleDurationEditor);
    timerCardLayout->addWidget(m_countdownBtn);

    // Compact custom duration editor (hidden until clicked while idle)
    m_durationEditor = new QWidget(timerCard);
    auto* editRow = new QHBoxLayout(m_durationEditor);
    editRow->setContentsMargins(0, 0, 0, 0);
    editRow->setSpacing(6);

    m_hours = new QSpinBox(m_durationEditor);
    m_hours->setRange(0, 23);
    m_hours->setSuffix(QStringLiteral(" h"));
    m_hours->setAlignment(Qt::AlignCenter);
    m_minutes = new QSpinBox(m_durationEditor);
    m_minutes->setRange(0, 59);
    m_minutes->setSuffix(QStringLiteral(" m"));
    m_minutes->setAlignment(Qt::AlignCenter);
    m_seconds = new QSpinBox(m_durationEditor);
    m_seconds->setRange(0, 59);
    m_seconds->setSuffix(QStringLiteral(" s"));
    m_seconds->setAlignment(Qt::AlignCenter);
    connect(m_hours, &QSpinBox::valueChanged, this, &SettingsPanel::onDurationInputsChanged);
    connect(m_minutes, &QSpinBox::valueChanged, this, &SettingsPanel::onDurationInputsChanged);
    connect(m_seconds, &QSpinBox::valueChanged, this, &SettingsPanel::onDurationInputsChanged);

    editRow->addWidget(m_hours, 1);
    editRow->addWidget(m_minutes, 1);
    editRow->addWidget(m_seconds, 1);
    m_durationEditor->hide();
    timerCardLayout->addWidget(m_durationEditor);

    // Preset chips (25m / 45m / 60m)
    auto* presetsRow = new QHBoxLayout();
    presetsRow->setSpacing(6);
    m_preset25 = new QPushButton(QStringLiteral("25m"), timerCard);
    m_preset45 = new QPushButton(QStringLiteral("45m"), timerCard);
    m_preset60 = new QPushButton(QStringLiteral("60m"), timerCard);
    for (QPushButton* btn : {m_preset25, m_preset45, m_preset60}) {
        styleChipButton(btn);
    }
    connect(m_preset25, &QPushButton::clicked, this, &SettingsPanel::onPreset25);
    connect(m_preset45, &QPushButton::clicked, this, &SettingsPanel::onPreset45);
    connect(m_preset60, &QPushButton::clicked, this, &SettingsPanel::onPreset60);
    presetsRow->addWidget(m_preset25);
    presetsRow->addWidget(m_preset45);
    presetsRow->addWidget(m_preset60);
    timerCardLayout->addLayout(presetsRow);

    // Dynamic icon controls: Play/Pause + Reset (Cancel removed)
    auto* iconControls = new QHBoxLayout();
    iconControls->setSpacing(10);
    iconControls->addStretch(1);

    m_playPauseBtn = new PixelIconButton(PixelIcon::Play, 38, 16, true, timerCard);
    m_playPauseBtn->setToolTip(QStringLiteral("Start timer"));
    m_playPauseBtn->setAccessibleName(QStringLiteral("Start timer"));
    connect(m_playPauseBtn, &QPushButton::clicked, this, &SettingsPanel::onPlayPauseClicked);

    m_resetBtn = new PixelIconButton(PixelIcon::Reset, 38, 16, false, timerCard);
    m_resetBtn->setToolTip(QStringLiteral("Reset timer"));
    m_resetBtn->setAccessibleName(QStringLiteral("Reset timer"));
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsPanel::onResetClicked);

    iconControls->addWidget(m_playPauseBtn);
    iconControls->addWidget(m_resetBtn);
    iconControls->addStretch(1);
    timerCardLayout->addLayout(iconControls);

    root->addWidget(timerCard);

    // =========================================================================
    // SECTION 2: ALARM (Collapsed by default)
    // =========================================================================
    m_alarmSection = new CollapsibleSection(QStringLiteral("Alarm"), false, m_contentWidget);
    connect(m_alarmSection, &CollapsibleSection::heightChanged, this, [this]() {
        updateLayoutSize();
        emit panelResized();
    });

    auto* alarmBody = new QVBoxLayout();
    alarmBody->setSpacing(8);

    // Time picker + Enable + Save Alarm (single daily alarm)
    auto* alarmTimeRow = new QHBoxLayout();
    alarmTimeRow->setSpacing(6);

    auto* bellLbl = new QLabel(m_contentWidget);
    bellLbl->setFixedSize(16, 16);
    bellLbl->setPixmap(PixelTheme::icon(PixelIcon::Bell, PixelTheme::kPrimaryAccent, 14).pixmap(14, 14));

    m_alarmTimeEdit = new QTimeEdit(m_contentWidget);
    m_alarmTimeEdit->setDisplayFormat(QLocale::system().timeFormat(QLocale::ShortFormat));
    m_alarmTimeEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_alarmTimeEdit->setToolTip(QStringLiteral("Daily alarm time"));

    m_saveAlarmBtn = new QPushButton(QStringLiteral("Save Alarm"), m_contentWidget);
    m_saveAlarmBtn->setObjectName(QStringLiteral("primary"));
    m_saveAlarmBtn->setCursor(Qt::PointingHandCursor);
    m_saveAlarmBtn->setToolTip(QStringLiteral("Save daily alarm time and schedule"));
    connect(m_saveAlarmBtn, &QPushButton::clicked, this, &SettingsPanel::onSaveAlarmClicked);

    alarmTimeRow->addWidget(bellLbl);
    alarmTimeRow->addWidget(m_alarmTimeEdit, 1);
    alarmTimeRow->addWidget(m_saveAlarmBtn);
    alarmBody->addLayout(alarmTimeRow);

    m_alarmEnableCheck = new PixelCheckBox(QStringLiteral("Enable daily alarm"), m_contentWidget);
    connect(m_alarmEnableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_syncingUi || !m_alarmScheduler) {
            return;
        }
        m_alarmScheduler->configureAlarm(m_alarmTimeEdit->time(), checked);
        refreshAlarmSummary();
    });
    alarmBody->addWidget(m_alarmEnableCheck);

    m_soundCheck = new PixelCheckBox(QStringLiteral("Completion & alarm sound"), m_contentWidget);
    connect(m_soundCheck, &QCheckBox::toggled, this, &SettingsPanel::onPreferenceChanged);
    alarmBody->addWidget(m_soundCheck);

    // Sound selector dropdown + Preview button
    auto* soundRow = new QHBoxLayout();
    soundRow->setSpacing(6);

    m_soundCombo = new QComboBox(m_contentWidget);
    m_soundCombo->setCursor(Qt::PointingHandCursor);
    m_soundCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const auto& opt : AlarmSoundManager::availableSounds()) {
        m_soundCombo->addItem(opt.label, opt.id);
    }
    if (m_soundCombo->view()) {
        m_soundCombo->view()->setCursor(Qt::PointingHandCursor);
    }
    connect(m_soundCombo, &QComboBox::currentIndexChanged, this, &SettingsPanel::onPreferenceChanged);

    m_previewSoundBtn = new PixelIconButton(PixelIcon::Preview, 30, 14, false, m_contentWidget);
    m_previewSoundBtn->setToolTip(QStringLiteral("Preview sound"));
    m_previewSoundBtn->setAccessibleName(QStringLiteral("Preview sound"));
    connect(m_previewSoundBtn, &QPushButton::clicked, this, &SettingsPanel::onPreviewSoundClicked);

    soundRow->addWidget(m_soundCombo, 1);
    soundRow->addWidget(m_previewSoundBtn);
    alarmBody->addLayout(soundRow);

    // Volume slider
    auto* volRow = new QHBoxLayout();
    volRow->setSpacing(8);
    auto* volIcon = new QLabel(m_contentWidget);
    volIcon->setFixedSize(16, 16);
    volIcon->setPixmap(PixelTheme::icon(PixelIcon::Volume, PixelTheme::kSecondaryText, 14).pixmap(14, 14));
    m_volumeSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setFocusPolicy(Qt::StrongFocus);
    m_volumeValue = new QLabel(QStringLiteral("25%"), m_contentWidget);
    m_volumeValue->setObjectName(QStringLiteral("valueBadge"));
    m_volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeValue->setFixedWidth(36);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_volumeValue->setText(QStringLiteral("%1%").arg(v));
        onPreferenceChanged();
    });
    volRow->addWidget(volIcon);
    volRow->addWidget(m_volumeSlider, 1);
    volRow->addWidget(m_volumeValue);
    alarmBody->addLayout(volRow);

    // Notification toggle
    m_notifyCheck = new PixelCheckBox(QStringLiteral("Show alarm notification"), m_contentWidget);
    connect(m_notifyCheck, &QCheckBox::toggled, this, &SettingsPanel::onPreferenceChanged);
    alarmBody->addWidget(m_notifyCheck);

    // Snooze duration selector (5m / 10m / 15m)
    auto* snoozeRow = new QHBoxLayout();
    snoozeRow->setSpacing(6);
    auto* snoozeLbl = new QLabel(QStringLiteral("Snooze"), m_contentWidget);
    snoozeLbl->setFixedWidth(48);
    m_snooze5Btn = new QPushButton(QStringLiteral("5m"), m_contentWidget);
    m_snooze10Btn = new QPushButton(QStringLiteral("10m"), m_contentWidget);
    m_snooze15Btn = new QPushButton(QStringLiteral("15m"), m_contentWidget);
    for (QPushButton* btn : {m_snooze5Btn, m_snooze10Btn, m_snooze15Btn}) {
        styleChipButton(btn);
    }
    auto bindSnooze = [this](QPushButton* btn, int mins) {
        connect(btn, &QPushButton::clicked, this, [this, mins]() {
            if (!m_settings) {
                return;
            }
            m_settings->setSnoozeMinutes(mins);
            m_settings->save();
            updateSnoozeChips();
            emit settingsApplied();
        });
    };
    bindSnooze(m_snooze5Btn, 5);
    bindSnooze(m_snooze10Btn, 10);
    bindSnooze(m_snooze15Btn, 15);

    snoozeRow->addWidget(snoozeLbl);
    snoozeRow->addWidget(m_snooze5Btn);
    snoozeRow->addWidget(m_snooze10Btn);
    snoozeRow->addWidget(m_snooze15Btn);
    alarmBody->addLayout(snoozeRow);

    m_alarmSection->setContentLayout(alarmBody);
    root->addWidget(m_alarmSection);

    // =========================================================================
    // SECTION 3: PET PREFERENCES (Collapsed by default, auto-persisted)
    // =========================================================================
    m_petSection = new CollapsibleSection(QStringLiteral("Pet Preferences"), false, m_contentWidget);
    connect(m_petSection, &CollapsibleSection::heightChanged, this, [this]() {
        updateLayoutSize();
        emit panelResized();
    });

    auto* petBody = new QVBoxLayout();
    petBody->setSpacing(6);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(8);
    auto* sizeLabel = new QLabel(QStringLiteral("Size"), m_contentWidget);
    sizeLabel->setFixedWidth(44);
    m_sizeSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_sizeSlider->setRange(SettingsDefaults::kMinPetSize, SettingsDefaults::kMaxPetSize);
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setFocusPolicy(Qt::StrongFocus);
    m_sizeValue = new QLabel(m_contentWidget);
    m_sizeValue->setObjectName(QStringLiteral("valueBadge"));
    m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sizeValue->setFixedWidth(42);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sizeValue->setText(QStringLiteral("%1px").arg(v));
        onPreferenceChanged();
    });
    sizeRow->addWidget(sizeLabel);
    sizeRow->addWidget(m_sizeSlider, 1);
    sizeRow->addWidget(m_sizeValue);
    petBody->addLayout(sizeRow);

    auto* speedRow = new QHBoxLayout();
    speedRow->setSpacing(8);
    auto* speedLabel = new QLabel(QStringLiteral("Speed"), m_contentWidget);
    speedLabel->setFixedWidth(44);
    m_speedSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_speedSlider->setRange(25, 300);
    m_speedSlider->setCursor(Qt::PointingHandCursor);
    m_speedSlider->setFocusPolicy(Qt::StrongFocus);
    m_speedValue = new QLabel(m_contentWidget);
    m_speedValue->setObjectName(QStringLiteral("valueBadge"));
    m_speedValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_speedValue->setFixedWidth(42);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_speedValue->setText(QStringLiteral("%1x").arg(v / 100.0, 0, 'f', 2));
        onPreferenceChanged();
    });
    speedRow->addWidget(speedLabel);
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedValue);
    petBody->addLayout(speedRow);

    m_moveCheck = new PixelCheckBox(QStringLiteral("Autonomous movement"), m_contentWidget);
    m_animCheck = new PixelCheckBox(QStringLiteral("Random animations"), m_contentWidget);
    m_desktopOnlyCheck = new PixelCheckBox(QStringLiteral("Desktop-Only Mode"), m_contentWidget);
    m_desktopOnlyCheck->setToolTip(
        QStringLiteral("Keep Chiikawa on your desktop, behind other application windows."));
    m_startupCheck = new PixelCheckBox(QStringLiteral("Launch on Windows startup"), m_contentWidget);

    for (QCheckBox* cb : {m_moveCheck, m_animCheck, m_desktopOnlyCheck, m_startupCheck}) {
        connect(cb, &QCheckBox::toggled, this, &SettingsPanel::onPreferenceChanged);
        petBody->addWidget(cb);
    }

    auto* restoreBtn = new QPushButton(
        PixelTheme::icon(PixelIcon::RestoreDefaults, PixelTheme::kSecondaryText, 12),
        QStringLiteral(" Restore Defaults"), m_contentWidget);
    restoreBtn->setObjectName(QStringLiteral("subtle"));
    restoreBtn->setIconSize(QSize(12, 12));
    restoreBtn->setCursor(Qt::PointingHandCursor);
    restoreBtn->setToolTip(QStringLiteral("Reset pet and sound preferences to defaults"));
    connect(restoreBtn, &QPushButton::clicked, this, &SettingsPanel::onRestoreDefaultsClicked);
    petBody->addWidget(restoreBtn);

    m_petSection->setContentLayout(petBody);
    root->addWidget(m_petSection);

    // =========================================================================
    // FOOTER: Single Exit Application action
    // =========================================================================
    root->addWidget(makeDivider(m_contentWidget));

    auto* exitBtn = new QPushButton(
        PixelTheme::icon(PixelIcon::Exit, PixelTheme::kDangerText, 12),
        QStringLiteral(" Exit Application"), m_contentWidget);
    exitBtn->setObjectName(QStringLiteral("danger"));
    exitBtn->setIconSize(QSize(12, 12));
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setToolTip(QStringLiteral("Exit Chiikawa Study Pet"));
    connect(exitBtn, &QPushButton::clicked, this, &SettingsPanel::exitApplicationRequested);
    root->addWidget(exitBtn);

    m_scrollArea->setWidget(m_contentWidget);
    shell->addWidget(m_scrollArea, 1);
}

void SettingsPanel::syncFromSettings()
{
    if (!m_settings) {
        return;
    }
    m_syncingUi = true;

    m_sizeSlider->setValue(m_settings->petSize());
    m_sizeValue->setText(QStringLiteral("%1px").arg(m_settings->petSize()));
    m_speedSlider->setValue(static_cast<int>(m_settings->movementSpeed() * 100.0));
    m_speedValue->setText(QStringLiteral("%1x").arg(m_settings->movementSpeed(), 0, 'f', 2));
    m_moveCheck->setChecked(m_settings->movementEnabled());
    m_animCheck->setChecked(m_settings->autoAnimationEnabled());
    m_desktopOnlyCheck->setChecked(m_settings->desktopOnlyMode());
    m_startupCheck->setChecked(m_settings->launchOnStartup());

    m_soundCheck->setChecked(m_settings->soundEnabled());
    const int idx = m_soundCombo->findData(m_settings->alarmSoundId());
    m_soundCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    const int volPct = qRound(m_settings->alarmVolume() * 100.0);
    m_volumeSlider->setValue(volPct);
    m_volumeValue->setText(QStringLiteral("%1%").arg(volPct));
    m_notifyCheck->setChecked(m_settings->alarmNotificationEnabled());

    if (!m_timer || !m_timer->isActive()) {
        setDurationInputs(m_settings->lastTimerSeconds());
    }

    m_syncingUi = false;
    updatePresetChips();
    updateSnoozeChips();
    refreshPetSummary();
    refreshAlarmSummary();
}

void SettingsPanel::syncFromTimer()
{
    refreshTimerUi();
}

void SettingsPanel::syncFromAlarm()
{
    m_syncingUi = true;
    if (m_alarmScheduler && m_alarmScheduler->isConfigured()) {
        m_alarmTimeEdit->setTime(m_alarmScheduler->alarmTime());
        m_alarmEnableCheck->setChecked(m_alarmScheduler->isEnabled());
    } else if (m_settings && m_settings->hasAlarmConfigured() && m_settings->alarmTime().isValid()) {
        m_alarmTimeEdit->setTime(m_settings->alarmTime());
        m_alarmEnableCheck->setChecked(m_settings->alarmEnabled());
    } else {
        // New alarm defaults to current computer local time (editable, NOT auto-enabled)
        const QTime nowTime = QDateTime::currentDateTime().time();
        m_alarmTimeEdit->setTime(QTime(nowTime.hour(), nowTime.minute(), 0));
        m_alarmEnableCheck->setChecked(false);
    }
    m_syncingUi = false;
    refreshAlarmSummary();
}

void SettingsPanel::setDurationInputs(int totalSeconds)
{
    totalSeconds = std::clamp(totalSeconds, SettingsDefaults::kMinTimerSeconds, SettingsDefaults::kMaxTimerSeconds);
    const bool prev = m_syncingUi;
    m_syncingUi = true;
    m_hours->setValue(totalSeconds / 3600);
    m_minutes->setValue((totalSeconds % 3600) / 60);
    m_seconds->setValue(totalSeconds % 60);
    m_syncingUi = prev;
    updatePresetChips();
    refreshTimerUi();
}

int SettingsPanel::durationFromInputs() const
{
    return m_hours->value() * 3600 + m_minutes->value() * 60 + m_seconds->value();
}

void SettingsPanel::setChipActive(QPushButton* btn, bool active)
{
    if (!btn) {
        return;
    }
    if (btn->property("active").toBool() != active) {
        btn->setProperty("active", active);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        btn->update();
    }
}

void SettingsPanel::updatePresetChips()
{
    const int secs = durationFromInputs();
    setChipActive(m_preset25, secs == 25 * 60);
    setChipActive(m_preset45, secs == 45 * 60);
    setChipActive(m_preset60, secs == 60 * 60);
}

void SettingsPanel::updateSnoozeChips()
{
    const int mins = m_settings ? m_settings->snoozeMinutes() : SettingsDefaults::kSnoozeMinutes;
    setChipActive(m_snooze5Btn, mins == 5);
    setChipActive(m_snooze10Btn, mins == 10);
    setChipActive(m_snooze15Btn, mins == 15);
}

void SettingsPanel::setDurationEditorVisible(bool visible)
{
    if (!m_durationEditor || m_durationEditor->isVisible() == visible) {
        return;
    }
    m_durationEditor->setVisible(visible);
    updateLayoutSize();
    emit panelResized();
}

void SettingsPanel::refreshTimerUi()
{
    if (!m_timer) {
        return;
    }

    const bool running = m_timer->isRunning();
    const bool active = m_timer->isActive();

    if (active) {
        m_countdownBtn->setText(StudyTimer::formatRemaining(m_timer->remaining()));
        if (m_durationEditor->isVisible()) {
            setDurationEditorVisible(false);
        }
    } else {
        const auto secs = std::max(1, durationFromInputs());
        m_countdownBtn->setText(StudyTimer::formatRemaining(std::chrono::seconds{secs}));
    }

    // Prevent accidental duration edits while timer is running or paused
    m_countdownBtn->setEnabled(!active);
    m_countdownBtn->setCursor(active ? Qt::ArrowCursor : Qt::PointingHandCursor);
    m_editDurationBtn->setEnabled(!active);
    for (QPushButton* preset : {m_preset25, m_preset45, m_preset60}) {
        preset->setEnabled(!active);
        preset->setCursor(active ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }

    if (running) {
        m_playPauseBtn->setPixelIcon(PixelIcon::Pause);
        m_playPauseBtn->setToolTip(QStringLiteral("Pause timer"));
        m_playPauseBtn->setAccessibleName(QStringLiteral("Pause timer"));
    } else if (m_timer->isPaused()) {
        m_playPauseBtn->setPixelIcon(PixelIcon::Play);
        m_playPauseBtn->setToolTip(QStringLiteral("Resume timer"));
        m_playPauseBtn->setAccessibleName(QStringLiteral("Resume timer"));
    } else {
        m_playPauseBtn->setPixelIcon(PixelIcon::Play);
        m_playPauseBtn->setToolTip(QStringLiteral("Start timer"));
        m_playPauseBtn->setAccessibleName(QStringLiteral("Start timer"));
    }
}

void SettingsPanel::refreshAlarmSummary()
{
    if (!m_alarmSection) {
        return;
    }
    const bool enabled = m_alarmScheduler ? m_alarmScheduler->isEnabled() : false;
    const QTime t = (m_alarmScheduler && m_alarmScheduler->isConfigured())
        ? m_alarmScheduler->alarmTime()
        : m_alarmTimeEdit->time();
    const QString soundTag = (m_settings && m_settings->soundEnabled())
        ? QStringLiteral("Sound on")
        : QStringLiteral("Muted");

    if (!enabled || !t.isValid()) {
        m_alarmSection->setSummary(QStringLiteral("Off · %1").arg(soundTag));
        return;
    }

    const QString timeStr = QLocale::system().toString(t, QLocale::ShortFormat);
    if (m_alarmScheduler && m_alarmScheduler->state() == AlarmScheduler::State::Snoozed) {
        m_alarmSection->setSummary(QStringLiteral("%1 · Snoozed").arg(timeStr));
    } else {
        m_alarmSection->setSummary(QStringLiteral("%1 · %2").arg(soundTag, timeStr));
    }
}

void SettingsPanel::refreshPetSummary()
{
    if (!m_petSection || !m_settings) {
        return;
    }
    const QString mode = m_settings->desktopOnlyMode()
        ? QStringLiteral(" · Desktop-Only")
        : QString();
    m_petSection->setSummary(QStringLiteral("Size %1px%2").arg(m_settings->petSize()).arg(mode));
}

void SettingsPanel::updateLayoutSize(const QScreen* screen)
{
    if (!m_scrollArea || !m_contentWidget || !m_headerWidget) {
        return;
    }

    if (!screen) {
        screen = QGuiApplication::screenAt(frameGeometry().center());
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

void SettingsPanel::prepareToShow(const QScreen* screen)
{
    syncFromSettings();
    syncFromTimer();
    syncFromAlarm();
    updateLayoutSize(screen);
}

void SettingsPanel::setAlwaysOnTop(bool enabled)
{
    const bool currentlyOnTop = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    if (currentlyOnTop == enabled) {
        return;
    }
    const bool wasVisible = isVisible();
    const QPoint savedPos = pos();
    Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint;
    if (enabled) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    if (wasVisible) {
        move(savedPos);
        show();
        raise();
    }
}

void SettingsPanel::showNear(const QRect& petGlobalGeometry)
{
    QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    prepareToShow(screen);

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
    pos.setX(std::clamp(pos.x(), avail.left(), std::max(avail.left(), avail.right() - width() + 1)));
    pos.setY(std::clamp(pos.y(), avail.top(), std::max(avail.top(), avail.bottom() - height() + 1)));
}

void SettingsPanel::onPreset25()
{
    if (m_timer && m_timer->isActive()) {
        return;
    }
    setDurationInputs(25 * 60);
    if (m_settings) {
        m_settings->setLastTimerSeconds(25 * 60);
        m_settings->save();
    }
}

void SettingsPanel::onPreset45()
{
    if (m_timer && m_timer->isActive()) {
        return;
    }
    setDurationInputs(45 * 60);
    if (m_settings) {
        m_settings->setLastTimerSeconds(45 * 60);
        m_settings->save();
    }
}

void SettingsPanel::onPreset60()
{
    if (m_timer && m_timer->isActive()) {
        return;
    }
    setDurationInputs(60 * 60);
    if (m_settings) {
        m_settings->setLastTimerSeconds(60 * 60);
        m_settings->save();
    }
}

void SettingsPanel::onToggleDurationEditor()
{
    if (m_timer && m_timer->isActive()) {
        return;
    }
    setDurationEditorVisible(!m_durationEditor->isVisible());
}

void SettingsPanel::onDurationInputsChanged()
{
    if (m_syncingUi) {
        return;
    }
    const int secs = std::max(1, durationFromInputs());
    if (m_settings) {
        m_settings->setLastTimerSeconds(secs);
        m_settings->save();
    }
    updatePresetChips();
    refreshTimerUi();
}

void SettingsPanel::onPlayPauseClicked()
{
    if (!m_timer || !m_settings) {
        return;
    }
    if (m_timer->isRunning()) {
        m_timer->pause();
    } else if (m_timer->isPaused()) {
        m_timer->resume();
    } else {
        setDurationEditorVisible(false);
        const int secs = std::max(1, durationFromInputs());
        m_settings->setLastTimerSeconds(secs);
        m_settings->save();
        m_timer->configure(std::chrono::seconds{secs});
        m_timer->start();
    }
    refreshTimerUi();
}

void SettingsPanel::onResetClicked()
{
    if (!m_timer) {
        return;
    }
    const int secs = std::max(1, durationFromInputs());
    m_timer->configure(std::chrono::seconds{secs});
    m_timer->reset();
    refreshTimerUi();
}

void SettingsPanel::onSaveAlarmClicked()
{
    if (!m_alarmScheduler) {
        return;
    }
    const QTime chosen = m_alarmTimeEdit->time();
    m_alarmScheduler->configureAlarm(chosen, m_alarmEnableCheck->isChecked());
    refreshAlarmSummary();
}

void SettingsPanel::onPreviewSoundClicked()
{
    if (!m_soundManager) {
        return;
    }
    const QString soundId = m_soundCombo->currentData().toString();
    m_soundManager->setVolume(m_volumeSlider->value() / 100.0);
    m_soundManager->preview(soundId);
}

void SettingsPanel::onPreferenceChanged()
{
    if (m_syncingUi || !m_settings) {
        return;
    }
    m_settings->setPetSize(m_sizeSlider->value());
    m_settings->setMovementSpeed(m_speedSlider->value() / 100.0);
    m_settings->setMovementEnabled(m_moveCheck->isChecked());
    m_settings->setAutoAnimationEnabled(m_animCheck->isChecked());
    m_settings->setDesktopOnlyMode(m_desktopOnlyCheck->isChecked());

    const bool wantStartup = m_startupCheck->isChecked();
    if (wantStartup != m_settings->launchOnStartup()) {
        m_settings->setLaunchOnStartup(wantStartup);
        if (wantStartup) {
            (void)StartupManager::enable(QCoreApplication::applicationFilePath());
        } else {
            (void)StartupManager::disable();
        }
    }

    m_settings->setSoundEnabled(m_soundCheck->isChecked());
    m_settings->setAlarmSoundId(m_soundCombo->currentData().toString());
    m_settings->setAlarmVolume(m_volumeSlider->value() / 100.0);
    m_settings->setAlarmNotificationEnabled(m_notifyCheck->isChecked());

    m_settings->save();
    refreshPetSummary();
    refreshAlarmSummary();
    emit settingsApplied();
}

void SettingsPanel::onRestoreDefaultsClicked()
{
    if (!m_settings) {
        return;
    }
    PixelConfirmDialog dlg(
        QStringLiteral("Restore default settings?"),
        QStringLiteral("Pet and sound preferences will reset. Active timers and scheduled alarms stay intact."),
        QStringLiteral("Restore"),
        QStringLiteral("Cancel"),
        this);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    m_settings->restoreDefaults();
    syncFromSettings();
    emit settingsApplied();
}

void SettingsPanel::onClosePanel()
{
    hide();
    emit closedByUser();
}

void SettingsPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QWidget* hit = childAt(event->position().toPoint());
        bool interactive = false;
        for (QWidget* w = hit; w && w != this; w = w->parentWidget()) {
            if (qobject_cast<QAbstractButton*>(w)
                || qobject_cast<QAbstractSlider*>(w)
                || qobject_cast<QAbstractSpinBox*>(w)
                || qobject_cast<QComboBox*>(w)
                || qobject_cast<QScrollBar*>(w)
                || qobject_cast<QLineEdit*>(w)) {
                interactive = true;
                break;
            }
        }
        if (!interactive) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SettingsPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint desiredTopLeft = event->globalPosition().toPoint() - m_dragOffset;
        emit dragRequested(desiredTopLeft);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SettingsPanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        unsetCursor();
        emit dragFinished();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
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
    PixelTheme::paintPixelCard(p, rect(), PixelTheme::kBackground, PixelTheme::kBorder, 4);
}
