#include "study/QuestionValidator.h"

#include <QRegularExpression>
#include <QSet>

namespace QuestionValidator {
namespace {

QStringList words(const QString& normalized)
{
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool isCatchAllOption(const QString& text)
{
    const QString n = normalizeForMatch(text);
    return n.contains(QStringLiteral("all of the above")) || n.contains(QStringLiteral("none of the above"))
        || n.contains(QStringLiteral("both a and")) || n == QStringLiteral("all of these")
        || n == QStringLiteral("none of these");
}

Result fail(const QString& reason)
{
    return {false, reason};
}

Result validateOptions(const Question& q)
{
    if (q.options.size() != 4) {
        return fail(QStringLiteral("needs exactly 4 options"));
    }
    QSet<QString> seen;
    int correct = 0;
    qsizetype longestWrong = 0;
    qsizetype longestRight = 0;
    for (const QuestionOption& o : q.options) {
        const QString n = normalizeForMatch(o.text);
        if (n.isEmpty()) {
            return fail(QStringLiteral("empty option"));
        }
        if (seen.contains(n)) {
            return fail(QStringLiteral("duplicate options"));
        }
        seen.insert(n);
        if (isCatchAllOption(o.text)) {
            return fail(QStringLiteral("catch-all option"));
        }
        if (o.correct) {
            ++correct;
            longestRight = std::max(longestRight, o.text.trimmed().size());
        } else {
            longestWrong = std::max(longestWrong, o.text.trimmed().size());
        }
    }
    if (q.type == QuestionType::SingleChoice) {
        if (correct != 1) {
            return fail(QStringLiteral("single choice needs exactly one correct option"));
        }
        // A correct answer far longer than every distractor gives the answer away.
        if (longestRight > 2 * std::max<qsizetype>(longestWrong, 12)) {
            return fail(QStringLiteral("correct option is conspicuously longer"));
        }
    } else {
        if (correct < 2 || correct > 3) {
            return fail(QStringLiteral("multi-select needs two or three correct options"));
        }
        for (const QuestionOption& o : q.options) {
            if (o.explanation.trimmed().isEmpty()) {
                return fail(QStringLiteral("multi-select options need explanations"));
            }
        }
    }
    return {true, QString()};
}

} // namespace

QString normalizeForMatch(const QString& text)
{
    QString t = text.normalized(QString::NormalizationForm_KC).toLower();
    static const QRegularExpression nonWord(QStringLiteral("[^\\p{L}\\p{N}]+"));
    t.replace(nonWord, QStringLiteral(" "));
    return t.simplified();
}

bool evidenceSupported(const QString& evidence, const QString& source)
{
    const QString e = normalizeForMatch(evidence);
    const QString s = normalizeForMatch(source);
    const QStringList ew = words(e);
    if (ew.size() < 4 || s.isEmpty()) {
        return false;
    }
    if ((QLatin1Char(' ') + s + QLatin1Char(' ')).contains(QLatin1Char(' ') + e + QLatin1Char(' '))) {
        return true;
    }
    if (ew.size() < 6) {
        return false;
    }
    const QStringList sw = words(s);
    QSet<QString> sourceGrams;
    for (qsizetype i = 0; i + 3 < sw.size(); ++i) {
        sourceGrams.insert(sw.mid(i, 4).join(QLatin1Char(' ')));
    }
    int hits = 0;
    const qsizetype total = ew.size() - 3;
    for (qsizetype i = 0; i + 3 < ew.size(); ++i) {
        if (sourceGrams.contains(ew.mid(i, 4).join(QLatin1Char(' ')))) {
            ++hits;
        }
    }
    return hits * 10 >= total * 7;
}

Result validate(const Question& q, const QString& sourceText, const QStringList& existingConcepts,
                const QStringList& existingPrompts)
{
    if (q.type == QuestionType::OpenTrivia) {
        return fail(QStringLiteral("not a document question"));
    }
    if (q.prompt.trimmed().size() < 10) {
        return fail(QStringLiteral("question text too short"));
    }
    if (q.explanation.trimmed().isEmpty()) {
        return fail(QStringLiteral("missing explanation"));
    }
    if (q.topic.trimmed().isEmpty()) {
        return fail(QStringLiteral("missing concept"));
    }
    if (q.sources.isEmpty()) {
        return fail(QStringLiteral("missing evidence"));
    }
    const QString evidence = q.sources.first().excerpt.trimmed();
    if (evidence.size() < 15 || evidence.size() > 800) {
        return fail(QStringLiteral("evidence length out of range"));
    }
    if (!evidenceSupported(evidence, sourceText)) {
        return fail(QStringLiteral("evidence is not quoted from the source"));
    }

    const QString topic = normalizeForMatch(q.topic);
    for (const QString& c : existingConcepts) {
        if (normalizeForMatch(c) == topic) {
            return fail(QStringLiteral("concept already covered"));
        }
    }
    const QString prompt = normalizeForMatch(q.prompt);
    for (const QString& p : existingPrompts) {
        if (normalizeForMatch(p) == prompt) {
            return fail(QStringLiteral("duplicate question"));
        }
    }

    switch (q.type) {
    case QuestionType::SingleChoice:
    case QuestionType::MultiSelect:
        return validateOptions(q);
    case QuestionType::Written: {
        if (q.expectedAnswer.trimmed().isEmpty()) {
            return fail(QStringLiteral("missing expected answer"));
        }
        if (q.keyConcepts.isEmpty() || q.keyConcepts.size() > 5) {
            return fail(QStringLiteral("needs one to five key concepts"));
        }
        for (const QString& k : q.keyConcepts) {
            if (k.trimmed().isEmpty()) {
                return fail(QStringLiteral("empty key concept"));
            }
        }
        // The expected answer must not just restate the question.
        if (normalizeForMatch(q.expectedAnswer) == prompt) {
            return fail(QStringLiteral("expected answer repeats the question"));
        }
        return {true, QString()};
    }
    case QuestionType::OpenTrivia:
        break;
    }
    return fail(QStringLiteral("unknown question type"));
}

} // namespace QuestionValidator
