#pragma once

#include "study/Question.h"

#include <QStringList>
#include <optional>

/**
 * Source of check-in questions. StudyQuestionProvider implements
 * this today; a document-grounded provider can implement it later and plug into
 * QuizController without changing the popup or scheduler.
 */
class QuestionProvider {
public:
    virtual ~QuestionProvider() = default;

    [[nodiscard]] virtual QString id() const = 0;

    /** Returns a question, avoiding recentIds when possible; nullopt when none are available. */
    [[nodiscard]] virtual std::optional<Question> nextQuestion(const QStringList& recentIds) = 0;
};
