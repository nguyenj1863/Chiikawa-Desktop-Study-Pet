#include "SettingsPanel.h"

#include "AlarmScheduler.h"
#include "AlarmSoundManager.h"
#include "AppSettings.h"
#include "CollapsibleSection.h"
#include "PixelTheme.h"
#include "SettingsDefaults.h"
#include "StartupManager.h"
#include "StudyTimer.h"
#include "UISoundManager.h"

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

void styleTimeCombo(QComboBox* combo)
{
    combo->setObjectName(QStringLiteral("timeSelect"));
    combo->setCursor(Qt::PointingHandCursor);
    combo->setMinimumWidth(0);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    combo->setMaxVisibleItems(10);
    if (combo->view()) {
        combo->view()->setCursor(Qt::PointingHandCursor);
    }
}

} // namespace

QTime SettingsPanel::composeAlarmTime(int displayHour, int minute, bool isPm, bool use24Hour)
{
    const int clampedMin = std::clamp(minute, 0, 59);
    if (use24Hour) {
        return QTime(std::clamp(displayHour, 0, 23), clampedMin, 0);
    }

    int h12 = std::clamp(displayHour, 1, 12);
    int h24 = h12 % 12; // 12 AM -> 0, 1..11 AM -> 1..11
    if (isPm) {
        h24 += 12; // 12 PM -> 12, 1..11 PM -> 13..23
    }
    return QTime(h24, clampedMin, 0);
}

SettingsPanel::TimeParts SettingsPanel::decomposeAlarmTime(const QTime& time, bool use24Hour)
{
    TimeParts out;
    if (!time.isValid()) {
        out.displayHour = use24Hour ? 0 : 12;
        out.minute = 0;
        out.isPm = false;
        return out;
    }

    out.minute = time.minute();
    if (use24Hour) {
        out.displayHour = time.hour();
        out.isPm = (time.hour() >= 12);
    } else {
        const int h = time.hour();
        out.isPm = (h >= 12);
        const int rem = h % 12;
        out.displayHour = (rem == 0) ? 12 : rem;
    }
    return out;
}

SettingsPanel::SettingsPanel(AppSettings* settings,
                             StudyTimer* timer,
                             AlarmScheduler* alarmScheduler,
                             AlarmSoundManager* soundManager,
                             UISoundManager* uiSound,
                             QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_settings(settings)
    , m_timer(timer)
    , m_alarmScheduler(alarmScheduler)
    , m_soundManager(soundManager)
    , m_uiSound(uiSound)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFixedWidth(kPanelWidth);
    setWindowTitle(QStringLiteral("Chiikawa Settings"));
    setStyleSheet(PixelTheme::styleSheet());

    m_use24Hour = m_settings ? m_settings->alarmUse24Hour() : SettingsDefaults::kAlarmUse24Hour;

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

void SettingsPanel::playUiClick()
{
    if (m_uiSound) {
        m_uiSound->playClick();
    }
}

void SettingsPanel::buildUi()
{
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(14, 12, 14, 12);
    shell->setSpacing(6);

    // --- Header (draggable) ---
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("header"));
    m_headerWidget->setCursor(Qt::OpenHandCursor);
    m_headerWidget->setToolTip(QStringLiteral("Drag to move settings and Chiikawa"));

    auto* titleRow = new QHBoxLayout(m_headerWidget);
    titleRow->setContentsMargins(2, 0, 0, 0);
    titleRow->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("Settings"), m_headerWidget);
    title->setObjectName(QStringLiteral("title"));

    auto* closeBtn = new PixelIconButton(PixelIcon::Close, 24, 11, false, m_headerWidget);
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
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(7);

    // =========================================================================
    // SECTION 1: STUDY TIMER (Expanded by default)
    // =========================================================================
    auto* timerCard = new QFrame(m_contentWidget);
    timerCard->setObjectName(QStringLiteral("timerCard"));
    timerCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* timerCardLayout = new QVBoxLayout(timerCard);
    timerCardLayout->setContentsMargins(10, 8, 10, 10);
    timerCardLayout->setSpacing(6);

    auto* timerTitleRow = new QHBoxLayout();
    timerTitleRow->setContentsMargins(0, 0, 0, 0);
    auto* timerTitle = new QLabel(QStringLiteral("Study Timer"), timerCard);
    timerTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_editDurationBtn = new PixelIconButton(PixelIcon::Edit, 22, 11, false, timerCard);
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

    // Compact custom duration editor (fits cleanly inside timer card without widening popup)
    m_durationEditor = new QWidget(timerCard);
    m_durationEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* editRow = new QHBoxLayout(m_durationEditor);
    editRow->setContentsMargins(0, 0, 0, 0);
    editRow->setSpacing(6);

    auto configureSpin = [](QSpinBox* sb, int maxVal, const QString& suffix) {
        sb->setRange(0, maxVal);
        sb->setSuffix(suffix);
        sb->setAlignment(Qt::AlignCenter);
        sb->setMinimumWidth(0);
        sb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        sb->setFixedHeight(26);
    };

    m_hours = new QSpinBox(m_durationEditor);
    configureSpin(m_hours, 23, QStringLiteral("h"));
    m_minutes = new QSpinBox(m_durationEditor);
    configureSpin(m_minutes, 59, QStringLiteral("m"));
    m_seconds = new QSpinBox(m_durationEditor);
    configureSpin(m_seconds, 59, QStringLiteral("s"));

    connect(m_hours, &QSpinBox::valueChanged, this, &SettingsPanel::onDurationInputsChanged);
    connect(m_minutes, &QSpinBox::valueChanged, this, &SettingsPanel::onDurationInputsChanged);
    connect(m_seconds, &QSpinBox::valueChanged, this, &SettingsPanel::onDurationInputsChanged);
    connect(m_hours, &QAbstractSpinBox::editingFinished, this, &SettingsPanel::playUiClick);
    connect(m_minutes, &QAbstractSpinBox::editingFinished, this, &SettingsPanel::playUiClick);
    connect(m_seconds, &QAbstractSpinBox::editingFinished, this, &SettingsPanel::playUiClick);

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

    m_playPauseBtn = new PixelIconButton(PixelIcon::Play, 34, 15, true, timerCard);
    m_playPauseBtn->setToolTip(QStringLiteral("Start timer"));
    m_playPauseBtn->setAccessibleName(QStringLiteral("Start timer"));
    connect(m_playPauseBtn, &QPushButton::clicked, this, &SettingsPanel::onPlayPauseClicked);

    m_resetBtn = new PixelIconButton(PixelIcon::Reset, 34, 15, false, timerCard);
    m_resetBtn->setToolTip(QStringLiteral("Reset timer"));
    m_resetBtn->setAccessibleName(QStringLiteral("Reset timer"));
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsPanel::onResetClicked);

    iconControls->addWidget(m_playPauseBtn);
    iconControls->addWidget(m_resetBtn);
    iconControls->addStretch(1);
    timerCardLayout->addLayout(iconControls);

    root->addWidget(timerCard);

    // =========================================================================
    // SECTION 2: ONE-TIME ALARM (Collapsed by default)
    // =========================================================================
    m_alarmSection = new CollapsibleSection(QStringLiteral("Alarm"), false, m_contentWidget);
    connect(m_alarmSection, &CollapsibleSection::userToggled, this, [this](bool) {
        playUiClick();
    });
    connect(m_alarmSection, &CollapsibleSection::heightChanged, this, [this]() {
        updateLayoutSize();
        emit panelResized();
    });

    auto* alarmBody = new QVBoxLayout();
    alarmBody->setSpacing(6);

    // Row 1: Bell + "Alarm Time" + compact 12h | 24h toggle
    auto* formatRow = new QHBoxLayout();
    formatRow->setSpacing(5);

    auto* bellLbl = new QLabel(m_contentWidget);
    bellLbl->setFixedSize(14, 14);
    bellLbl->setPixmap(PixelTheme::icon(PixelIcon::Bell, PixelTheme::kPrimaryAccent, 13).pixmap(13, 13));

    auto* alarmTimeLbl = new QLabel(QStringLiteral("Alarm Time"), m_contentWidget);
    alarmTimeLbl->setObjectName(QStringLiteral("sectionTitle"));

    m_format12Btn = new QPushButton(QStringLiteral("12h"), m_contentWidget);
    m_format24Btn = new QPushButton(QStringLiteral("24h"), m_contentWidget);
    for (QPushButton* btn : {m_format12Btn, m_format24Btn}) {
        styleChipButton(btn);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btn->setFixedWidth(40);
        btn->setFixedHeight(22);
    }
    m_format12Btn->setToolTip(QStringLiteral("12-hour format"));
    m_format24Btn->setToolTip(QStringLiteral("24-hour format"));
    connect(m_format12Btn, &QPushButton::clicked, this, [this]() {
        playUiClick();
        setUse24Hour(false);
    });
    connect(m_format24Btn, &QPushButton::clicked, this, [this]() {
        playUiClick();
        setUse24Hour(true);
    });

    formatRow->addWidget(bellLbl);
    formatRow->addWidget(alarmTimeLbl, 1);
    formatRow->addWidget(m_format12Btn);
    formatRow->addWidget(m_format24Btn);
    alarmBody->addLayout(formatRow);

    // Row 2: Separate Hour : Minute [AM/PM] selectors
    auto* selectorsRow = new QHBoxLayout();
    selectorsRow->setSpacing(4);

    m_alarmHourCombo = new QComboBox(m_contentWidget);
    styleTimeCombo(m_alarmHourCombo);
    m_alarmHourCombo->setAccessibleName(QStringLiteral("Alarm hour"));
    m_alarmHourCombo->setToolTip(QStringLiteral("Alarm hour"));

    auto* colonLbl = new QLabel(QStringLiteral(":"), m_contentWidget);
    colonLbl->setAlignment(Qt::AlignCenter);
    colonLbl->setFixedWidth(10);
    colonLbl->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 13px; color: #654C43;"));

    m_alarmMinuteCombo = new QComboBox(m_contentWidget);
    styleTimeCombo(m_alarmMinuteCombo);
    m_alarmMinuteCombo->setAccessibleName(QStringLiteral("Alarm minute"));
    m_alarmMinuteCombo->setToolTip(QStringLiteral("Alarm minute"));
    for (int m = 0; m < 60; ++m) {
        m_alarmMinuteCombo->addItem(QStringLiteral("%1").arg(m, 2, 10, QChar(u'0')), m);
    }

    m_alarmPeriodCombo = new QComboBox(m_contentWidget);
    styleTimeCombo(m_alarmPeriodCombo);
    m_alarmPeriodCombo->setAccessibleName(QStringLiteral("AM or PM"));
    m_alarmPeriodCombo->setToolTip(QStringLiteral("AM or PM"));
    m_alarmPeriodCombo->addItem(QStringLiteral("AM"), false);
    m_alarmPeriodCombo->addItem(QStringLiteral("PM"), true);

    populateHourCombo(m_use24Hour);
    m_alarmPeriodCombo->setVisible(!m_use24Hour);

    connect(m_alarmHourCombo, &QComboBox::currentIndexChanged, this, &SettingsPanel::onAlarmSelectorsChanged);
    connect(m_alarmMinuteCombo, &QComboBox::currentIndexChanged, this, &SettingsPanel::onAlarmSelectorsChanged);
    connect(m_alarmPeriodCombo, &QComboBox::currentIndexChanged, this, &SettingsPanel::onAlarmSelectorsChanged);
    connect(m_alarmHourCombo, &QComboBox::activated, this, [this](int) { playUiClick(); });
    connect(m_alarmMinuteCombo, &QComboBox::activated, this, [this](int) { playUiClick(); });
    connect(m_alarmPeriodCombo, &QComboBox::activated, this, [this](int) { playUiClick(); });

    selectorsRow->addWidget(m_alarmHourCombo, 1);
    selectorsRow->addWidget(colonLbl, 0);
    selectorsRow->addWidget(m_alarmMinuteCombo, 1);
    selectorsRow->addWidget(m_alarmPeriodCombo, 1);
    alarmBody->addLayout(selectorsRow);

    // Row 3: Schedule indicator + Save Alarm button
    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);

    m_alarmScheduleLabel = new QLabel(QStringLiteral("Not scheduled"), m_contentWidget);
    m_alarmScheduleLabel->setObjectName(QStringLiteral("sectionSummary"));
    m_alarmScheduleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_saveAlarmBtn = new QPushButton(QStringLiteral("Save Alarm"), m_contentWidget);
    m_saveAlarmBtn->setObjectName(QStringLiteral("primary"));
    m_saveAlarmBtn->setCursor(Qt::PointingHandCursor);
    m_saveAlarmBtn->setToolTip(QStringLiteral("Schedule this one-time alarm"));
    connect(m_saveAlarmBtn, &QPushButton::clicked, this, &SettingsPanel::onSaveAlarmClicked);

    actionRow->addWidget(m_alarmScheduleLabel, 1);
    actionRow->addWidget(m_saveAlarmBtn);
    alarmBody->addLayout(actionRow);

    // One-time enable toggle (no daily recurrence)
    m_alarmEnableCheck = new PixelCheckBox(QStringLiteral("Enable alarm"), m_contentWidget);
    m_alarmEnableCheck->setToolTip(QStringLiteral("Trigger once at the selected time"));
    connect(m_alarmEnableCheck, &QCheckBox::toggled, this, &SettingsPanel::onAlarmEnableToggled);
    connect(m_alarmEnableCheck, &QCheckBox::clicked, this, [this](bool) { playUiClick(); });
    alarmBody->addWidget(m_alarmEnableCheck);

    m_soundCheck = new PixelCheckBox(QStringLiteral("Completion & alarm sound"), m_contentWidget);
    connect(m_soundCheck, &QCheckBox::toggled, this, &SettingsPanel::onPreferenceChanged);
    connect(m_soundCheck, &QCheckBox::clicked, this, [this](bool) { playUiClick(); });
    alarmBody->addWidget(m_soundCheck);

    m_buttonSoundsCheck = new PixelCheckBox(QStringLiteral("Button Sounds"), m_contentWidget);
    m_buttonSoundsCheck->setObjectName(QStringLiteral("buttonSoundsCheck"));
    m_buttonSoundsCheck->setToolTip(QStringLiteral("Play a gentle click sound when pressing controls"));
    connect(m_buttonSoundsCheck, &QCheckBox::toggled, this, &SettingsPanel::onPreferenceChanged);
    connect(m_buttonSoundsCheck, &QCheckBox::clicked, this, [this](bool checked) {
        if (m_uiSound) {
            m_uiSound->setEnabled(checked);
            if (checked) {
                m_uiSound->playClick();
            }
        }
    });
    alarmBody->addWidget(m_buttonSoundsCheck);

    // Sound selector dropdown + Preview button
    auto* soundRow = new QHBoxLayout();
    soundRow->setSpacing(6);

    m_soundCombo = new QComboBox(m_contentWidget);
    m_soundCombo->setCursor(Qt::PointingHandCursor);
    m_soundCombo->setMinimumWidth(0);
    m_soundCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const auto& opt : AlarmSoundManager::availableSounds()) {
        m_soundCombo->addItem(opt.label, opt.id);
    }
    if (m_soundCombo->view()) {
        m_soundCombo->view()->setCursor(Qt::PointingHandCursor);
    }
    connect(m_soundCombo, &QComboBox::currentIndexChanged, this, &SettingsPanel::onPreferenceChanged);
    connect(m_soundCombo, &QComboBox::activated, this, [this](int) { playUiClick(); });

    m_previewSoundBtn = new PixelIconButton(PixelIcon::Preview, 28, 13, false, m_contentWidget);
    m_previewSoundBtn->setToolTip(QStringLiteral("Preview sound"));
    m_previewSoundBtn->setAccessibleName(QStringLiteral("Preview sound"));
    connect(m_previewSoundBtn, &QPushButton::clicked, this, &SettingsPanel::onPreviewSoundClicked);

    soundRow->addWidget(m_soundCombo, 1);
    soundRow->addWidget(m_previewSoundBtn);
    alarmBody->addLayout(soundRow);

    // Volume slider
    auto* volRow = new QHBoxLayout();
    volRow->setSpacing(6);
    auto* volIcon = new QLabel(m_contentWidget);
    volIcon->setFixedSize(14, 14);
    volIcon->setPixmap(PixelTheme::icon(PixelIcon::Volume, PixelTheme::kSecondaryText, 13).pixmap(13, 13));
    m_volumeSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setFocusPolicy(Qt::StrongFocus);
    m_volumeValue = new QLabel(QStringLiteral("25%"), m_contentWidget);
    m_volumeValue->setObjectName(QStringLiteral("valueBadge"));
    m_volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeValue->setFixedWidth(34);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_volumeValue->setText(QStringLiteral("%1%").arg(v));
        onPreferenceChanged();
    });
    connect(m_volumeSlider, &QSlider::sliderReleased, this, &SettingsPanel::playUiClick);
    volRow->addWidget(volIcon);
    volRow->addWidget(m_volumeSlider, 1);
    volRow->addWidget(m_volumeValue);
    alarmBody->addLayout(volRow);

    // Notification toggle
    m_notifyCheck = new PixelCheckBox(QStringLiteral("Show alarm notification"), m_contentWidget);
    connect(m_notifyCheck, &QCheckBox::toggled, this, &SettingsPanel::onPreferenceChanged);
    connect(m_notifyCheck, &QCheckBox::clicked, this, [this](bool) { playUiClick(); });
    alarmBody->addWidget(m_notifyCheck);

    // Snooze duration selector (5m / 10m / 15m)
    auto* snoozeRow = new QHBoxLayout();
    snoozeRow->setSpacing(5);
    auto* snoozeLbl = new QLabel(QStringLiteral("Snooze"), m_contentWidget);
    snoozeLbl->setFixedWidth(44);
    m_snooze5Btn = new QPushButton(QStringLiteral("5m"), m_contentWidget);
    m_snooze10Btn = new QPushButton(QStringLiteral("10m"), m_contentWidget);
    m_snooze15Btn = new QPushButton(QStringLiteral("15m"), m_contentWidget);
    for (QPushButton* btn : {m_snooze5Btn, m_snooze10Btn, m_snooze15Btn}) {
        styleChipButton(btn);
    }
    auto bindSnooze = [this](QPushButton* btn, int mins) {
        connect(btn, &QPushButton::clicked, this, [this, mins]() {
            playUiClick();
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
    connect(m_petSection, &CollapsibleSection::userToggled, this, [this](bool) {
        playUiClick();
    });
    connect(m_petSection, &CollapsibleSection::heightChanged, this, [this]() {
        updateLayoutSize();
        emit panelResized();
    });

    auto* petBody = new QVBoxLayout();
    petBody->setSpacing(6);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(6);
    auto* sizeLabel = new QLabel(QStringLiteral("Size"), m_contentWidget);
    sizeLabel->setFixedWidth(40);
    m_sizeSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_sizeSlider->setRange(SettingsDefaults::kMinPetSize, SettingsDefaults::kMaxPetSize);
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setFocusPolicy(Qt::StrongFocus);
    m_sizeValue = new QLabel(m_contentWidget);
    m_sizeValue->setObjectName(QStringLiteral("valueBadge"));
    m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sizeValue->setFixedWidth(40);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sizeValue->setText(QStringLiteral("%1px").arg(v));
        onPreferenceChanged();
    });
    connect(m_sizeSlider, &QSlider::sliderReleased, this, &SettingsPanel::playUiClick);
    sizeRow->addWidget(sizeLabel);
    sizeRow->addWidget(m_sizeSlider, 1);
    sizeRow->addWidget(m_sizeValue);
    petBody->addLayout(sizeRow);

    auto* speedRow = new QHBoxLayout();
    speedRow->setSpacing(6);
    auto* speedLabel = new QLabel(QStringLiteral("Speed"), m_contentWidget);
    speedLabel->setFixedWidth(40);
    m_speedSlider = new QSlider(Qt::Horizontal, m_contentWidget);
    m_speedSlider->setRange(25, 300);
    m_speedSlider->setCursor(Qt::PointingHandCursor);
    m_speedSlider->setFocusPolicy(Qt::StrongFocus);
    m_speedValue = new QLabel(m_contentWidget);
    m_speedValue->setObjectName(QStringLiteral("valueBadge"));
    m_speedValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_speedValue->setFixedWidth(40);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_speedValue->setText(QStringLiteral("%1x").arg(v / 100.0, 0, 'f', 2));
        onPreferenceChanged();
    });
    connect(m_speedSlider, &QSlider::sliderReleased, this, &SettingsPanel::playUiClick);
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
        connect(cb, &QCheckBox::clicked, this, [this](bool) { playUiClick(); });
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
    exitBtn->setToolTip(QStringLiteral("Exit Chiikawa"));
    connect(exitBtn, &QPushButton::clicked, this, [this]() {
        playUiClick();
        emit exitApplicationRequested();
    });
    root->addWidget(exitBtn);

    m_scrollArea->setWidget(m_contentWidget);
    shell->addWidget(m_scrollArea, 1);
}

void SettingsPanel::populateHourCombo(bool use24Hour)
{
    m_alarmHourCombo->clear();
    if (use24Hour) {
        for (int h = 0; h < 24; ++h) {
            m_alarmHourCombo->addItem(QStringLiteral("%1").arg(h, 2, 10, QChar(u'0')), h);
        }
    } else {
        for (int h = 1; h <= 12; ++h) {
            m_alarmHourCombo->addItem(QStringLiteral("%1").arg(h, 2, 10, QChar(u'0')), h);
        }
    }
}

void SettingsPanel::setAlarmSelectorsFromTime(const QTime& time)
{
    const TimeParts parts = decomposeAlarmTime(time, m_use24Hour);
    const int hourIdx = m_alarmHourCombo->findData(parts.displayHour);
    if (hourIdx >= 0) {
        m_alarmHourCombo->setCurrentIndex(hourIdx);
    }
    const int minIdx = m_alarmMinuteCombo->findData(parts.minute);
    if (minIdx >= 0) {
        m_alarmMinuteCombo->setCurrentIndex(minIdx);
    }
    m_alarmPeriodCombo->setCurrentIndex(parts.isPm ? 1 : 0);
}

QTime SettingsPanel::selectedAlarmTime() const
{
    const int displayHour = m_alarmHourCombo->currentData().toInt();
    const int minute = m_alarmMinuteCombo->currentData().toInt();
    const bool isPm = m_alarmPeriodCombo->currentData().toBool();
    return composeAlarmTime(displayHour, minute, isPm, m_use24Hour);
}

void SettingsPanel::setUse24Hour(bool use24Hour)
{
    if (m_use24Hour == use24Hour) {
        updateTimeFormatButtons();
        return;
    }

    const QTime current = selectedAlarmTime();
    const bool prevSync = m_syncingUi;
    m_syncingUi = true;
    m_use24Hour = use24Hour;
    populateHourCombo(m_use24Hour);
    m_alarmPeriodCombo->setVisible(!m_use24Hour);
    setAlarmSelectorsFromTime(current);
    m_syncingUi = prevSync;

    if (m_settings) {
        m_settings->setAlarmUse24Hour(m_use24Hour);
        m_settings->save();
    }
    updateTimeFormatButtons();
    refreshAlarmSummary();
}

void SettingsPanel::updateTimeFormatButtons()
{
    setChipActive(m_format12Btn, !m_use24Hour);
    setChipActive(m_format24Btn, m_use24Hour);
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
    m_buttonSoundsCheck->setChecked(m_settings->buttonSoundsEnabled());
    const int idx = m_soundCombo->findData(m_settings->alarmSoundId());
    m_soundCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    const int volPct = qRound(m_settings->alarmVolume() * 100.0);
    m_volumeSlider->setValue(volPct);
    m_volumeValue->setText(QStringLiteral("%1%").arg(volPct));
    m_notifyCheck->setChecked(m_settings->alarmNotificationEnabled());

    const bool want24 = m_settings->alarmUse24Hour();
    if (want24 != m_use24Hour) {
        const QTime cur = selectedAlarmTime();
        m_use24Hour = want24;
        populateHourCombo(m_use24Hour);
        m_alarmPeriodCombo->setVisible(!m_use24Hour);
        setAlarmSelectorsFromTime(cur);
    }

    if (!m_timer || !m_timer->isActive()) {
        setDurationInputs(m_settings->lastTimerSeconds());
    }

    m_syncingUi = false;
    updatePresetChips();
    updateSnoozeChips();
    updateTimeFormatButtons();
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
        setAlarmSelectorsFromTime(m_alarmScheduler->alarmTime());
        m_alarmEnableCheck->setChecked(m_alarmScheduler->isEnabled());
    } else if (m_settings && m_settings->hasAlarmConfigured() && m_settings->alarmTime().isValid()) {
        setAlarmSelectorsFromTime(m_settings->alarmTime());
        m_alarmEnableCheck->setChecked(m_settings->alarmEnabled());
    } else {
        // New alarm defaults to current computer local time (editable, NOT auto-enabled)
        const QTime nowTime = QDateTime::currentDateTime().time();
        setAlarmSelectorsFromTime(QTime(nowTime.hour(), nowTime.minute(), 0));
        m_alarmEnableCheck->setChecked(false);
    }
    m_syncingUi = false;
    updateTimeFormatButtons();
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
    const QDateTime now = QDateTime::currentDateTime();
    const bool enabled = m_alarmScheduler ? m_alarmScheduler->isEnabled() : false;
    const auto state = m_alarmScheduler ? m_alarmScheduler->state() : AlarmScheduler::State::Inactive;
    const QString soundTag = (m_settings && m_settings->soundEnabled())
        ? QStringLiteral("Sound on")
        : QStringLiteral("Muted");

    if (state == AlarmScheduler::State::Snoozed && m_alarmScheduler->snoozeTargetTime().isValid()) {
        const QString occ = AlarmScheduler::formatOccurrence(
            m_alarmScheduler->snoozeTargetTime(), now, m_use24Hour);
        m_alarmScheduleLabel->setText(QStringLiteral("Snoozed · %1").arg(occ));
        m_alarmSection->setSummary(QStringLiteral("Snoozed · %1").arg(occ));
        return;
    }

    if (enabled && m_alarmScheduler && m_alarmScheduler->nextTriggerTime().isValid()) {
        const QString occ = AlarmScheduler::formatOccurrence(
            m_alarmScheduler->nextTriggerTime(), now, m_use24Hour);
        m_alarmScheduleLabel->setText(occ);
        m_alarmSection->setSummary(occ);
        return;
    }

    m_alarmScheduleLabel->setText(QStringLiteral("Not scheduled"));
    m_alarmSection->setSummary(soundTag);
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

    if (m_contentWidget->layout()) {
        m_contentWidget->layout()->activate();
    }

    const QMargins margins = layout()->contentsMargins();
    const int spacing = layout()->spacing();

    const int headerH = m_headerWidget->sizeHint().height();
    const int contentH = m_contentWidget->sizeHint().height();
    const int neededH = margins.top() + headerH + spacing + contentH + margins.bottom() + 2;

    const int maxH = screen ? std::max(280, screen->availableGeometry().height() - 32) : 640;

    if (neededH <= maxH) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFixedSize(kPanelWidth, neededH);
    } else {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFixedSize(kPanelWidth, maxH);
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
    playUiClick();
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
    playUiClick();
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
    playUiClick();
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
    playUiClick();
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
    playUiClick();
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
    playUiClick();
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
    playUiClick();
    const QTime chosen = selectedAlarmTime();
    // Explicitly saving activates the one-time alarm
    m_alarmScheduler->configureAlarm(chosen, true);
    refreshAlarmSummary();
}

void SettingsPanel::onAlarmEnableToggled(bool checked)
{
    if (m_syncingUi || !m_alarmScheduler) {
        return;
    }
    m_alarmScheduler->configureAlarm(selectedAlarmTime(), checked);
    refreshAlarmSummary();
}

void SettingsPanel::onAlarmSelectorsChanged()
{
    if (m_syncingUi) {
        return;
    }
    // If alarm is already enabled, update its scheduled occurrence live;
    // otherwise keep it inactive until user clicks Save Alarm or Enable alarm.
    if (m_alarmScheduler && m_alarmEnableCheck->isChecked()) {
        m_alarmScheduler->configureAlarm(selectedAlarmTime(), true);
    }
    refreshAlarmSummary();
}

void SettingsPanel::onPreviewSoundClicked()
{
    playUiClick();
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
    m_settings->setButtonSoundsEnabled(m_buttonSoundsCheck->isChecked());
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
    playUiClick();
    PixelConfirmDialog dlg(
        QStringLiteral("Restore default settings?"),
        QStringLiteral("Pet and sound preferences will reset. Active timers and scheduled alarms stay intact."),
        QStringLiteral("Restore"),
        QStringLiteral("Cancel"),
        this);

    const int res = dlg.exec();
    playUiClick();
    if (res != QDialog::Accepted) {
        return;
    }

    m_settings->restoreDefaults();
    if (m_uiSound) {
        m_uiSound->setEnabled(m_settings->buttonSoundsEnabled());
    }
    syncFromSettings();
    emit settingsApplied();
}

void SettingsPanel::onClosePanel()
{
    playUiClick();
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
                || qobject_cast<QLineEdit*>(w)
                || w->objectName() == QStringLiteral("sectionHeader")) {
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
