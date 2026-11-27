#pragma once

#include "ai/OllamaClient.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class AppSettings;
class PixelCheckBox;
class PixelOptionButton;
class ProgressTracker;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QuestionGenerator;
class StudyLibrary;

/**
 * The Study window's Preferences tab: when Chiikawa asks questions, which question formats
 * to use, the local AI model, and resetting progress. Everything is stored through
 * AppSettings; the page never owns study data.
 */
class StudyPreferencesPage : public QWidget {
    Q_OBJECT

public:
    StudyPreferencesPage(AppSettings* settings,
                         StudyLibrary* library,
                         QuestionGenerator* generator,
                         OllamaClient* ollama,
                         ProgressTracker* progress,
                         QWidget* parent = nullptr);

    /** Refills the check-in study set picker from the library. */
    void reloadSets();
    /** Loads every control from AppSettings. */
    void syncFromSettings();
    void refreshProgress();
    /** Lists installed models; the card is only shown when a model can be chosen or downloaded. */
    void updateModels(OllamaClient::SetupState state);

    /** "Multiple choice", "Multiple choice, Written answer", "All formats" or "Only written answer". */
    [[nodiscard]] QString formatsSummary() const;

    // Accessors for tests
    [[nodiscard]] QCheckBox* checkInsCheck() const { return m_checkIns; }
    [[nodiscard]] QComboBox* sourceCombo() const { return m_source; }
    [[nodiscard]] QComboBox* frequencyCombo() const { return m_frequency; }
    [[nodiscard]] QPushButton* askNowButton() const { return m_askNow; }
    [[nodiscard]] QPushButton* resetButton() const { return m_reset; }
    [[nodiscard]] QLabel* progressLabel() const { return m_progressLabel; }
    [[nodiscard]] QComboBox* modelCombo() const { return m_modelCombo; }
    [[nodiscard]] QWidget* modelCard() const { return m_modelCard; }
    [[nodiscard]] PixelCheckBox* formatCheck(int formatBit) const { return m_formatChecks.value(formatBit, nullptr); }
    [[nodiscard]] PixelOptionButton* mixedModeButton() const { return m_mixedBtn; }
    [[nodiscard]] PixelOptionButton* focusModeButton() const { return m_focusBtn; }
    [[nodiscard]] QComboBox* focusCombo() const { return m_focusCombo; }
    [[nodiscard]] QPushButton* formatsToggle() const { return m_formatsToggle; }
    [[nodiscard]] bool formatsExpanded() const;

    /** Asks for confirmation unless confirmed is true, then clears progress. */
    bool resetProgress(bool confirmed = false);

signals:
    void preferencesChanged();
    void askQuestionRequested();
    void resetRequested();
    void buttonActivated();

private:
    void build();
    void applyCheckIns();
    void applyFormatControls();
    void setFormatsExpanded(bool expanded);
    void updateFormatsSummary();

    AppSettings* m_settings = nullptr;
    StudyLibrary* m_library = nullptr;
    QuestionGenerator* m_generator = nullptr;
    QPointer<OllamaClient> m_ollama;
    ProgressTracker* m_tracker = nullptr;

    QCheckBox* m_checkIns = nullptr;
    QComboBox* m_source = nullptr;
    QComboBox* m_frequency = nullptr;
    QPushButton* m_askNow = nullptr;

    QPushButton* m_formatsToggle = nullptr;
    QWidget* m_formatsBody = nullptr;
    QHash<int, PixelCheckBox*> m_formatChecks;
    PixelOptionButton* m_mixedBtn = nullptr;
    PixelOptionButton* m_focusBtn = nullptr;
    QComboBox* m_focusCombo = nullptr;
    QLabel* m_formatHint = nullptr;

    QWidget* m_modelCard = nullptr;
    QComboBox* m_modelCombo = nullptr;

    QLabel* m_progressLabel = nullptr;
    QPushButton* m_reset = nullptr;

    bool m_syncing = false;
};
