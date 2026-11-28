#pragma once

#include "study/StudyTypes.h"

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

/**
 * A figure, table or data snippet shown with a question. Questions that depend on a visual
 * must carry it, so the student always sees what the question refers to.
 */
struct SupportRef {
    SupportKind kind = SupportKind::Image;
    qint64 assetId = 0;          // stored asset (images and tables); 0 for an inline data snippet
    QString imagePath;
    QString caption;
    TableData table;
    QString text;                // data snippet, or the text read from the image
    QString interpretation;      // what the vision model saw in the image, if anything
};

/** The reference a question keeps to a stored asset (with everything needed to show it). */
inline SupportRef supportRefFor(const SupportAsset& asset)
{
    SupportRef r;
    r.kind = asset.kind;
    r.assetId = asset.id;
    r.imagePath = asset.path;
    r.caption = asset.caption;
    r.table = asset.table;
    r.text = asset.text;
    r.interpretation = asset.interpretation;
    return r;
}

/** Where a question came from. Values are persisted; append only. */
enum class QuestionOrigin {
    Generated = 0,
    Written = 1,   // written by the student
    Edited = 2     // generated, then edited by the student
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
    qint64 chunkId = 0;                // source passage of a generated question (0 if none)
    QVector<SupportRef> support;       // visuals shown with the question
    QuestionOrigin origin = QuestionOrigin::Generated;
    qint64 editedAt = 0;               // epoch ms of the student's last edit
    bool evidenceFromVisual = false;   // the evidence quotes a table or figure, not the passage

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
