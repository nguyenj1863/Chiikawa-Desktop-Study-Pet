#include "trivia/AnswerEvaluator.h"

#include <QLatin1StringView>

namespace AnswerEvaluator {

QString normalize(const QString& text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    bool pendingSpace = false;
    for (const QChar ch : decomposed) {
        if (ch.category() == QChar::Mark_NonSpacing) {
            continue;
        }
        // '+' and '#' stay significant so "C", "C++", and "C#" remain distinct.
        if (ch.isLetterOrNumber() || ch == QLatin1Char('+') || ch == QLatin1Char('#')) {
            if (pendingSpace && !out.isEmpty()) {
                out.append(QLatin1Char(' '));
            }
            pendingSpace = false;
            out.append(ch.toCaseFolded());
        } else if (ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char('/')) {
            pendingSpace = true;
        }
        // Other punctuation (periods, commas, apostrophes) is dropped without a gap.
    }

    static const QLatin1StringView kArticles[] = {
        QLatin1StringView("the "), QLatin1StringView("a "), QLatin1StringView("an ")};
    for (const QLatin1StringView article : kArticles) {
        if (out.startsWith(article) && out.size() > article.size()) {
            out.remove(0, article.size());
            break;
        }
    }
    return out;
}

AnswerResult evaluate(const Question& question, const QString& input, const QStringList& alreadyGiven)
{
    AnswerResult result;
    const QString needle = normalize(input);
    if (needle.isEmpty()) {
        result.empty = true;
        return result;
    }

    for (const AcceptedAnswer& answer : question.answers) {
        bool match = normalize(answer.canonical) == needle;
        for (qsizetype i = 0; !match && i < answer.aliases.size(); ++i) {
            match = normalize(answer.aliases.at(i)) == needle;
        }
        if (!match) {
            continue;
        }
        result.matchedCanonical = answer.canonical;
        result.rarity = answer.rarity;
        if (alreadyGiven.contains(answer.canonical)) {
            result.duplicate = true;
            result.points = 0;
        } else {
            result.points = Trivia::pointsFor(answer.rarity);
        }
        return result;
    }
    return result;
}

} // namespace AnswerEvaluator
