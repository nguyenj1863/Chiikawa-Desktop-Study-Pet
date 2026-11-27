#pragma once

#include "study/Question.h"

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <memory>

class AppSettings;
class OllamaClient;
class ProgressTracker;
class QLabel;
class QPushButton;
class QStackedWidget;
class QuizController;
class QuizPopup;
class SetQuestionProvider;
class StudyLibrary;
class WrittenAnswerEvaluator;

/**
 * Runs one study set inside the Study window: a question at a time (the same question UI as
 * Chiikawa's bubble), progress, and a summary at the end. Questions are asked in the formats
 * chosen in Preferences; due and new cards first. Grading, XP and review scheduling reuse
 * QuizController in practice mode.
 */
class StudyPracticeView : public QWidget {
    Q_OBJECT

public:
    StudyPracticeView(AppSettings* settings,
                      StudyLibrary* library,
                      ProgressTracker* progress,
                      OllamaClient* ollama,
                      QWidget* parent = nullptr);
    ~StudyPracticeView() override;

    /** Question formats practice uses: the enabled formats, or only the Focus format. */
    [[nodiscard]] static QList<QuestionType> enabledTypes(const AppSettings* settings);

    /** Starts a session over a study set; false (and nothing shown) when it has no questions. */
    bool begin(qint64 setId);
    /** Stops asking. With answers it shows the summary, otherwise it just closes. */
    void end();
    /** Drops the session without a summary or signal (the host is leaving). */
    void abandon();
    [[nodiscard]] bool isActive() const { return m_active; }
    [[nodiscard]] qint64 setId() const { return m_setId; }

    struct Summary {
        int total = 0;       // questions in the session
        int answered = 0;
        int correct = 0;
        int partial = 0;
        int incorrect = 0;
        int skipped = 0;
        int xp = 0;
        QStringList topicsToReview;
    };
    [[nodiscard]] const Summary& summary() const { return m_summary; }

    // Accessors for tests
    [[nodiscard]] QuizPopup* questionView() const { return m_question; }
    [[nodiscard]] QuizController* controller() const { return m_controller.get(); }
    [[nodiscard]] QLabel* progressLabel() const { return m_progressLabel; }
    [[nodiscard]] QPushButton* endButton() const { return m_endBtn; }
    [[nodiscard]] QPushButton* againButton() const { return m_againBtn; }
    [[nodiscard]] QPushButton* backButton() const { return m_backBtn; }
    [[nodiscard]] QLabel* summaryLabel() const { return m_summaryLabel; }
    [[nodiscard]] bool showingSummary() const;

signals:
    /** The session is over and the host should go back to the set. */
    void finished();
    void progressChanged();
    void buttonActivated();

private:
    void askNext();
    void showSummary();
    void updateProgress();
    void wireQuestion();

    AppSettings* m_settings = nullptr;
    StudyLibrary* m_library = nullptr;
    ProgressTracker* m_tracker = nullptr;
    std::unique_ptr<WrittenAnswerEvaluator> m_written;
    std::unique_ptr<QuizController> m_controller;
    SetQuestionProvider* m_provider = nullptr;   // owned by m_controller

    QStackedWidget* m_pages = nullptr;
    QLabel* m_progressLabel = nullptr;
    QPushButton* m_endBtn = nullptr;
    QuizPopup* m_question = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QPushButton* m_againBtn = nullptr;
    QPushButton* m_backBtn = nullptr;

    Summary m_summary;
    qint64 m_setId = 0;
    bool m_active = false;
};
