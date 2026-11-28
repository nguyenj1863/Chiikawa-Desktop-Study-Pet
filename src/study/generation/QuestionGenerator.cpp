#include "study/generation/QuestionGenerator.h"

#include "study/ingest/FigureInterpreter.h"
#include "study/generation/QuestionValidator.h"
#include "study/StudyDatabase.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcGenerator, "chiikawa.study.generator")

namespace {

constexpr int kGenerationTimeoutMs = 180000;   // small local models on CPU can be slow
constexpr int kGenerationMaxTokens = 900;

const char* const kSystemPrompt =
    "You are a careful tutor writing practice questions for a student. "
    "Use ONLY facts stated in the passage you are given; never add outside knowledge. "
    "\"evidence\" must be copied word for word from the passage and must contain the information "
    "needed to answer. Write the question like a normal exam question: ask it directly, and never list "
    "the answer options inside the question. The student never sees the passage, so the question must "
    "stand on its own: never write \"your notes\", \"the passage\", \"the text\", \"the lecture\", "
    "\"the slides\" or \"the material\"; name the subject instead. If answering needs numbers, values "
    "or measurements, write them into the question, or copy them word for word from the passage into "
    "\"data\" (shown to the student above the question); otherwise leave \"data\" empty. Never invent a value. "
    "Write \"explanation\" to the student in 1-2 sentences, explaining why the answer is right; "
    "do not describe the question itself, and state facts directly. Reply with JSON only.";

const char* const kContextRetryNote =
    "\nYour previous question depended on information the student cannot see. Write a question that "
    "includes every fact or number it needs (or puts the numbers in \"data\", or uses a listed visual).\n";

constexpr int kMaxDataChars = 400;

QByteArray stringProp(const char* name)
{
    return QByteArray("\"") + name + "\":{\"type\":\"string\"}";
}

QByteArray objectSchema(const QList<QByteArray>& props, const QList<QByteArray>& required)
{
    QByteArray req;
    for (const QByteArray& r : required) {
        req += (req.isEmpty() ? "" : ",") + QByteArray("\"") + r + "\"";
    }
    QByteArray body;
    for (const QByteArray& p : props) {
        body += (body.isEmpty() ? "" : ",") + p;
    }
    return "{\"type\":\"object\",\"properties\":{" + body + "},\"required\":[" + req + "]}";
}

QString difficultyName(Difficulty d)
{
    switch (d) {
    case Difficulty::Easy:
        return QStringLiteral("easy");
    case Difficulty::Medium:
        return QStringLiteral("medium");
    case Difficulty::Hard:
        return QStringLiteral("hard");
    }
    return QStringLiteral("medium");
}

/**
 * Small models sometimes repeat the options inside the question ("Which are true?
1. ...
2. ...").
 * Keep only the stem: cut at the first line that starts a numbered, lettered, or bulleted list.
 */
QString stripEmbeddedOptions(const QString& prompt)
{
    static const QRegularExpression listLine(QStringLiteral(R"(\n\s*(?:\d{1,2}[.)]|[A-Da-d][.)]|[-*\x{2022}])\s+)"));
    // The student never sees "the passage" or "your notes"; drop phrases that refer to them.
    static const QRegularExpression passageRef(
        QStringLiteral(R"(,?\s+(?:according to|based on|in|from) (?:the (?:provided |given )?(?:passage|text)|)"
                       R"((?:your|the) (?:class |course |lecture )?(?:notes|lecture|slides|material|materials|reading|document))\b)"),
        QRegularExpression::CaseInsensitiveOption);
    // ...and the same phrase opening the question: "According to your notes, what ..." -> "What ..."
    static const QRegularExpression leadingRef(
        QStringLiteral(R"(^(?:according to|based on|in|from) (?:the (?:provided |given )?(?:passage|text)|)"
                       R"((?:your|the) (?:class |course |lecture )?(?:notes|lecture|slides|material|materials|reading|document))\s*,\s*)"),
        QRegularExpression::CaseInsensitiveOption);
    QString stem = prompt;
    const QRegularExpressionMatch m = listLine.match(stem);
    if (m.hasMatch() && m.capturedStart() > 10) {
        stem = stem.left(m.capturedStart()).trimmed();
    }
    const QRegularExpressionMatch lead = leadingRef.match(stem);
    if (lead.hasMatch() && lead.capturedLength() < stem.size()) {
        stem = stem.mid(lead.capturedLength());
        stem[0] = stem.at(0).toUpper();
    }
    stem.replace(passageRef, QString());
    return stem.trimmed();
}

QString typeName(QuestionType t)
{
    switch (t) {
    case QuestionType::SingleChoice:
        return QStringLiteral("multiple choice");
    case QuestionType::MultiSelect:
        return QStringLiteral("multiple select");
    case QuestionType::Written:
        return QStringLiteral("written answer");
    }
    return QStringLiteral("question");
}

} // namespace

QuestionGenerator::QuestionGenerator(StudyDatabase* db, LlmClient* llm, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_llm(llm)
    , m_modelName(QStringLiteral("local"))
{
    connect(m_llm, &LlmClient::finished, this, &QuestionGenerator::onFinished);
    connect(m_llm, &LlmClient::failed, this, &QuestionGenerator::onFailed);
}

QuestionGenerator::~QuestionGenerator()
{
    if (m_running && m_llm && m_currentRequest) {
        disconnect(m_llm, nullptr, this, nullptr);
        m_llm->cancel(m_currentRequest);
    }
}

QString QuestionGenerator::describeVisual(const SupportAsset& asset)
{
    QStringList parts;
    if (!asset.caption.isEmpty()) {
        parts.append(asset.caption);
    }
    if (asset.kind == SupportKind::Table) {
        parts.append(StudyText::tableToText(asset.table));
    } else {
        if (!asset.interpretation.isEmpty()) {
            parts.append(asset.interpretation);
        }
        if (!asset.text.isEmpty()) {
            parts.append(QStringLiteral("Text in the image: %1").arg(asset.text.left(600)));
        }
    }
    return parts.join(QLatin1Char('\n'));
}

QVector<SupportAsset> QuestionGenerator::usableVisuals(const QVector<SupportAsset>& assets, const Chunk& chunk)
{
    QVector<SupportAsset> out;
    for (const SupportAsset& a : assets) {
        if (a.unitNumber != chunk.refNumber || !StudyText::sameUnit(a.unitKind, chunk.refKind)) {
            continue;
        }
        // A picture can only be asked about once a vision model has described it (and could read it).
        const bool usable = a.kind == SupportKind::Table ? !a.table.rows.isEmpty()
                                                         : a.interpreted && a.legible && !a.interpretation.trimmed().isEmpty();
        if (usable && out.size() < 4) {
            out.append(a);
        }
    }
    return out;
}

QByteArray QuestionGenerator::schemaFor(QuestionType type, int visualCount)
{
    const QByteArray difficulty = R"("difficulty":{"type":"string","enum":["easy","medium","hard"]})";
    const QByteArray visual = visualCount > 0
        ? QByteArray(R"("visual":{"type":"integer","minimum":0,"maximum":)") + QByteArray::number(visualCount) + "}"
        : QByteArray();
    const auto withVisual = [&](QByteArray schema) {
        // "visual" goes right after "concept": the model picks what it uses before quoting evidence.
        if (!visual.isEmpty()) {
            schema.replace(R"("concept":{"type":"string"},)", R"("concept":{"type":"string"},)" + visual + ",");
            schema.replace(R"("required":["concept",)", R"("required":["concept","visual",)");
            schema.replace(R"("required":["supported","concept",)", R"("required":["supported","concept","visual",)");
        }
        return schema;
    };
    return withVisual(baseSchemaFor(type, difficulty));
}

QByteArray QuestionGenerator::baseSchemaFor(QuestionType type, const QByteArray& difficulty)
{
    switch (type) {
    case QuestionType::SingleChoice:
    case QuestionType::MultiSelect: {
        const QByteArray option = objectSchema({stringProp("text"), R"("correct":{"type":"boolean"})", stringProp("why")},
                                               {"text", "correct", "why"});
        const QByteArray options = R"("options":{"type":"array","minItems":4,"maxItems":4,"items":)" + option + "}";
        QList<QByteArray> props = {stringProp("concept"), stringProp("evidence"), stringProp("data"), stringProp("question"),
                                   options, stringProp("explanation"), difficulty};
        QList<QByteArray> required = {"concept", "evidence", "data", "question", "options", "explanation", "difficulty"};
        if (type == QuestionType::MultiSelect) {
            props.prepend(R"("supported":{"type":"boolean"})");
            required.prepend("supported");
        }
        return objectSchema(props, required);
    }
    case QuestionType::Written:
        return objectSchema({stringProp("concept"), stringProp("evidence"), stringProp("data"), stringProp("question"),
                             stringProp("expected_answer"),
                             R"("key_points":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"string"}})",
                             R"("aliases":{"type":"array","maxItems":6,"items":{"type":"string"}})",
                             stringProp("explanation"), difficulty},
                            {"concept", "evidence", "data", "question", "expected_answer", "key_points", "aliases",
                             "explanation", "difficulty"});
    }
    return QByteArray();
}

LlmRequest QuestionGenerator::buildRequest(QuestionType type, const Chunk& chunk, const QString& documentTitle,
                                           const QStringList& avoidConcepts, const QVector<SupportAsset>& visuals,
                                           bool textOnly, bool contextRetry)
{
    QString task;
    switch (type) {
    case QuestionType::SingleChoice:
        task = QStringLiteral(
            "Write ONE multiple-choice question about an important idea in the passage.\n"
            "- Exactly 4 options. Exactly 1 option is correct according to the passage.\n"
            "- The 3 wrong options must be plausible but clearly wrong according to the passage.\n"
            "- Keep all options similar in length and style. Never use \"all of the above\" or \"none of the above\".\n"
            "- In \"why\", say in one short sentence why each option is right or wrong.");
        break;
    case QuestionType::MultiSelect:
        task = QStringLiteral(
            "Write ONE \"select all that apply\" question about the passage.\n"
            "- Exactly 4 statements. 2 or 3 are true according to the passage; the rest are false according to the passage.\n"
            "- Every statement must be clearly true or clearly false from the passage alone; avoid opinions and vague wording.\n"
            "- In \"why\", say in one short sentence why each statement is true or false.\n"
            "- If the passage cannot support at least 2 clearly true statements and 1 clearly false one, "
            "set \"supported\" to false (fill the other fields briefly).");
        break;
    case QuestionType::Written:
        task = QStringLiteral(
            "Write ONE short written-answer question the student can answer in a few words or 1-3 sentences.\n"
            "- \"expected_answer\" is a model answer taken from the passage.\n"
            "- \"key_points\" lists 1-4 essential ideas a correct answer must include.\n"
            "- \"aliases\" lists other acceptable names or spellings when the answer is a single term (otherwise empty).");
        break;
    }
    QString avoid;
    if (!avoidConcepts.isEmpty()) {
        avoid = QStringLiteral("\nDo not test these concepts again: %1.").arg(avoidConcepts.mid(0, 20).join(QStringLiteral("; ")));
    }
    // Visuals: the student sees the one a question uses, so questions may be about them. Anything
    // else that is not written in the passage must never be referred to.
    const QVector<SupportAsset> shown = textOnly ? QVector<SupportAsset>() : visuals;
    QString visualBlock;
    if (!shown.isEmpty()) {
        QStringList items;
        for (qsizetype i = 0; i < shown.size(); ++i) {
            const SupportAsset& a = shown.at(i);
            items.append(QStringLiteral("[%1] %2:\n%3")
                             .arg(i + 1)
                             .arg(a.kind == SupportKind::Table ? QStringLiteral("Table") : QStringLiteral("Figure"))
                             .arg(describeVisual(a).left(1500)));
        }
        visualBlock = QStringLiteral(
                          "\nVISUALS ON THE SAME %1 (the student sees the one your question uses, right under it):\n%2\n\n"
                          "- Prefer a question that needs a visual: something it shows that the passage does not state "
                          "(a label, a value, a step in a flow, a cell of a table, how parts connect). Set \"visual\" to its "
                          "number; \"evidence\" may then be quoted from the passage or from that visual's description, "
                          "and \"according to the passage\" above also means \"according to that visual\". "
                          "Refer to it as \"the table\" or \"the figure\".\n"
                          "- Only if no visual shows anything worth asking, set \"visual\" to 0 and never mention any "
                          "figure, diagram, chart, table, image or data.\n")
                          .arg(chunk.refKind == SegmentKind::Page ? QStringLiteral("PAGE")
                               : chunk.refKind == SegmentKind::Section ? QStringLiteral("SECTION") : QStringLiteral("SLIDE"),
                               items.join(QStringLiteral("\n")));
    } else {
        visualBlock = QStringLiteral(
            "\nThe student sees only your question, not the slides or pages: ask only about what the passage text states, "
            "and do not refer to any figure, diagram, chart, image or table. Numbers the question needs go in the "
            "question or in \"data\".\n");
    }
    if (contextRetry) {
        visualBlock += QString::fromLatin1(kContextRetryNote);
    }
    LlmRequest r;
    r.system = QString::fromLatin1(kSystemPrompt);
    r.prompt = QStringLiteral(
                   "Source: \"%1\", %2\n\nPASSAGE:\n\"\"\"\n%3\n\"\"\"\n%6\n%4\n"
                   "\"concept\" names the single idea being tested in a few words. "
                   "\"difficulty\" is easy for recall, medium for understanding, hard for applying or comparing ideas."
                   "%5")
                   .arg(documentTitle, StudyText::referenceLabel(chunk.refKind, chunk.refNumber),
                        chunk.text.left(2400), task, avoid, visualBlock);
    r.schema = schemaFor(type, static_cast<int>(shown.size()));
    r.temperature = 0.3;
    r.maxTokens = kGenerationMaxTokens;
    r.timeoutMs = kGenerationTimeoutMs;
    return r;
}

std::optional<Question> QuestionGenerator::parseResponse(QuestionType type, const QString& json, QString* error,
                                                         bool* unsupported, int* visual)
{
    if (unsupported) {
        *unsupported = false;
    }
    if (visual) {
        *visual = 0;
    }
    const auto fail = [error](const QString& message) -> std::optional<Question> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    // Tolerate code fences or stray text around the object.
    const qsizetype open = json.indexOf(QLatin1Char('{'));
    const qsizetype close = json.lastIndexOf(QLatin1Char('}'));
    if (open < 0 || close <= open) {
        return fail(QStringLiteral("reply is not JSON"));
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.mid(open, close - open + 1).toUtf8(), &parseError);
    if (!doc.isObject()) {
        return fail(QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject obj = doc.object();
    if (type == QuestionType::MultiSelect && obj.contains(QStringLiteral("supported"))
        && !obj.value(QStringLiteral("supported")).toBool(true)) {
        if (unsupported) {
            *unsupported = true;
        }
        return fail(QStringLiteral("passage does not support a multi-select question"));
    }

    if (visual) {
        *visual = std::max(0, obj.value(QStringLiteral("visual")).toInt(0));
    }
    Question q;
    q.type = type;
    q.providerId = QStringLiteral("study");
    q.prompt = stripEmbeddedOptions(obj.value(QStringLiteral("question")).toString().trimmed());
    q.topic = obj.value(QStringLiteral("concept")).toString().trimmed().left(80);
    q.explanation = obj.value(QStringLiteral("explanation")).toString().trimmed();
    const QString difficulty = obj.value(QStringLiteral("difficulty")).toString().trimmed().toLower();
    if (difficulty == QStringLiteral("easy")) {
        q.difficulty = Difficulty::Easy;
    } else if (difficulty == QStringLiteral("hard")) {
        q.difficulty = Difficulty::Hard;
    } else if (difficulty == QStringLiteral("medium")) {
        q.difficulty = Difficulty::Medium;
    } else {
        return fail(QStringLiteral("invalid difficulty"));
    }
    SourceRef source;
    source.excerpt = obj.value(QStringLiteral("evidence")).toString().trimmed();
    q.sources.append(source);
    // Numbers the question needs, shown above it. The generator checks they are quoted, not invented.
    const QString data = obj.value(QStringLiteral("data")).toString().trimmed();
    if (!data.isEmpty()) {
        SupportRef ref;
        ref.kind = SupportKind::Data;
        ref.text = data.left(kMaxDataChars);
        q.support.append(ref);
    }

    if (type == QuestionType::SingleChoice || type == QuestionType::MultiSelect) {
        const QJsonArray options = obj.value(QStringLiteral("options")).toArray();
        static const QString ids = QStringLiteral("abcdefgh");
        for (qsizetype i = 0; i < options.size() && i < ids.size(); ++i) {
            const QJsonObject o = options.at(i).toObject();
            QuestionOption option;
            option.id = ids.at(i);
            option.text = o.value(QStringLiteral("text")).toString().trimmed();
            option.correct = o.value(QStringLiteral("correct")).toBool();
            option.explanation = o.value(QStringLiteral("why")).toString().trimmed();
            q.options.append(option);
        }
    } else if (type == QuestionType::Written) {
        q.expectedAnswer = obj.value(QStringLiteral("expected_answer")).toString().trimmed();
        for (const QJsonValue& v : obj.value(QStringLiteral("key_points")).toArray()) {
            const QString point = v.toString().trimmed();
            if (!point.isEmpty()) {
                q.keyConcepts.append(point);
            }
        }
        QStringList aliases;
        for (const QJsonValue& v : obj.value(QStringLiteral("aliases")).toArray()) {
            const QString alias = v.toString().trimmed();
            if (!alias.isEmpty() && alias.compare(q.expectedAnswer, Qt::CaseInsensitive) != 0) {
                aliases.append(alias);
            }
        }
        q.answers.append({q.expectedAnswer, aliases});
    } else {
        return fail(QStringLiteral("unsupported type"));
    }
    if (q.prompt.isEmpty()) {
        return fail(QStringLiteral("missing question"));
    }
    return q;
}

QString QuestionGenerator::stableId(qint64 documentId, QuestionType type, const QString& prompt)
{
    const QByteArray key = QByteArray::number(documentId) + '|' + QByteArray::number(static_cast<int>(type)) + '|'
        + QuestionValidator::normalizeForMatch(prompt).toUtf8();
    const QByteArray hash = QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("d%1-%2").arg(documentId).arg(QString::fromLatin1(hash));
}

QVector<Chunk> QuestionGenerator::pickChunks(const QVector<Chunk>& chunks, const QSet<qint64>& used, int maxCount)
{
    QVector<Chunk> candidates;
    QVector<Chunk> shortOnes;
    for (const Chunk& c : chunks) {
        if (used.contains(c.id)) {
            continue;
        }
        (c.text.size() >= kMinChunkChars ? candidates : shortOnes).append(c);
    }
    if (candidates.isEmpty()) {
        candidates = shortOnes;
    }
    if (maxCount <= 0 || candidates.size() <= maxCount) {
        return candidates;
    }
    // Spread picks evenly so questions cover the whole document, not just its start.
    QVector<Chunk> out;
    out.reserve(maxCount);
    for (int i = 0; i < maxCount; ++i) {
        out.append(candidates.at(static_cast<qsizetype>(i) * candidates.size() / maxCount));
    }
    return out;
}

bool QuestionGenerator::start(const QList<qint64>& documentIds, const QList<QuestionType>& formats, bool regenerate)
{
    if (m_running || !m_db || !m_llm) {
        return false;
    }
    m_formats.clear();
    for (const QuestionType t : formats) {
        if (!m_formats.contains(t)) {
            m_formats.append(t);
        }
    }
    if (m_formats.isEmpty()) {
        return false;
    }
    m_jobs.clear();
    m_docs.clear();
    m_total = m_done = m_accepted = m_rejected = 0;
    m_figures.clear();
    m_figuresRead = m_figuresUnreadable = 0;
    m_figuresOnly = false;

    for (const qint64 docId : documentIds) {
        const auto doc = m_db->document(docId);
        if (!doc || doc->chunkCount == 0 || doc->kind == DocumentKind::Manual) {
            continue;   // nothing to generate from, and a student's own questions are never replaced
        }
        if (regenerate) {
            m_db->removeQuestions(docId);
        }
        DocState state;
        const QHash<int, double> scanned = m_db->ocrUnits(docId);
        for (auto it = scanned.cbegin(); it != scanned.cend(); ++it) {
            state.scannedUnits.insert(it.key());
        }
        const QVector<Question> existing = m_db->documentQuestions(docId);
        for (const Question& q : existing) {
            state.concepts.append(q.topic);
            state.prompts.append(q.prompt);
        }
        const QVector<Chunk> picks = pickChunks(m_db->chunks(docId), m_db->usedChunkIds(docId), kMaxQuestionsPerRun);
        const QVector<SupportAsset> assets = m_db->supportAssets(docId);
        for (qsizetype i = 0; i < picks.size(); ++i) {
            Job job;
            job.documentId = docId;
            job.setId = doc->setId;
            job.title = doc->title;
            job.chunk = picks.at(i);
            job.visuals = usableVisuals(assets, job.chunk);
            job.type = m_formats.at(static_cast<int>((existing.size() + i) % m_formats.size()));
            m_jobs.enqueue(job);
        }
        state.pending = static_cast<int>(picks.size());
        // Figures on the picked slides or pages are read first, so questions can be about them.
        queueFigures(docId, picks, doc->title, kMaxFiguresPerRun);
        if (state.pending > 0) {
            m_docs.insert(docId, state);
            m_db->setDocumentStatus(docId, DocumentStatus::Generating);
            emit documentStarted(docId);
        } else if (regenerate) {
            m_db->setDocumentStatus(docId, DocumentStatus::Extracted);
        }
    }
    m_total = static_cast<int>(m_jobs.size());
    if (m_total == 0) {
        m_figures.clear();
        return false;
    }
    m_total += static_cast<int>(m_figures.size());
    m_running = true;
    emit progress(0, m_total, QStringLiteral("Starting…"));
    submitNext();
    return true;
}

bool QuestionGenerator::regenerateQuestion(const QString& questionId)
{
    if (m_running || !m_db || !m_llm) {
        return false;
    }
    const auto old = m_db->question(questionId);
    if (!old || old->chunkId <= 0 || old->origin == QuestionOrigin::Written) {
        return false;
    }
    const auto chunk = m_db->chunk(old->chunkId);
    const auto doc = m_db->document(old->documentId);
    if (!chunk || !doc) {
        return false;
    }
    m_formats = {old->type};
    m_jobs.clear();
    m_docs.clear();
    m_total = m_done = m_accepted = m_rejected = 0;
    m_figures.clear();
    m_figuresOnly = false;

    // Other questions' concepts stay off-limits; the old wording must not come back unchanged.
    DocState state;
    for (const Question& q : m_db->documentQuestions(old->documentId)) {
        if (q.id != old->id) {
            state.concepts.append(q.topic);
        }
        state.prompts.append(q.prompt);
    }
    state.pending = 1;
    const QHash<int, double> scanned = m_db->ocrUnits(old->documentId);
    for (auto it = scanned.cbegin(); it != scanned.cend(); ++it) {
        state.scannedUnits.insert(it.key());
    }
    Job job;
    job.documentId = old->documentId;
    job.setId = old->setId;
    job.title = doc->title;
    job.chunk = *chunk;
    job.type = old->type;
    job.fellBack = true;   // keep the format the student had
    job.visuals = usableVisuals(m_db->supportAssets(old->documentId), job.chunk);
    job.replacing = old->id;
    m_jobs.enqueue(job);
    m_docs.insert(old->documentId, state);
    m_total = 1;
    m_running = true;
    emit documentStarted(old->documentId);
    emit progress(0, m_total, QStringLiteral("Starting…"));
    submitNext();
    return true;
}

void QuestionGenerator::submitNext()
{
    if (!m_running) {
        return;
    }
    if (!m_figures.isEmpty()) {
        submitFigure();
        return;
    }
    if (m_jobs.isEmpty()) {
        finishRun(QString());
        return;
    }
    m_current = m_jobs.dequeue();
    // Visuals are looked up now, so figures read earlier in this run can be used.
    m_current.visuals = usableVisuals(m_db->supportAssets(m_current.documentId), m_current.chunk);
    const DocState& state = m_docs[m_current.documentId];
    emit progress(m_done, m_total,
                  QStringLiteral("Writing a %1 question from %2, %3")
                      .arg(typeName(m_current.type), m_current.title,
                           StudyText::referenceLabel(m_current.chunk.refKind, m_current.chunk.refNumber)));
    m_currentRequest = m_llm->generate(buildRequest(m_current.type, m_current.chunk, m_current.title, state.concepts,
                                                    m_current.visuals, m_current.textOnly, m_current.contextRetry));
}

QuestionType QuestionGenerator::fallbackFormat(QuestionType current) const
{
    const int index = static_cast<int>(m_formats.indexOf(current));
    for (int step = 1; step < m_formats.size(); ++step) {
        const QuestionType t = m_formats.at((index + step) % m_formats.size());
        if (t != QuestionType::MultiSelect) {
            return t;
        }
    }
    return current;
}

void QuestionGenerator::onFinished(int requestId, const QString& content)
{
    if (!m_running || requestId != m_currentRequest) {
        return;
    }
    m_currentRequest = 0;
    if (m_readingFigure) {
        onFigureFinished(content);
        return;
    }
    Job job = m_current;
    QString error;
    bool unsupported = false;
    int visual = 0;
    std::optional<Question> parsed = parseResponse(job.type, content, &error, &unsupported, &visual);

    if (unsupported) {
        // Never force a multi-select question: ask for another enabled format from this passage instead.
        const QuestionType fallback = fallbackFormat(job.type);
        if (!job.fellBack && fallback != job.type) {
            job.type = fallback;
            job.fellBack = true;
            job.attempts = 0;
            m_jobs.prepend(job);
            submitNext();
            return;
        }
        qCInfo(lcGenerator) << "rejected (no multi-select support in passage)" << job.chunk.id;
        completeJob(job, false);
        return;
    }
    if (!parsed) {
        retryOrReject(job, error);
        return;
    }

    Question q = *parsed;
    q.documentId = job.documentId;
    q.setId = job.setId;
    q.id = stableId(job.documentId, q.type, q.prompt);
    q.sources.first().title = job.title;
    q.sources.first().location = StudyText::referenceLabel(job.chunk.refKind, job.chunk.refNumber);
    if (m_docs.value(job.documentId).scannedUnits.contains(job.chunk.refNumber)) {
        q.sources.first().location += QStringLiteral(" (scanned)");
    }
    q.sources.first().documentId = job.documentId;
    q.sources.first().chunkId = job.chunk.id;
    q.chunkId = job.chunk.id;

    // A data snippet must be copied from the passage: shown numbers are never made up.
    for (const SupportRef& s : std::as_const(q.support)) {
        if (s.kind == SupportKind::Data && !QuestionValidator::dataQuoted(s.text, job.chunk.text)) {
            retryOrReject(job, QStringLiteral("data is not quoted from the source"));
            return;
        }
    }

    // The visual the question uses travels with it, and its description counts as source text.
    QString sourceText = job.chunk.text;
    const QVector<SupportAsset> shown = job.textOnly ? QVector<SupportAsset>() : job.visuals;
    if (visual > 0 && visual <= shown.size()) {
        const SupportAsset& asset = shown.at(visual - 1);
        q.support.append(supportRefFor(asset));
        const QString description = describeVisual(asset);
        const QString evidence = q.sources.first().excerpt;
        q.evidenceFromVisual = !QuestionValidator::evidenceSupported(evidence, job.chunk.text)
            && QuestionValidator::evidenceSupported(evidence, description);
        sourceText += QLatin1Char('\n') + description;
    }

    DocState& state = m_docs[job.documentId];
    const QuestionValidator::Result verdict = QuestionValidator::validate(q, sourceText, state.concepts, state.prompts);
    if (!verdict.ok) {
        if (verdict.reason == QLatin1String(QuestionValidator::kUnseenVisualReason)) {
            job.textOnly = true;   // try once more, about the text alone
        }
        if (QuestionValidator::isContextReason(verdict.reason)) {
            job.contextRetry = true;   // say what went wrong, so the next try stands on its own
        }
        retryOrReject(job, verdict.reason);
        return;
    }
    if (!job.replacing.isEmpty() && m_db->question(q.id)) {
        retryOrReject(job, QStringLiteral("duplicate question"));
        return;
    }
    if (!m_db->saveQuestion(q, job.chunk.id, m_modelName)) {
        retryOrReject(job, m_db->lastError());
        return;
    }
    state.concepts.append(q.topic);
    state.prompts.append(q.prompt);
    if (!job.replacing.isEmpty()) {
        emit questionRegenerated(job.replacing, q.id);
    }
    completeJob(job, true);
}

void QuestionGenerator::onFailed(int requestId, const LlmError& error)
{
    if (!m_running || requestId != m_currentRequest) {
        return;
    }
    m_currentRequest = 0;
    if (m_readingFigure && error.kind != LlmErrorKind::Cancelled) {
        // Figure reading is optional: without it questions stay about the text. A missing vision
        // model stops figure reading for this run; other errors skip just this figure.
        m_readingFigure = false;
        ++m_figuresUnreadable;
        ++m_done;
        qCInfo(lcGenerator) << "figure" << m_currentFigure.asset.id << "not read:" << error.message;
        if (error.kind == LlmErrorKind::ModelMissing || error.kind == LlmErrorKind::NotRunning) {
            m_done += static_cast<int>(m_figures.size());
            m_figures.clear();
            if (error.kind == LlmErrorKind::NotRunning) {
                abort(error.message);
                return;
            }
        }
        submitNext();
        return;
    }
    switch (error.kind) {
    case LlmErrorKind::Cancelled:
        return;
    case LlmErrorKind::NotRunning:
    case LlmErrorKind::ModelMissing:
        // Retrying cannot help; keep what was generated and stop.
        m_jobs.prepend(m_current);
        abort(error.message);
        return;
    case LlmErrorKind::Timeout:
    case LlmErrorKind::Http:
    case LlmErrorKind::BadResponse:
    case LlmErrorKind::None:
        retryOrReject(m_current, error.message);
        return;
    }
}

void QuestionGenerator::retryOrReject(Job job, const QString& reason)
{
    ++job.attempts;
    qCInfo(lcGenerator) << "attempt" << job.attempts << "failed for chunk" << job.chunk.id << ":" << reason;
    if (job.attempts < kMaxAttemptsPerChunk) {
        m_jobs.prepend(job);
        submitNext();
        return;
    }
    completeJob(job, false);
}

void QuestionGenerator::completeJob(const Job& job, bool accepted)
{
    ++m_done;
    DocState& state = m_docs[job.documentId];
    if (accepted) {
        ++state.accepted;
        ++m_accepted;
    } else {
        ++state.rejected;
        ++m_rejected;
    }
    if (--state.pending <= 0) {
        finishDocument(job.documentId);
    }
    emit progress(m_done, m_total, QStringLiteral("%1 of %2 done").arg(m_done).arg(m_total));
    submitNext();
}

void QuestionGenerator::finishDocument(qint64 documentId)
{
    const DocState state = m_docs.value(documentId);
    if (m_db->documentQuestions(documentId).isEmpty()) {
        m_db->setDocumentStatus(documentId, DocumentStatus::Extracted,
                                state.rejected > 0
                                    ? QStringLiteral("No questions could be checked against the text. Try Generate again.")
                                    : QString());
    } else {
        m_db->setDocumentStatus(documentId, DocumentStatus::Ready);
    }
    emit documentFinished(documentId, state.accepted, state.rejected);
}

void QuestionGenerator::abort(const QString& message)
{
    // Unfinished documents keep whatever was already saved.
    QSet<qint64> open;
    for (auto it = m_docs.cbegin(); it != m_docs.cend(); ++it) {
        if (it.value().pending > 0) {
            open.insert(it.key());
        }
    }
    m_jobs.clear();
    m_figures.clear();
    m_readingFigure = false;
    for (const qint64 docId : open) {
        m_docs[docId].pending = 0;
        finishDocument(docId);
    }
    finishRun(message);
}

void QuestionGenerator::cancel()
{
    if (!m_running) {
        return;
    }
    const int request = m_currentRequest;
    m_currentRequest = 0;
    if (request) {
        m_llm->cancel(request);
    }
    abort(QStringLiteral("Generation cancelled."));
}

void QuestionGenerator::finishRun(const QString& message)
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_currentRequest = 0;
    m_llm->releaseModel();
    QString text = message;
    if (m_figuresOnly && text.isEmpty()) {
        text = m_figuresUnreadable > 0
            ? QStringLiteral("Read %1 figure%2; %3 could not be read clearly.")
                  .arg(m_figuresRead).arg(m_figuresRead == 1 ? QString() : QStringLiteral("s")).arg(m_figuresUnreadable)
            : QStringLiteral("Read %1 figure%2.").arg(m_figuresRead).arg(m_figuresRead == 1 ? QString() : QStringLiteral("s"));
    }
    m_figuresOnly = false;
    emit finished(m_accepted, m_rejected, text);
}

void QuestionGenerator::queueFigures(qint64 documentId, const QVector<Chunk>& chunks, const QString& title, int limit)
{
    if (m_visionModel.isEmpty()) {
        return;
    }
    QSet<qint64> queued;
    for (const FigureJob& f : std::as_const(m_figures)) {
        queued.insert(f.asset.id);
    }
    const QVector<SupportAsset> assets = m_db->supportAssets(documentId);
    for (const Chunk& chunk : chunks) {
        for (const SupportAsset& a : assets) {
            if (a.kind != SupportKind::Image || a.interpreted || queued.contains(a.id) || a.unitNumber != chunk.refNumber
                || !StudyText::sameUnit(a.unitKind, chunk.refKind) || m_figures.size() >= limit) {
                continue;
            }
            FigureJob job;
            job.asset = a;
            job.title = title;
            job.context = (a.caption.isEmpty() ? QString() : a.caption + QLatin1Char('\n')) + chunk.text.left(600);
            m_figures.enqueue(job);
            queued.insert(a.id);
        }
    }
}

bool QuestionGenerator::readFigures(qint64 documentId)
{
    if (m_running || !m_db || !m_llm || m_visionModel.isEmpty()) {
        return false;
    }
    const auto doc = m_db->document(documentId);
    if (!doc) {
        return false;
    }
    m_jobs.clear();
    m_docs.clear();
    m_figures.clear();
    m_total = m_done = m_accepted = m_rejected = 0;
    m_figuresRead = m_figuresUnreadable = 0;
    // Every not-yet-read image of the document, with its page or slide text as context.
    QVector<Chunk> units;
    const QVector<Chunk> chunks = m_db->chunks(documentId);
    for (const SupportAsset& a : m_db->supportAssets(documentId)) {
        if (a.kind != SupportKind::Image || a.interpreted) {
            continue;
        }
        Chunk unit;
        unit.refKind = a.unitKind;
        unit.refNumber = a.unitNumber;
        for (const Chunk& c : chunks) {
            if (c.refNumber == a.unitNumber && StudyText::sameUnit(a.unitKind, c.refKind)) {
                unit.text += c.text + QLatin1Char('\n');
            }
        }
        units.append(unit);
    }
    for (const Chunk& unit : units) {
        queueFigures(documentId, {unit}, doc->title, kMaxFiguresPerDocument);
    }
    if (m_figures.isEmpty()) {
        return false;
    }
    m_total = static_cast<int>(m_figures.size());
    m_figuresOnly = true;
    m_running = true;
    emit progress(0, m_total, QStringLiteral("Starting…"));
    submitNext();
    return true;
}

void QuestionGenerator::submitFigure()
{
    m_currentFigure = m_figures.dequeue();
    m_readingFigure = true;
    const QByteArray image = FigureInterpreter::prepareImage(m_currentFigure.asset.path);
    if (image.isEmpty()) {
        m_db->setAssetInterpretation(m_currentFigure.asset.id, QString(), m_visionModel, false);
        ++m_figuresUnreadable;
        ++m_done;
        m_readingFigure = false;
        submitNext();
        return;
    }
    emit progress(m_done, m_total,
                  QStringLiteral("Looking at the figure on %1 of %2")
                      .arg(StudyText::referenceLabel(m_currentFigure.asset.unitKind, m_currentFigure.asset.unitNumber),
                           m_currentFigure.title));
    m_currentRequest = m_llm->generate(FigureInterpreter::buildRequest(image, m_currentFigure.context, m_visionModel));
}

void QuestionGenerator::onFigureFinished(const QString& content)
{
    m_readingFigure = false;
    QString error;
    const std::optional<FigureInterpreter::Reading> reading = FigureInterpreter::parse(content, &error);
    const SupportAsset& asset = m_currentFigure.asset;
    if (reading) {
        // A reading that contradicts the words OCR found in the same image is not trusted.
        const bool legible = reading->legible && FigureInterpreter::agreesWithOcr(*reading, asset.text);
        m_db->setAssetInterpretation(asset.id, reading->toText(), m_visionModel, legible, reading->readableText);
        legible ? ++m_figuresRead : ++m_figuresUnreadable;
    } else {
        qCInfo(lcGenerator) << "figure" << asset.id << "not understood:" << error;
        m_db->setAssetInterpretation(asset.id, QString(), m_visionModel, false);
        ++m_figuresUnreadable;
    }
    ++m_done;
    submitNext();
}
