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
 * Reference to where a question came from. Unused by offline trivia; reserved
 * for document-grounded questions (file title, page/slide, quoted excerpt).
 */
struct SourceRef {
    QString title;
    QString location;
    QString excerpt;
};

/**
 * Provider-agnostic question model shared by the scheduler, controller, and popup.
 */
struct Question {
    QString id;
    QString prompt;
    QString category;
    QString explanation;
    QString providerId;
    QVector<AcceptedAnswer> answers;
    QVector<SourceRef> sources;

    [[nodiscard]] bool isValid() const { return !id.isEmpty() && !prompt.isEmpty() && !answers.isEmpty(); }
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
