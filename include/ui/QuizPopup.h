#pragma once

#include "study/Evaluation.h"
#include "study/Question.h"

#include <QElapsedTimer>
#include <QList>
#include <QTimer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class PixelIconButton;
class PixelOptionButton;

/**
 * Compact smoked-glass speech bubble that shows one check-in question near the pet.
 * It appears without activating or taking keyboard focus; text inputs only take
 * focus after the user deliberately clicks the popup. The layout adapts to the
 * question format (single-choice radios, multi-select
 * checkboxes, written answer with Check / Reveal) and scrolls instead of clipping
 * when content is taller than the cap. Provider-agnostic: it only consumes
 * Question and Evaluation.
 *
 * In embedded mode the same question UI is a plain child widget (no window, fade, tail, or
 * auto-hide) used inside the Study window's practice page. There, closed() means "move on":
 * it follows Skip and the button that appears after a result (see setAdvanceText()).
 */
class QuizPopup : public QWidget {
    Q_OBJECT

public:
    static constexpr int kStudyWidth = 324;
    static constexpr int kMaxHeight = 460;
    static constexpr int kPetGapPx = 8;
    static constexpr int kTailHeight = 7;
    static constexpr int kFadeInMs = 180;
    static constexpr int kFadeOutMs = 200;
    static constexpr int kEntranceSlidePx = 6;
    static constexpr int kStudyResultDisplayMs = 15000;

    /** Where the bubble sits relative to the pet; the tail points back toward it. */
    enum class Placement {
        Above,
        Below,
        Left,
        Right
    };

    enum class Phase {
        Hidden,
        Asking,
        Suggesting,   // "Did you mean …?" for a written answer
        Judging,      // written answer being checked by the local model
        Result,
        Revealed,     // expected answer shown, waiting for self-assessment
        Notice
    };

    struct Layout {
        QPoint pos;
        Placement placement = Placement::Above;
    };

    explicit QuizPopup(QWidget* parent = nullptr, bool embedded = false);
    ~QuizPopup() override;

    void showQuestion(const Question& question, const QRect& petGlobalGeometry);
    /** Result: verdict, XP, per-option marks, feedback, and source. */
    void showEvaluation(const Evaluation& evaluation, int xp);
    void showSuggestion(const QString& suggestion);
    void showJudging();
    /** Shows the expected answer, key points, explanation, and source, then asks for a self-rating. */
    void showReveal();
    /** Informational bubble (e.g. no study questions yet) with an optional action button. */
    void showNotice(const QString& title, const QString& body, const QString& actionText,
                    const QRect& petGlobalGeometry);

    /** Keeps the bubble beside the pet while it moves (no animation). */
    void followPet(const QRect& petGlobalGeometry);

    /** Fades out and hides; emits closed() once hidden. */
    void dismiss();

    /** Pure placement helper: prefers above, then sides, then below; always inside available. */
    [[nodiscard]] static Layout calculateLayout(const QRect& petRect,
                                                const QSize& popupSize,
                                                const QRect& available,
                                                int gap = kPetGapPx);

    /** Height available for the popup on a screen: kMaxHeight, or 60% of short work areas. */
    [[nodiscard]] static int heightCap(const QRect& available);

    [[nodiscard]] bool isEmbedded() const { return m_embedded; }
    /** Embedded mode: text of the button shown after a result ("Next question", "Finish"). */
    void setAdvanceText(const QString& text) { m_advanceText = text; }
    [[nodiscard]] bool isShowingResult() const { return m_showingResult; }
    [[nodiscard]] bool isFadingOut() const { return m_fadingOut; }
    [[nodiscard]] Phase phase() const { return m_phase; }
    [[nodiscard]] Placement placement() const { return m_placement; }
    [[nodiscard]] const Question& question() const { return m_question; }

    // Accessors for tests and accessibility checks
    [[nodiscard]] QPlainTextEdit* writtenEdit() const { return m_writtenEdit; }
    [[nodiscard]] QPushButton* submitButton() const { return m_submitBtn; }
    [[nodiscard]] QPushButton* skipButton() const { return m_skipBtn; }
    [[nodiscard]] QPushButton* laterButton() const { return m_laterBtn; }
    [[nodiscard]] QPushButton* revealButton() const { return m_revealBtn; }
    [[nodiscard]] QPushButton* useSuggestionButton() const { return m_useSuggestionBtn; }
    [[nodiscard]] QPushButton* keepAnswerButton() const { return m_keepAnswerBtn; }
    [[nodiscard]] QPushButton* selfAssessButton(SelfRating rating) const;
    [[nodiscard]] QPushButton* closeButton() const;
    [[nodiscard]] QLabel* resultLabel() const { return m_resultLabel; }
    [[nodiscard]] QLabel* feedbackLabel() const { return m_feedbackLabel; }
    [[nodiscard]] QLabel* expectedLabel() const { return m_expectedLabel; }
    [[nodiscard]] QLabel* sourceLabel() const { return m_sourceLabel; }
    [[nodiscard]] QLabel* suggestionLabel() const { return m_suggestionLabel; }
    [[nodiscard]] QLabel* hintLabel() const { return m_hintLabel; }
    [[nodiscard]] QScrollArea* scrollArea() const { return m_scroll; }
    [[nodiscard]] QList<PixelOptionButton*> optionButtons() const { return m_options; }
    [[nodiscard]] int remainingDismissMs() const;

signals:
    void choiceSubmitted(const QString& optionId);
    void selectionSubmitted(const QStringList& optionIds);
    void writtenSubmitted(const QString& answer, bool allowSuggestion);
    void revealRequested();
    void selfAssessed(int rating);   // SelfRating value
    void postponeRequested();
    void noticeActionTriggered();
    void skipRequested();
    void closed();
    void buttonActivated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void engage();
    void submit();
    void closeAsSkip();
    void reposition(const QRect& petGlobalGeometry);
    void fitToContent(const QRect& available);
    void present(const QRect& petGlobalGeometry);
    void startFadeIn(const QPoint& finalPos);
    void startFadeOut();
    void finishFadeOut();
    void startAutoHide(int ms);
    void clearOptions();
    void updateSubmitEnabled();
    void setResultText(const QString& text, const QColor& color, bool displayFont);
    void setSourceText();
    /** Written answers need four buttons: put Reveal and Check on their own row. */
    void placeActionButtons(bool twoRows);
    [[nodiscard]] QMargins bubbleMargins() const;

    // Header
    QLabel* m_categoryLabel = nullptr;
    PixelIconButton* m_closeBtn = nullptr;
    // Scrollable body
    QScrollArea* m_scroll = nullptr;
    QWidget* m_body = nullptr;
    QLabel* m_promptLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QWidget* m_optionsBox = nullptr;
    QVBoxLayout* m_optionsLayout = nullptr;
    QList<PixelOptionButton*> m_options;
    QPlainTextEdit* m_writtenEdit = nullptr;
    QWidget* m_suggestionRow = nullptr;
    QLabel* m_suggestionLabel = nullptr;
    QPushButton* m_useSuggestionBtn = nullptr;
    QPushButton* m_keepAnswerBtn = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QLabel* m_expectedLabel = nullptr;
    QLabel* m_explanationLabel = nullptr;
    QLabel* m_sourceLabel = nullptr;
    // Actions
    QWidget* m_actionRow = nullptr;
    QHBoxLayout* m_actionTop = nullptr;
    QHBoxLayout* m_actionBottom = nullptr;
    QPushButton* m_laterBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
    QPushButton* m_revealBtn = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QWidget* m_selfRow = nullptr;
    QPushButton* m_needReviewBtn = nullptr;
    QPushButton* m_partlyBtn = nullptr;
    QPushButton* m_gotItBtn = nullptr;

    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    QPropertyAnimation* m_slide = nullptr;

    Question m_question;
    QString m_suggestion;
    QString m_originalWritten;
    QTimer m_autoHide;
    QElapsedTimer m_remainingClock;
    int m_remainingMs = kStudyResultDisplayMs;
    QRect m_lastPetRect;
    Placement m_placement = Placement::Above;
    Phase m_phase = Phase::Hidden;
    bool m_showingResult = false;
    bool m_fadingOut = false;
    bool m_hovered = false;
    bool m_embedded = false;
    QString m_advanceText = QStringLiteral("Done");
};
