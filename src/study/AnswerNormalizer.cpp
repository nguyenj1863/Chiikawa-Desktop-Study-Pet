#include "study/AnswerNormalizer.h"

#include <QLatin1StringView>

namespace AnswerNormalizer {

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

} // namespace AnswerNormalizer
