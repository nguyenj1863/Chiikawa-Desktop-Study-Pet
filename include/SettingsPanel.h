#pragma once

#include <QTime>
#include <QWidget>

class AlarmScheduler;
class AlarmSoundManager;
class AppSettings;
class CollapsibleSection;
class PixelIconButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScreen;
class QScrollArea;
class QSlider;
class QSpinBox;
class StudyTimer;
class UISoundManager;

/**
 * Compact pixel-art floating settings popup with collapsible sections,
 * icon-driven Study Timer, one-time Clock Alarm (12h/24h separate selectors),
 * and auto-persisted Pet Preferences.
 */
class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPanelWidth = 336;

    SettingsPanel(AppSettings* settings,
                  StudyTimer* timer,
                  AlarmScheduler* alarmScheduler,
                  AlarmSoundManager* soundManager,
                  UISoundManager* uiSound = nullptr,
                  QWidget* parent = nullptr);

    void showNear(const QRect& petGlobalGeometry);
    void prepareToShow(const QScreen* screen = nullptr);
    void setAlwaysOnTop(bool enabled);
    void syncFromSettings();
    void syncFromTimer();
    void syncFromAlarm();

    /** Converts separate hour/minute/AM-PM selector values into a QTime. */
    [[nodiscard]] static QTime composeAlarmTime(int displayHour, int minute, bool isPm, bool use24Hour);

    /** Decomposes a QTime into 12-hour or 24-hour display fields. */
    struct TimeParts {
        int displayHour = 12;
        int minute = 0;
        bool isPm = false;
    };
    [[nodiscard]] static TimeParts decomposeAlarmTime(const QTime& time, bool use24Hour);

    /** Current time selected in the alarm dropdowns. */
    [[nodiscard]] QTime selectedAlarmTime() const;

    /** Programmatically switches between 12h and 24h display preserving the underlying QTime. */
    void setUse24Hour(bool use24Hour);
    [[nodiscard]] bool use24Hour() const { return m_use24Hour; }

signals:
    void settingsApplied();
    void showTutorialRequested();
    void exitApplicationRequested();
    void dragRequested(const QPoint& desiredTopLeft);
    void dragFinished();
    void closedByUser();
    void panelResized();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onPreset25();
    void onPreset45();
    void onPreset60();
    void onToggleDurationEditor();
    void onDurationInputsChanged();
    void onPlayPauseClicked();
    void onResetClicked();
    void onSaveAlarmClicked();
    void onAlarmEnableToggled(bool checked);
    void onAlarmSelectorsChanged();
    void onPreviewSoundClicked();
    void onPreferenceChanged();
    void onRestoreDefaultsClicked();
    void onClosePanel();

private:
    void playUiClick();
    void buildUi();
    void populateHourCombo(bool use24Hour);
    void setAlarmSelectorsFromTime(const QTime& time);
    void updateTimeFormatButtons();
    void setDurationInputs(int totalSeconds);
    [[nodiscard]] int durationFromInputs() const;
    void updatePresetChips();
    void updateSnoozeChips();
    void setChipActive(QPushButton* btn, bool active);
    void setDurationEditorVisible(bool visible);
    void refreshTimerUi();
    void refreshAlarmSummary();
    void refreshPetSummary();
    void updateLayoutSize(const QScreen* screen = nullptr);
    void clampToScreen(QPoint& pos) const;

    AppSettings* m_settings = nullptr;
    StudyTimer* m_timer = nullptr;
    AlarmScheduler* m_alarmScheduler = nullptr;
    AlarmSoundManager* m_soundManager = nullptr;
    UISoundManager* m_uiSound = nullptr;

    QWidget* m_headerWidget = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;

    // Study Timer
    QPushButton* m_countdownBtn = nullptr;
    PixelIconButton* m_editDurationBtn = nullptr;
    QWidget* m_durationEditor = nullptr;
    QSpinBox* m_hours = nullptr;
    QSpinBox* m_minutes = nullptr;
    QSpinBox* m_seconds = nullptr;
    QPushButton* m_preset25 = nullptr;
    QPushButton* m_preset45 = nullptr;
    QPushButton* m_preset60 = nullptr;
    PixelIconButton* m_playPauseBtn = nullptr;
    PixelIconButton* m_resetBtn = nullptr;

    // Alarm (Collapsible, collapsed by default)
    CollapsibleSection* m_alarmSection = nullptr;
    QComboBox* m_alarmHourCombo = nullptr;
    QComboBox* m_alarmMinuteCombo = nullptr;
    QComboBox* m_alarmPeriodCombo = nullptr;
    QPushButton* m_format12Btn = nullptr;
    QPushButton* m_format24Btn = nullptr;
    QLabel* m_alarmScheduleLabel = nullptr;
    QCheckBox* m_alarmEnableCheck = nullptr;
    QPushButton* m_saveAlarmBtn = nullptr;
    QCheckBox* m_soundCheck = nullptr;
    QCheckBox* m_buttonSoundsCheck = nullptr;
    QComboBox* m_soundCombo = nullptr;
    PixelIconButton* m_previewSoundBtn = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeValue = nullptr;
    QCheckBox* m_notifyCheck = nullptr;
    QPushButton* m_snooze5Btn = nullptr;
    QPushButton* m_snooze10Btn = nullptr;
    QPushButton* m_snooze15Btn = nullptr;

    // Pet Preferences (Collapsible, collapsed by default)
    CollapsibleSection* m_petSection = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QLabel* m_sizeValue = nullptr;
    QSlider* m_speedSlider = nullptr;
    QLabel* m_speedValue = nullptr;
    QCheckBox* m_moveCheck = nullptr;
    QCheckBox* m_animCheck = nullptr;
    QCheckBox* m_desktopOnlyCheck = nullptr;
    QCheckBox* m_startupCheck = nullptr;

    bool m_use24Hour = false;
    bool m_syncingUi = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
