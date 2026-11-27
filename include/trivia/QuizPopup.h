#pragma once

#include "trivia/TriviaTypes.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QLineEdit;
class QPropertyAnimation;
class QPushButton;
class PixelIconButton;

/**
 * Compact smoked-glass speech bubble that shows one check-in question near the pet.
 * It appears without activating or taking keyboard focus; the answer field only
 * takes focus after the user deliberately clicks the popup. Provider-agnostic:
 * it only consumes Question and AnswerResult.
 */
class QuizPopup : public QWidget {
    Q_OBJECT

public:
    static constexpr int kWidth = 280;
    static constexpr int kPetGapPx = 8;
    static constexpr int kTailHeight = 7;
    static constexpr int kFadeInMs = 180;
    static constexpr int kFadeOutMs = 200;
    static constexpr int kEntranceSlidePx = 6;
    static constexpr int kResultDisplayMs = 6000;

    /** Where the bubble sits relative to the pet; the tail points back toward it. */
    enum class Placement {
        Above,
        Below,
        Left,
        Right
    };

    struct Layout {
        QPoint pos;
        Placement placement = Placement::Above;
    };

    explicit QuizPopup(QWidget* parent = nullptr);
    ~QuizPopup() override;

    void showQuestion(const Question& question, const QRect& petGlobalGeometry);
    void showResult(const AnswerResult& result, const QString& explanation);

    /** Keeps the bubble beside the pet while it moves (no animation). */
    void followPet(const QRect& petGlobalGeometry);

    /** Fades out and hides; emits closed() once hidden. */
    void dismiss();

    /** Pure placement helper: prefers above, then sides, then below; always inside available. */
    [[nodiscard]] static Layout calculateLayout(const QRect& petRect,
                                                const QSize& popupSize,
                                                const QRect& available,
                                                int gap = kPetGapPx);

    [[nodiscard]] bool isShowingResult() const { return m_showingResult; }
    [[nodiscard]] bool isFadingOut() const { return m_fadingOut; }
    [[nodiscard]] Placement placement() const { return m_placement; }
    [[nodiscard]] QLineEdit* answerEdit() const { return m_answerEdit; }
    [[nodiscard]] QPushButton* submitButton() const { return m_submitBtn; }
    [[nodiscard]] QPushButton* skipButton() const { return m_skipBtn; }
    [[nodiscard]] QPushButton* closeButton() const;
    [[nodiscard]] QLabel* resultLabel() const { return m_resultLabel; }
    [[nodiscard]] int remainingDismissMs() const;

signals:
    void answerSubmitted(const QString& answer);
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
    void reposition(const QRect& petGlobalGeometry);
    void startFadeIn(const QPoint& finalPos);
    void startFadeOut();
    void finishFadeOut();
    [[nodiscard]] QMargins bubbleMargins() const;

    QLabel* m_categoryLabel = nullptr;
    QLabel* m_promptLabel = nullptr;
    QLineEdit* m_answerEdit = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_explanationLabel = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
    PixelIconButton* m_closeBtn = nullptr;
    QWidget* m_answerRow = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    QPropertyAnimation* m_slide = nullptr;

    QTimer m_autoHide;
    QElapsedTimer m_remainingClock;
    int m_remainingMs = kResultDisplayMs;
    QRect m_lastPetRect;
    Placement m_placement = Placement::Above;
    bool m_showingResult = false;
    bool m_fadingOut = false;
    bool m_hovered = false;
};
