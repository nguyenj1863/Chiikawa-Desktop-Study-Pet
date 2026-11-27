#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Rarity rank of an accepted trivia answer. Ranks are curated by hand in the
 * question data; they are not measured player statistics.
 */
enum class Rarity {
    Common,
    Uncommon,
    Rare,
    Legendary,
    Incorrect
};

namespace Trivia {

[[nodiscard]] constexpr int pointsFor(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return 10;
    case Rarity::Uncommon:
        return 30;
    case Rarity::Rare:
        return 60;
    case Rarity::Legendary:
        return 100;
    case Rarity::Incorrect:
        return 0;
    }
    return 0;
}

[[nodiscard]] QString rarityName(Rarity rarity);

/** Parses a lowercase rarity name from question data; returns false when unknown. */
[[nodiscard]] bool parseRarity(const QString& name, Rarity* out);

} // namespace Trivia

/** One accepted answer and the alternative spellings that also match it. */
struct AcceptedAnswer {
    QString canonical;
    QStringList aliases;
    Rarity rarity = Rarity::Common;
};

/**
 * Where a question came from: the document title, a human-readable location
 * ("p. 5", "slide 3"), and the supporting passage quoted from the source.
 */
struct SourceRef {
    QString title;
    QString location;
    QString excerpt;
    qint64 documentId = 0;
    qint64 chunkId = 0;
};

/** Answer format. Values are persisted; append only. */
enum class QuestionType {
    OpenTrivia = 0,     // general-knowledge trivia scored by rarity
    SingleChoice = 1,   // four options, exactly one correct
    MultiSelect = 2,    // four options, two or three correct
    Written = 3         // free-text answer checked against key concepts
};

enum class Difficulty {
    Easy = 0,
    Medium = 1,
    Hard = 2
};

/** One choice in a single-choice or multi-select question, with why it is right or wrong. */
struct QuestionOption {
    QString id;            // stable within the question ("a".."d" as generated, never shown)
    QString text;
    bool correct = false;
    QString explanation;
};

/**
 * Provider-agnostic question model shared by the scheduler, controller, and popup.
 * Trivia uses answers/rarity; document questions use the typed fields below.
 */
struct Question {
    QString id;
    QString prompt;
    QString category;
    QString explanation;
    QString providerId;
    QVector<AcceptedAnswer> answers;   // trivia answers, or short-answer aliases for Written
    QVector<SourceRef> sources;

    QuestionType type = QuestionType::OpenTrivia;
    Difficulty difficulty = Difficulty::Medium;
    QString topic;                   // the idea being tested, used for progress tracking
    QVector<QuestionOption> options;   // SingleChoice / MultiSelect
    QString expectedAnswer;            // Written
    QStringList keyConcepts;           // Written: points a correct answer must cover
    qint64 courseId = 0;
    qint64 documentId = 0;

    [[nodiscard]] bool isValid() const
    {
        if (id.isEmpty() || prompt.isEmpty()) {
            return false;
        }
        switch (type) {
        case QuestionType::OpenTrivia:
            return !answers.isEmpty();
        case QuestionType::SingleChoice:
        case QuestionType::MultiSelect:
            return options.size() >= 2;
        case QuestionType::Written:
            return !expectedAnswer.isEmpty();
        }
        return false;
    }

    [[nodiscard]] bool isDocumentQuestion() const { return type != QuestionType::OpenTrivia; }
};

/**
 * Outcome of evaluating one typed answer. This is rarity scoring for open-ended
 * trivia, kept separate from any future educational correctness grading.
 */
struct AnswerResult {
    Rarity rarity = Rarity::Incorrect;
    int points = 0;
    QString matchedCanonical;
    bool duplicate = false;
    bool empty = false;

    [[nodiscard]] bool isCorrect() const { return rarity != Rarity::Incorrect; }
};

Q_DECLARE_METATYPE(Question)
Q_DECLARE_METATYPE(AnswerResult)
