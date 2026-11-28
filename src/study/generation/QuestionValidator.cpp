#include "study/generation/QuestionValidator.h"

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

/** Everything the student reads: the question, its options, and the expected answer. */
QStringList questionTexts(const Question& q)
{
    QStringList texts = {q.prompt};
    for (const QuestionOption& o : q.options) {
        texts.append(o.text);
    }
    texts.append(q.expectedAnswer);
    return texts;
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

bool referencesVisual(const Question& q)
{
    static const QRegularExpression visualRef(
        QStringLiteral(
            // "the figure", "this diagram", "the graph above" ...
            R"(\b(?:the|this|these|following|above|below|provided|given|attached|accompanying)\s+(?:figure|fig\.|diagram|chart|graph|illustration|image|picture|plot|screenshot|photo|drawing|map)s?\b)"
            // tables and data only with a pointing word: "the table below", "the provided data"
            R"(|\b(?:this|following|above|below|provided|given|attached|accompanying)\s+(?:table|data|dataset|values|numbers)\b)"
            R"(|\b(?:table|data)\s+(?:above|below|shown)\b)"
            // reading instructions: "according to the table", "using the data", "look at the chart"
            R"(|\b(?:according\s+to|based\s+on|using|refer\s+to|look\s+at)\s+the\s+(?:table|data|dataset|values)\s*(?:[,?.]|$|\b(?:above|below|shown|provided|given|what|which|how|who|where|when|why)\b))"
            // numbered references: "Figure 3", "Table 2", "Fig. 4"
            R"(|\b(?:figure|fig\.|table|chart|exhibit|diagram|graph)\s*\d+)"
            // "shown above", "as illustrated below"
            R"(|\b(?:shown|depicted|illustrated|plotted|pictured|displayed|labell?ed)\s+(?:above|below|here|in\s+the\s+(?:figure|diagram|image|chart|graph|picture))\b)"
            // unseen data: "the observed data", "the measured values", "the results above"
            R"(|\b(?:the|these|those)\s+(?:observed|experimental|collected|measured|recorded|sample|reported|given|following|provided|above)\s+(?:data|results?|values|measurements|readings|numbers|observations|counts)\b)"
            R"(|\bthe\s+results\s+(?:above|below|shown|obtained|recorded)\b)"
            // an unnamed study: "in the experiment", "from the survey" ("the Milgram experiment" is fine)
            R"(|\b(?:in|from|during)\s+the\s+(?:experiment|survey|trial|case\s+study|lab|study)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& t : questionTexts(q)) {
        if (visualRef.match(t).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool referencesUnseenSource(const Question& q)
{
    static const QRegularExpression sourceRef(
        QStringLiteral(
            // "your notes", "the provided slides", "my lecture"
            R"(\b(?:your|my|our|these|the\s+(?:provided|given|assigned|attached|accompanying))\s+(?:class\s+|course\s+|lecture\s+|study\s+)?)"
            R"((?:notes|lectures?|readings?|slides?|handouts?|materials?|textbook|documents?|passages?|text|course|lesson)\b)"
            // "according to the notes", "as stated in the text"
            R"(|\b(?:according\s+to|based\s+on|as\s+(?:stated|mentioned|described|shown|discussed|explained|noted)\s+in)\s+the\s+)"
            R"((?:class\s+|course\s+|lecture\s+|study\s+)?(?:notes|lecture|slides?|handout|passage|text|materials?|reading|lesson)\b)"
            // "in the lecture", "from the slides" ("in the text field", "in the reading frame" are subject words)
            R"(|\b(?:in|from)\s+the\s+(?:class\s+|course\s+|lecture\s+|study\s+)?(?:notes|lecture|slides|handout|passage|lesson)\b)"
            // "the study material", "the course materials"
            R"(|\bthe\s+(?:study|course|class|lecture)\s+(?:materials?|notes|slides)\b)"
            // "the provided derivations", "the above example", "the listed problems"
            R"(|\b(?:the|these|those)\s+(?:provided|above|accompanying|attached|listed|aforementioned)\s+\w+)"
            // "What are the examples of undecidable problems listed?"
            R"(|\b(?:listed|mentioned|provided|described|shown|presented)\s*[?.]?\s*$)"
            // "as mentioned earlier", "as discussed in class"
            R"(|\bas\s+(?:stated|mentioned|described|discussed|explained|noted|shown)\s+(?:earlier|above|before|previously|in\s+class)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& t : questionTexts(q)) {
        if (sourceRef.match(t).hasMatch()) {
            return true;
        }
    }
    return false;
}

QString contextProblem(const Question& q)
{
    if (referencesUnseenSource(q)) {
        return QString::fromLatin1(kVagueSourceReason);
    }
    if (q.support.isEmpty() && referencesVisual(q)) {
        return QString::fromLatin1(kUnseenVisualReason);
    }
    // "Calculate the percentage…" with no numbers anywhere the student can see.
    static const QRegularExpression askForValue(
        QStringLiteral(R"(\b(?:calculate|compute|work\s+out)\b)"
                       R"(|\bthe\s+correct\s+(?:percentage|value|number|amount|ratio|rate|proportion)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression digit(QStringLiteral(R"(\d)"));
    if (q.support.isEmpty() && askForValue.match(q.prompt).hasMatch() && !digit.match(q.prompt).hasMatch()) {
        QStringList answers = {q.expectedAnswer};
        for (const QuestionOption& o : q.options) {
            if (o.correct) {
                answers.append(o.text);
            }
        }
        for (const QString& a : std::as_const(answers)) {
            if (digit.match(a).hasMatch()) {
                return QString::fromLatin1(kMissingInputsReason);
            }
        }
    }
    return QString();
}

bool isContextReason(const QString& reason)
{
    return reason == QLatin1String(kUnseenVisualReason) || reason == QLatin1String(kVagueSourceReason)
        || reason == QLatin1String(kMissingInputsReason);
}

bool needsContext(const Question& q)
{
    return q.origin == QuestionOrigin::Generated && !contextProblem(q).isEmpty();
}

QVector<Question> answerable(const QVector<Question>& questions)
{
    QVector<Question> out;
    out.reserve(questions.size());
    for (const Question& q : questions) {
        if (!needsContext(q)) {
            out.append(q);
        }
    }
    return out;
}

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

bool dataQuoted(const QString& data, const QString& source)
{
    if (!evidenceSupported(data, source)) {
        return false;
    }
    static const QRegularExpression number(QStringLiteral(R"(\d+(?:[.,]\d+)*)"));
    QSet<QString> known;
    for (auto it = number.globalMatch(source); it.hasNext();) {
        known.insert(it.next().captured());
    }
    for (auto it = number.globalMatch(data); it.hasNext();) {
        if (!known.contains(it.next().captured())) {
            return false;
        }
    }
    return true;
}

Result validate(const Question& q, const QString& sourceText, const QStringList& existingConcepts,
                const QStringList& existingPrompts)
{
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
    // Never ask about something the student cannot see: a question about a visual must carry it.
    if (const QString problem = contextProblem(q); !problem.isEmpty()) {
        return fail(problem);
    }
    for (const SupportRef& s : q.support) {
        const bool showable = s.kind == SupportKind::Image ? !s.imagePath.isEmpty()
                            : s.kind == SupportKind::Table ? !s.table.rows.isEmpty()
                                                           : !s.text.trimmed().isEmpty();
        if (!showable) {
            return fail(QString::fromLatin1(kUnseenVisualReason));
        }
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
    }
    return fail(QStringLiteral("unknown question type"));
}

} // namespace QuestionValidator
