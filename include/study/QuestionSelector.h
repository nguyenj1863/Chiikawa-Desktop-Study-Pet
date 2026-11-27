#pragma once

#include "study/QuestionProvider.h"
#include "study/Question.h"

#include <QList>
#include <QRandomGenerator>
#include <QSet>
#include <optional>

class ProgressTracker;
class StudyDatabase;

/** How check-ins choose among the enabled question formats. Values are persisted. */
enum class FormatMode {
    Mixed = 0,   // vary among every enabled format
    Focus = 1    // only the one chosen format
};

struct SelectionPrefs {
    QList<QuestionType> enabled = {QuestionType::SingleChoice, QuestionType::MultiSelect, QuestionType::Written};
    FormatMode mode = FormatMode::Mixed;
    QuestionType focus = QuestionType::SingleChoice;
};

/**
 * Picks the next study question. Pure logic with an injectable random generator.
 */
namespace QuestionSelector {

/** Formats in play: every enabled one (Mixed) or just the focus format (Focus, falling back to the first enabled). */
[[nodiscard]] QList<QuestionType> activeFormats(const SelectionPrefs& prefs);

/**
 * Chooses a format among those with questions available. In Mixed mode the format
 * used for the last two questions in a row is avoided when another is available.
 */
[[nodiscard]] std::optional<QuestionType> chooseFormat(const QList<QuestionType>& available,
                                                       const QList<QuestionType>& recentTypes,
                                                       QRandomGenerator& rng);

/**
 * Picks a question: skips recently asked IDs (falling back to the least recent when
 * everything was asked recently) and, most of the time, prefers topics flagged for
 * review (reviewKeys from ProgressTracker::reviewKeys).
 */
[[nodiscard]] std::optional<Question> pick(const QVector<Question>& candidates,
                                           const SelectionPrefs& prefs,
                                           const QStringList& recentIds,
                                           const QSet<QString>& reviewKeys,
                                           const QList<QuestionType>& recentTypes,
                                           QRandomGenerator& rng);

/** Shuffles option display order; option IDs (and so the answer mapping) are unchanged. */
[[nodiscard]] Question withShuffledOptions(Question question, QRandomGenerator& rng);

inline constexpr double kReviewPreference = 0.7;

} // namespace QuestionSelector

/**
 * QuestionProvider serving cached, pre-generated questions from the selected course.
 * No model call happens at ask time.
 */
class StudyQuestionProvider : public QuestionProvider {
public:
    StudyQuestionProvider(StudyDatabase* db, ProgressTracker* progress);

    void setCourse(qint64 courseId) { m_courseId = courseId; }
    [[nodiscard]] qint64 course() const { return m_courseId; }
    void setPreferences(const SelectionPrefs& prefs) { m_prefs = prefs; }
    [[nodiscard]] const SelectionPrefs& preferences() const { return m_prefs; }
    void setSeed(quint32 seed) { m_rng.seed(seed); }

    [[nodiscard]] QString id() const override { return QStringLiteral("study"); }
    [[nodiscard]] std::optional<Question> nextQuestion(const QStringList& recentIds) override;

    /** Questions currently available for the course and formats (ignores recency). */
    [[nodiscard]] int availableCount() const;

private:
    StudyDatabase* m_db = nullptr;
    ProgressTracker* m_progress = nullptr;
    qint64 m_courseId = 0;
    SelectionPrefs m_prefs;
    QList<QuestionType> m_recentTypes;
    QRandomGenerator m_rng;
};
