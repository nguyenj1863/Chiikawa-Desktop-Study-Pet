#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

/** An accepted short answer and the alternative spellings that also match it. */
struct AcceptedAnswer {
    QString canonical;
    QStringList aliases;
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
 * Question model shared by the practice session, check-in controller, and popup.
 */
struct Question {
    QString id;
    QString prompt;
    QString category;
    QString explanation;
    QString providerId;
    QVector<AcceptedAnswer> answers;   // short-answer aliases for Written
    QVector<SourceRef> sources;

    QuestionType type = QuestionType::SingleChoice;
    Difficulty difficulty = Difficulty::Medium;
    QString topic;                   // the idea being tested, used for progress tracking
    QVector<QuestionOption> options;   // SingleChoice / MultiSelect
    QString expectedAnswer;            // Written
    QStringList keyConcepts;           // Written: points a correct answer must cover
    qint64 setId = 0;
    qint64 documentId = 0;

    [[nodiscard]] bool isValid() const
    {
        if (id.isEmpty() || prompt.isEmpty()) {
            return false;
        }
        switch (type) {
        case QuestionType::SingleChoice:
        case QuestionType::MultiSelect:
            return options.size() >= 2;
        case QuestionType::Written:
            return !expectedAnswer.isEmpty();
        }
        return false;
    }

};

Q_DECLARE_METATYPE(Question)
