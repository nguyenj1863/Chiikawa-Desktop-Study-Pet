#include "study/QuestionGenerator.h"

#include "study/QuestionValidator.h"
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
    "needed to answer. Write the question like a normal exam question: ask it directly and do not "
    "mention \"the passage\" or \"the text\", and never list the answer options inside the question. "
    "Write \"explanation\" to the student in 1-2 sentences, explaining why the answer is right; "
    "do not describe the question itself. In \"explanation\" and \"why\", the student never sees the passage: "
    "refer to it as \"your notes\" or state the fact directly. Reply with JSON only.";

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
    // The student never sees "the passage"; drop phrases that refer to it.
    static const QRegularExpression passageRef(
        QStringLiteral(R"(,?\s+(?:according to|based on|in|from) the (?:provided |given )?(?:passage|text)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    QString stem = prompt;
    const QRegularExpressionMatch m = listLine.match(stem);
    if (m.hasMatch() && m.capturedStart() > 10) {
        stem = stem.left(m.capturedStart()).trimmed();
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
    case QuestionType::OpenTrivia:
        break;
    }
    return QStringLiteral("trivia");
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

QByteArray QuestionGenerator::schemaFor(QuestionType type)
{
    const QByteArray difficulty = R"("difficulty":{"type":"string","enum":["easy","medium","hard"]})";
    switch (type) {
    case QuestionType::SingleChoice:
    case QuestionType::MultiSelect: {
        const QByteArray option = objectSchema({stringProp("text"), R"("correct":{"type":"boolean"})", stringProp("why")},
                                               {"text", "correct", "why"});
        const QByteArray options = R"("options":{"type":"array","minItems":4,"maxItems":4,"items":)" + option + "}";
        QList<QByteArray> props = {stringProp("concept"), stringProp("evidence"), stringProp("question"), options,
                                   stringProp("explanation"), difficulty};
        QList<QByteArray> required = {"concept", "evidence", "question", "options", "explanation", "difficulty"};
        if (type == QuestionType::MultiSelect) {
            props.prepend(R"("supported":{"type":"boolean"})");
            required.prepend("supported");
        }
        return objectSchema(props, required);
    }
    case QuestionType::Written:
        return objectSchema({stringProp("concept"), stringProp("evidence"), stringProp("question"),
                             stringProp("expected_answer"),
                             R"("key_points":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"string"}})",
                             R"("aliases":{"type":"array","maxItems":6,"items":{"type":"string"}})",
                             stringProp("explanation"), difficulty},
                            {"concept", "evidence", "question", "expected_answer", "key_points", "aliases",
                             "explanation", "difficulty"});
    case QuestionType::OpenTrivia:
        break;
    }
    return QByteArray();
}

LlmRequest QuestionGenerator::buildRequest(QuestionType type, const Chunk& chunk, const QString& documentTitle,
                                           const QStringList& avoidConcepts)
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
    case QuestionType::OpenTrivia:
        break;
    }
    QString avoid;
    if (!avoidConcepts.isEmpty()) {
        avoid = QStringLiteral("\nDo not test these concepts again: %1.").arg(avoidConcepts.mid(0, 20).join(QStringLiteral("; ")));
    }
    LlmRequest r;
    r.system = QString::fromLatin1(kSystemPrompt);
    r.prompt = QStringLiteral(
                   "Source: \"%1\", %2\n\nPASSAGE:\n\"\"\"\n%3\n\"\"\"\n\n%4\n"
                   "\"concept\" names the single idea being tested in a few words. "
                   "\"difficulty\" is easy for recall, medium for understanding, hard for applying or comparing ideas."
                   "%5")
                   .arg(documentTitle, StudyText::referenceLabel(chunk.refKind, chunk.refNumber),
                        chunk.text.left(2400), task, avoid);
    r.schema = schemaFor(type);
    r.temperature = 0.3;
    r.maxTokens = kGenerationMaxTokens;
    r.timeoutMs = kGenerationTimeoutMs;
    return r;
}

std::optional<Question> QuestionGenerator::parseResponse(QuestionType type, const QString& json, QString* error,
                                                         bool* unsupported)
{
    if (unsupported) {
        *unsupported = false;
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
        q.answers.append({q.expectedAnswer, aliases, Rarity::Common});
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
        if (t != QuestionType::OpenTrivia && !m_formats.contains(t)) {
            m_formats.append(t);
        }
    }
    if (m_formats.isEmpty()) {
        return false;
    }
    m_jobs.clear();
    m_docs.clear();
    m_total = m_done = m_accepted = m_rejected = 0;

    for (const qint64 docId : documentIds) {
        const auto doc = m_db->document(docId);
        if (!doc || doc->chunkCount == 0) {
            continue;
        }
        if (regenerate) {
            m_db->removeQuestions(docId);
        }
        DocState state;
        const QVector<Question> existing = m_db->documentQuestions(docId);
        for (const Question& q : existing) {
            state.concepts.append(q.topic);
            state.prompts.append(q.prompt);
        }
        const QVector<Chunk> picks = pickChunks(m_db->chunks(docId), m_db->usedChunkIds(docId), kMaxQuestionsPerRun);
        for (qsizetype i = 0; i < picks.size(); ++i) {
            Job job;
            job.documentId = docId;
            job.courseId = doc->courseId;
            job.title = doc->title;
            job.chunk = picks.at(i);
            job.type = m_formats.at(static_cast<int>((existing.size() + i) % m_formats.size()));
            m_jobs.enqueue(job);
        }
        state.pending = static_cast<int>(picks.size());
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
        return false;
    }
    m_running = true;
    emit progress(0, m_total, QStringLiteral("Starting…"));
    submitNext();
    return true;
}

void QuestionGenerator::submitNext()
{
    if (!m_running) {
        return;
    }
    if (m_jobs.isEmpty()) {
        finishRun(QString());
        return;
    }
    m_current = m_jobs.dequeue();
    const DocState& state = m_docs[m_current.documentId];
    emit progress(m_done, m_total,
                  QStringLiteral("Writing a %1 question from %2, %3")
                      .arg(typeName(m_current.type), m_current.title,
                           StudyText::referenceLabel(m_current.chunk.refKind, m_current.chunk.refNumber)));
    m_currentRequest = m_llm->generate(buildRequest(m_current.type, m_current.chunk, m_current.title, state.concepts));
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
    Job job = m_current;
    QString error;
    bool unsupported = false;
    std::optional<Question> parsed = parseResponse(job.type, content, &error, &unsupported);

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
    q.courseId = job.courseId;
    q.id = stableId(job.documentId, q.type, q.prompt);
    q.sources.first().title = job.title;
    q.sources.first().location = StudyText::referenceLabel(job.chunk.refKind, job.chunk.refNumber);
    q.sources.first().documentId = job.documentId;
    q.sources.first().chunkId = job.chunk.id;

    DocState& state = m_docs[job.documentId];
    const QuestionValidator::Result verdict = QuestionValidator::validate(q, job.chunk.text, state.concepts, state.prompts);
    if (!verdict.ok) {
        retryOrReject(job, verdict.reason);
        return;
    }
    if (!m_db->saveQuestion(q, job.chunk.id, m_modelName)) {
        retryOrReject(job, m_db->lastError());
        return;
    }
    state.concepts.append(q.topic);
    state.prompts.append(q.prompt);
    completeJob(job, true);
}

void QuestionGenerator::onFailed(int requestId, const LlmError& error)
{
    if (!m_running || requestId != m_currentRequest) {
        return;
    }
    m_currentRequest = 0;
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
    emit finished(m_accepted, m_rejected, message);
}
