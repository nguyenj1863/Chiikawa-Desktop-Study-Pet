#pragma once

#include "trivia/QuestionProvider.h"

#include <QRandomGenerator>
#include <QVector>

/**
 * Offline curated trivia loaded from bundled JSON (schema version 1).
 * Malformed entries and duplicate IDs are skipped.
 */
class TriviaQuestionProvider : public QuestionProvider {
public:
    static constexpr auto kDefaultPath = ":/assets/trivia/questions.json";

    explicit TriviaQuestionProvider(const QString& jsonPath = QString::fromLatin1(kDefaultPath));

    [[nodiscard]] QString id() const override { return QStringLiteral("trivia"); }
    [[nodiscard]] std::optional<Question> nextQuestion(const QStringList& recentIds) override;

    /** Parses question JSON; exposed for tests. */
    [[nodiscard]] static QVector<Question> parse(const QByteArray& json);

    [[nodiscard]] const QVector<Question>& questions() const { return m_questions; }
    void setSeed(quint32 seed) { m_rng.seed(seed); }

private:
    QVector<Question> m_questions;
    QRandomGenerator m_rng;
};
