#pragma once

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
class QTimeEdit;
class StudyTimer;

/**
 * Kawaii pixel-art floating settings popup with collapsible sections,
 * compact icon-driven Study Timer, Clock Alarm, and auto-persisted Pet Preferences.
 */
class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    SettingsPanel(AppSettings* settings,
                  StudyTimer* timer,
                  AlarmScheduler* alarmScheduler,
                  AlarmSoundManager* soundManager,
                  QWidget* parent = nullptr);

    void showNear(const QRect& petGlobalGeometry);
    void prepareToShow(const QScreen* screen = nullptr);
    void setAlwaysOnTop(bool enabled);
    void syncFromSettings();
    void syncFromTimer();
    void syncFromAlarm();
    void updateLayoutSize(const QScreen* screen = nullptr);

    // Accessors for automated UI tests
    [[nodiscard]] CollapsibleSection* alarmSection() const { return m_alarmSection; }
    [[nodiscard]] CollapsibleSection* petSection() const { return m_petSection; }

signals:
    void settingsApplied();
    void exitApplicationRequested();
    void closedByUser();
    void dragRequested(const QPoint& desiredTopLeft);
    void dragFinished();
    void panelResized();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPlayPauseClicked();
    void onResetClicked();
    void onPreset25();
    void onPreset45();
    void onPreset60();
    void onToggleDurationEditor();
    void onDurationInputsChanged();
    void onSaveAlarmClicked();
    void onPreviewSoundClicked();
    void onPreferenceChanged();
    void onRestoreDefaultsClicked();
    void onClosePanel();
    void refreshTimerUi();
    void refreshAlarmSummary();
    void refreshPetSummary();

private:
    void buildUi();
    void clampToScreen(QPoint& pos) const;
    [[nodiscard]] int durationFromInputs() const;
    void setDurationInputs(int totalSeconds);
    void updatePresetChips();
    void updateSnoozeChips();
    void setDurationEditorVisible(bool visible);
    void setChipActive(QPushButton* btn, bool active);

    AppSettings* m_settings = nullptr;
    StudyTimer* m_timer = nullptr;
    AlarmScheduler* m_alarmScheduler = nullptr;
    AlarmSoundManager* m_soundManager = nullptr;

    QWidget* m_headerWidget = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;

    // Study Timer controls
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

    // Alarm collapsible section
    CollapsibleSection* m_alarmSection = nullptr;
    QTimeEdit* m_alarmTimeEdit = nullptr;
    QCheckBox* m_alarmEnableCheck = nullptr;
    QPushButton* m_saveAlarmBtn = nullptr;
    QCheckBox* m_soundCheck = nullptr;
    QComboBox* m_soundCombo = nullptr;
    PixelIconButton* m_previewSoundBtn = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeValue = nullptr;
    QCheckBox* m_notifyCheck = nullptr;
    QPushButton* m_snooze5Btn = nullptr;
    QPushButton* m_snooze10Btn = nullptr;
    QPushButton* m_snooze15Btn = nullptr;

    // Pet Preferences collapsible section
    CollapsibleSection* m_petSection = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QSlider* m_speedSlider = nullptr;
    QLabel* m_sizeValue = nullptr;
    QLabel* m_speedValue = nullptr;
    QCheckBox* m_moveCheck = nullptr;
    QCheckBox* m_animCheck = nullptr;
    QCheckBox* m_desktopOnlyCheck = nullptr;
    QCheckBox* m_startupCheck = nullptr;

    bool m_syncingUi = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
