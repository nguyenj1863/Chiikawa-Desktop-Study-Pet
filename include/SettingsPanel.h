#pragma once

#include <QWidget>

class AppSettings;
class StudyTimer;
class QSpinBox;
class QSlider;
class QCheckBox;
class QPushButton;
class QLabel;
class QScrollArea;
class QScreen;

/**
 * Compact floating settings panel with soft pastel styling.
 * Automatically sizes to fit its content without unnecessary scrollbars.
 */
class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    SettingsPanel(AppSettings* settings, StudyTimer* timer, QWidget* parent = nullptr);

    void showNear(const QRect& petGlobalGeometry);
    void syncFromSettings();
    void syncFromTimer();
    void updateLayoutSize(const QScreen* screen = nullptr);

signals:
    void settingsApplied();
    void exitApplicationRequested();
    void closedByUser();

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onStart();
    void onPauseResume();
    void onReset();
    void onCancel();
    void onPreset25();
    void onPreset45();
    void onPreset60();
    void onApplyPetSettings();
    void onClosePanel();
    void refreshTimerButtons();
    void updatePresetSelection();

private:
    void buildUi();
    void applyStyle();
    void clampToScreen(QPoint& pos) const;
    [[nodiscard]] int durationFromInputs() const;
    void setDurationInputs(int totalSeconds);

    AppSettings* m_settings = nullptr;
    StudyTimer* m_timer = nullptr;

    QWidget* m_headerWidget = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;

    QSpinBox* m_hours = nullptr;
    QSpinBox* m_minutes = nullptr;
    QSpinBox* m_seconds = nullptr;
    QPushButton* m_preset25 = nullptr;
    QPushButton* m_preset45 = nullptr;
    QPushButton* m_preset60 = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    QSlider* m_sizeSlider = nullptr;
    QSlider* m_speedSlider = nullptr;
    QLabel* m_sizeValue = nullptr;
    QLabel* m_speedValue = nullptr;
    QCheckBox* m_moveCheck = nullptr;
    QCheckBox* m_animCheck = nullptr;
    QCheckBox* m_topCheck = nullptr;
    QCheckBox* m_startupCheck = nullptr;
    QCheckBox* m_soundCheck = nullptr;
};
