#include "study/StudyDatabase.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>

namespace {

QString& storageOverride()
{
    static QString dir;
    return dir;
}

QString nextConnectionName()
{
    static QAtomicInteger<int> counter;
    return QStringLiteral("chiikawa_study_%1").arg(counter.fetchAndAddRelaxed(1));
}

/** Qt binds a null QString as SQL NULL; text columns are NOT NULL, so bind "" instead. */
QString text(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString joinUnits(const QList<int>& units)
{
    QStringList parts;
    for (const int u : units) {
        parts.append(QString::number(u));
    }
    return parts.join(QLatin1Char(','));
}

QList<int> splitUnits(const QString& text)
{
    QList<int> out;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = part.toInt(&ok);
        if (ok) {
            out.append(v);
        }
    }
    return out;
}

DocumentRecord readDocument(const QSqlQuery& q)
{
    DocumentRecord d;
    d.id = q.value(0).toLongLong();
    d.courseId = q.value(1).toLongLong();
    d.title = q.value(2).toString();
    d.originalPath = q.value(3).toString();
    d.storedPath = q.value(4).toString();
    d.kind = static_cast<DocumentKind>(q.value(5).toInt());
    d.status = static_cast<DocumentStatus>(q.value(6).toInt());
    d.error = q.value(7).toString();
    d.unitCount = q.value(8).toInt();
    d.unsupportedUnits = splitUnits(q.value(9).toString());
    d.selected = q.value(10).toBool();
    d.chunkCount = q.value(11).toInt();
    d.questionCount = q.value(12).toInt();
    return d;
}

const QString kDocumentColumns = QStringLiteral(
    "d.id, d.course_id, d.title, d.original_path, d.stored_path, d.kind, d.status, d.error, "
    "d.unit_count, d.unsupported, d.selected, "
    "(SELECT COUNT(*) FROM chunks c WHERE c.document_id = d.id), "
    "(SELECT COUNT(*) FROM questions qn WHERE qn.document_id = d.id)");

QJsonObject questionPayload(const Question& q)
{
    QJsonArray options;
    for (const QuestionOption& o : q.options) {
        options.append(QJsonObject{{QStringLiteral("id"), o.id},
                                   {QStringLiteral("text"), o.text},
                                   {QStringLiteral("correct"), o.correct},
                                   {QStringLiteral("why"), o.explanation}});
    }
    QJsonArray aliases;
    for (const AcceptedAnswer& a : q.answers) {
        for (const QString& alias : a.aliases) {
            aliases.append(alias);
        }
    }
    const SourceRef source = q.sources.isEmpty() ? SourceRef{} : q.sources.first();
    return QJsonObject{
        {QStringLiteral("options"), options},
        {QStringLiteral("expected"), q.expectedAnswer},
        {QStringLiteral("key_points"), QJsonArray::fromStringList(q.keyConcepts)},
        {QStringLiteral("aliases"), aliases},
        {QStringLiteral("explanation"), q.explanation},
        {QStringLiteral("evidence"), source.excerpt},
        {QStringLiteral("location"), source.location},
    };
}

const QString kQuestionColumns = QStringLiteral(
    "qn.id, qn.document_id, qn.course_id, qn.chunk_id, qn.type, qn.prompt, qn.concept, qn.difficulty, "
    "qn.payload, d.title, co.name");

const QString kQuestionFrom = QStringLiteral(
    " FROM questions qn JOIN documents d ON d.id = qn.document_id JOIN courses co ON co.id = qn.course_id");

Question readQuestion(const QSqlQuery& q)
{
    Question out;
    out.id = q.value(0).toString();
    out.documentId = q.value(1).toLongLong();
    out.courseId = q.value(2).toLongLong();
    const qint64 chunkId = q.value(3).toLongLong();
    out.type = static_cast<QuestionType>(q.value(4).toInt());
    out.prompt = q.value(5).toString();
    out.topic = q.value(6).toString();
    out.difficulty = static_cast<Difficulty>(std::clamp(q.value(7).toInt(), 0, 2));
    const QJsonObject payload = QJsonDocument::fromJson(q.value(8).toString().toUtf8()).object();
    out.providerId = QStringLiteral("study");
    out.category = q.value(10).toString();
    out.explanation = payload.value(QStringLiteral("explanation")).toString();
    out.expectedAnswer = payload.value(QStringLiteral("expected")).toString();
    for (const QJsonValue& k : payload.value(QStringLiteral("key_points")).toArray()) {
        out.keyConcepts.append(k.toString());
    }
    for (const QJsonValue& v : payload.value(QStringLiteral("options")).toArray()) {
        const QJsonObject o = v.toObject();
        out.options.append({o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("text")).toString(),
                            o.value(QStringLiteral("correct")).toBool(), o.value(QStringLiteral("why")).toString()});
    }
    QStringList aliases;
    for (const QJsonValue& a : payload.value(QStringLiteral("aliases")).toArray()) {
        aliases.append(a.toString());
    }
    if (!out.expectedAnswer.isEmpty()) {
        out.answers.append({out.expectedAnswer, aliases});
    }
    SourceRef source;
    source.title = q.value(9).toString();
    source.location = payload.value(QStringLiteral("location")).toString();
    source.excerpt = payload.value(QStringLiteral("evidence")).toString();
    source.documentId = out.documentId;
    source.chunkId = chunkId;
    out.sources.append(source);
    return out;
}

} // namespace

StudyDatabase::StudyDatabase()
    : m_connectionName(nextConnectionName())
{
}

StudyDatabase::~StudyDatabase()
{
    close();
}

void StudyDatabase::setStorageDirectory(const QString& dir)
{
    storageOverride() = dir;
}

QString StudyDatabase::storageDirectory()
{
    if (!storageOverride().isEmpty()) {
        return storageOverride();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/study");
}

QString StudyDatabase::databasePath()
{
    return storageDirectory() + QStringLiteral("/library.db");
}

QString StudyDatabase::filesDirectory()
{
    return storageDirectory() + QStringLiteral("/files");
}

bool StudyDatabase::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName) && QSqlDatabase::database(m_connectionName, false).isOpen();
}

QSqlDatabase StudyDatabase::connection() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool StudyDatabase::open()
{
    if (isOpen()) {
        return true;
    }
    if (!QDir().mkpath(storageDirectory()) || !QDir().mkpath(filesDirectory())) {
        m_lastError = QStringLiteral("Could not create the study data folder.");
        return false;
    }
    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
        ? QSqlDatabase::database(m_connectionName, false)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(databasePath());
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    if (!migrate()) {
        db.close();
        return false;
    }
    return true;
}

void StudyDatabase::close()
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool StudyDatabase::exec(const QString& sql)
{
    QSqlQuery q(connection());
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool StudyDatabase::migrate()
{
    QSqlQuery v(connection());
    v.exec(QStringLiteral("PRAGMA user_version"));
    const int version = v.next() ? v.value(0).toInt() : 0;
    if (version >= kSchemaVersion) {
        return true;
    }

    const QStringList schema = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS courses ("
                       " id INTEGER PRIMARY KEY,"
                       " name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                       " created_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS documents ("
                       " id INTEGER PRIMARY KEY,"
                       " course_id INTEGER NOT NULL REFERENCES courses(id) ON DELETE CASCADE,"
                       " title TEXT NOT NULL,"
                       " original_path TEXT NOT NULL DEFAULT '',"
                       " stored_path TEXT NOT NULL DEFAULT '',"
                       " kind INTEGER NOT NULL,"
                       " status INTEGER NOT NULL,"
                       " error TEXT NOT NULL DEFAULT '',"
                       " unit_count INTEGER NOT NULL DEFAULT 0,"
                       " unsupported TEXT NOT NULL DEFAULT '',"
                       " selected INTEGER NOT NULL DEFAULT 1,"
                       " created_at INTEGER NOT NULL,"
                       " updated_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS chunks ("
                       " id INTEGER PRIMARY KEY,"
                       " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                       " course_id INTEGER NOT NULL,"
                       " ref_kind INTEGER NOT NULL,"
                       " ref_number INTEGER NOT NULL,"
                       " ordinal INTEGER NOT NULL,"
                       " text TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS chunks_document ON chunks(document_id)"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
                       " text, content='chunks', content_rowid='id', tokenize='porter unicode61')"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS chunks_ai AFTER INSERT ON chunks BEGIN"
                       " INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text); END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS chunks_ad AFTER DELETE ON chunks BEGIN"
                       " INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text); END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS chunks_au AFTER UPDATE ON chunks BEGIN"
                       " INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text);"
                       " INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text); END"),
        // Generated questions. Format-specific fields live in the JSON payload.
        QStringLiteral("CREATE TABLE IF NOT EXISTS questions ("
                       " id TEXT PRIMARY KEY,"
                       " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                       " course_id INTEGER NOT NULL,"
                       " chunk_id INTEGER REFERENCES chunks(id) ON DELETE SET NULL,"
                       " type INTEGER NOT NULL,"
                       " prompt TEXT NOT NULL,"
                       " concept TEXT NOT NULL DEFAULT '',"
                       " difficulty INTEGER NOT NULL DEFAULT 1,"
                       " payload TEXT NOT NULL,"
                       " model TEXT NOT NULL DEFAULT '',"
                       " created_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS questions_course ON questions(course_id, type)"),
        // Every answer. kind 0 = automatically graded, 1 = self-assessed after Reveal.
        QStringLiteral("CREATE TABLE IF NOT EXISTS attempts ("
                       " id INTEGER PRIMARY KEY,"
                       " question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,"
                       " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                       " course_id INTEGER NOT NULL,"
                       " type INTEGER NOT NULL,"
                       " kind INTEGER NOT NULL,"
                       " verdict INTEGER NOT NULL,"
                       " score REAL NOT NULL,"
                       " xp INTEGER NOT NULL,"
                       " answered_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS attempts_course ON attempts(course_id, answered_at)"),
        // Per-concept progress; due_at and ease are reserved for spaced repetition.
        QStringLiteral("CREATE TABLE IF NOT EXISTS concept_stats ("
                       " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                       " course_id INTEGER NOT NULL,"
                       " concept TEXT NOT NULL,"
                       " seen INTEGER NOT NULL DEFAULT 0,"
                       " correct INTEGER NOT NULL DEFAULT 0,"
                       " partial INTEGER NOT NULL DEFAULT 0,"
                       " incorrect INTEGER NOT NULL DEFAULT 0,"
                       " last_seen INTEGER NOT NULL DEFAULT 0,"
                       " needs_review INTEGER NOT NULL DEFAULT 0,"
                       " due_at INTEGER,"
                       " ease REAL,"
                       " PRIMARY KEY (document_id, concept))"),
    };

    QSqlDatabase db = connection();
    db.transaction();
    for (const QString& sql : schema) {
        if (!exec(sql)) {
            db.rollback();
            return false;
        }
    }
    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        db.rollback();
        return false;
    }
    return db.commit();
}

std::optional<qint64> StudyDatabase::addCourse(const QString& name)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty()) {
        m_lastError = QStringLiteral("Course name is empty.");
        return std::nullopt;
    }
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO courses(name, created_at) VALUES (?, ?)"));
    q.addBindValue(text(trimmed));
    q.addBindValue(nowMs());
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return std::nullopt;
    }
    return q.lastInsertId().toLongLong();
}

bool StudyDatabase::renameCourse(qint64 id, const QString& name)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty()) {
        return false;
    }
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE courses SET name = ? WHERE id = ?"));
    q.addBindValue(text(trimmed));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

bool StudyDatabase::removeCourse(qint64 id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM courses WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

QVector<Course> StudyDatabase::courses()
{
    QVector<Course> out;
    QSqlQuery q(connection());
    q.exec(QStringLiteral("SELECT id, name FROM courses ORDER BY name COLLATE NOCASE"));
    while (q.next()) {
        out.append({q.value(0).toLongLong(), q.value(1).toString()});
    }
    return out;
}

std::optional<qint64> StudyDatabase::addDocument(qint64 courseId, const QString& title,
                                                 const QString& originalPath, DocumentKind kind)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO documents(course_id, title, original_path, kind, status, created_at, updated_at)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(courseId);
    q.addBindValue(text(title));
    q.addBindValue(text(originalPath));
    q.addBindValue(static_cast<int>(kind));
    q.addBindValue(static_cast<int>(DocumentStatus::Queued));
    q.addBindValue(nowMs());
    q.addBindValue(nowMs());
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return std::nullopt;
    }
    return q.lastInsertId().toLongLong();
}

bool StudyDatabase::setDocumentStoredPath(qint64 id, const QString& storedPath)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE documents SET stored_path = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(text(storedPath));
    q.addBindValue(nowMs());
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

bool StudyDatabase::setDocumentStatus(qint64 id, DocumentStatus status, const QString& error)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE documents SET status = ?, error = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(static_cast<int>(status));
    q.addBindValue(text(error));
    q.addBindValue(nowMs());
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

bool StudyDatabase::setDocumentExtraction(qint64 id, int unitCount, const QList<int>& unsupportedUnits)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE documents SET unit_count = ?, unsupported = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(unitCount);
    q.addBindValue(text(joinUnits(unsupportedUnits)));
    q.addBindValue(nowMs());
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

bool StudyDatabase::setDocumentSelected(qint64 id, bool selected)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE documents SET selected = ? WHERE id = ?"));
    q.addBindValue(selected ? 1 : 0);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

std::optional<DocumentRecord> StudyDatabase::document(qint64 id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT %1 FROM documents d WHERE d.id = ?").arg(kDocumentColumns));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readDocument(q);
    }
    return std::nullopt;
}

QVector<DocumentRecord> StudyDatabase::documents(qint64 courseId)
{
    QVector<DocumentRecord> out;
    QSqlQuery q(connection());
    if (courseId > 0) {
        q.prepare(QStringLiteral("SELECT %1 FROM documents d WHERE d.course_id = ? ORDER BY d.created_at, d.id")
                      .arg(kDocumentColumns));
        q.addBindValue(courseId);
    } else {
        q.prepare(QStringLiteral("SELECT %1 FROM documents d ORDER BY d.created_at, d.id").arg(kDocumentColumns));
    }
    if (q.exec()) {
        while (q.next()) {
            out.append(readDocument(q));
        }
    }
    return out;
}

bool StudyDatabase::removeDocument(qint64 id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM documents WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

bool StudyDatabase::replaceChunks(qint64 documentId, const QVector<Chunk>& chunks)
{
    const auto doc = document(documentId);
    if (!doc) {
        m_lastError = QStringLiteral("Document not found.");
        return false;
    }
    QSqlDatabase db = connection();
    if (!db.transaction()) {
        m_lastError = db.lastError().text();
        return false;
    }
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM chunks WHERE document_id = ?"));
    del.addBindValue(documentId);
    if (!del.exec()) {
        m_lastError = del.lastError().text();
        db.rollback();
        return false;
    }
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO chunks(document_id, course_id, ref_kind, ref_number, ordinal, text)"
                               " VALUES (?, ?, ?, ?, ?, ?)"));
    for (const Chunk& c : chunks) {
        ins.addBindValue(documentId);
        ins.addBindValue(doc->courseId);
        ins.addBindValue(static_cast<int>(c.refKind));
        ins.addBindValue(c.refNumber);
        ins.addBindValue(c.ordinal);
        ins.addBindValue(text(c.text));
        if (!ins.exec()) {
            m_lastError = ins.lastError().text();
            db.rollback();
            return false;
        }
    }
    if (!db.commit()) {
        m_lastError = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

QVector<Chunk> StudyDatabase::chunks(qint64 documentId)
{
    QVector<Chunk> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT id, document_id, course_id, ref_kind, ref_number, ordinal, text"
                             " FROM chunks WHERE document_id = ? ORDER BY ordinal"));
    q.addBindValue(documentId);
    if (q.exec()) {
        while (q.next()) {
            Chunk c;
            c.id = q.value(0).toLongLong();
            c.documentId = q.value(1).toLongLong();
            c.courseId = q.value(2).toLongLong();
            c.refKind = static_cast<SegmentKind>(q.value(3).toInt());
            c.refNumber = q.value(4).toInt();
            c.ordinal = q.value(5).toInt();
            c.text = q.value(6).toString();
            out.append(c);
        }
    }
    return out;
}

std::optional<Chunk> StudyDatabase::chunk(qint64 chunkId)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT id, document_id, course_id, ref_kind, ref_number, ordinal, text"
                             " FROM chunks WHERE id = ?"));
    q.addBindValue(chunkId);
    if (q.exec() && q.next()) {
        Chunk c;
        c.id = q.value(0).toLongLong();
        c.documentId = q.value(1).toLongLong();
        c.courseId = q.value(2).toLongLong();
        c.refKind = static_cast<SegmentKind>(q.value(3).toInt());
        c.refNumber = q.value(4).toInt();
        c.ordinal = q.value(5).toInt();
        c.text = q.value(6).toString();
        return c;
    }
    return std::nullopt;
}

bool StudyDatabase::saveQuestion(const Question& question, qint64 chunkId, const QString& model)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO questions(id, document_id, course_id, chunk_id, type, prompt,"
                             " concept, difficulty, payload, model, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(question.id);
    q.addBindValue(question.documentId);
    q.addBindValue(question.courseId);
    q.addBindValue(chunkId > 0 ? QVariant(chunkId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(static_cast<int>(question.type));
    q.addBindValue(text(question.prompt));
    q.addBindValue(text(question.topic));
    q.addBindValue(static_cast<int>(question.difficulty));
    q.addBindValue(QString::fromUtf8(QJsonDocument(questionPayload(question)).toJson(QJsonDocument::Compact)));
    q.addBindValue(text(model));
    q.addBindValue(nowMs());
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<Question> StudyDatabase::question(const QString& id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT ") + kQuestionColumns + kQuestionFrom + QStringLiteral(" WHERE qn.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readQuestion(q);
    }
    return std::nullopt;
}

QVector<Question> StudyDatabase::documentQuestions(qint64 documentId)
{
    QVector<Question> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT ") + kQuestionColumns + kQuestionFrom
              + QStringLiteral(" WHERE qn.document_id = ? ORDER BY qn.created_at, qn.id"));
    q.addBindValue(documentId);
    if (q.exec()) {
        while (q.next()) {
            out.append(readQuestion(q));
        }
    }
    return out;
}

QVector<Question> StudyDatabase::courseQuestions(qint64 courseId, const QList<QuestionType>& types, bool selectedOnly)
{
    QVector<Question> out;
    QString sql = QStringLiteral("SELECT ") + kQuestionColumns + kQuestionFrom + QStringLiteral(" WHERE qn.course_id = ?");
    if (selectedOnly) {
        sql += QStringLiteral(" AND d.selected = 1");
    }
    if (!types.isEmpty()) {
        QStringList ids;
        for (const QuestionType t : types) {
            ids.append(QString::number(static_cast<int>(t)));
        }
        sql += QStringLiteral(" AND qn.type IN (%1)").arg(ids.join(QLatin1Char(',')));
    }
    sql += QStringLiteral(" ORDER BY qn.created_at, qn.id");
    QSqlQuery q(connection());
    q.prepare(sql);
    q.addBindValue(courseId);
    if (q.exec()) {
        while (q.next()) {
            out.append(readQuestion(q));
        }
    }
    return out;
}

bool StudyDatabase::removeQuestions(qint64 documentId)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM questions WHERE document_id = ?"));
    q.addBindValue(documentId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QSet<qint64> StudyDatabase::usedChunkIds(qint64 documentId)
{
    QSet<qint64> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT DISTINCT chunk_id FROM questions WHERE document_id = ? AND chunk_id IS NOT NULL"));
    q.addBindValue(documentId);
    if (q.exec()) {
        while (q.next()) {
            out.insert(q.value(0).toLongLong());
        }
    }
    return out;
}

void StudyDatabase::recoverInterruptedDocuments()
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE documents SET status = ?, error = ? WHERE status IN (?, ?)"));
    q.addBindValue(static_cast<int>(DocumentStatus::Failed));
    q.addBindValue(QStringLiteral("Processing was interrupted. Import the file again."));
    q.addBindValue(static_cast<int>(DocumentStatus::Queued));
    q.addBindValue(static_cast<int>(DocumentStatus::Extracting));
    q.exec();
    // Interrupted generation keeps its extracted text; it just needs regenerating.
    QSqlQuery g(connection());
    g.prepare(QStringLiteral("UPDATE documents SET status = ? WHERE status = ?"));
    g.addBindValue(static_cast<int>(DocumentStatus::Extracted));
    g.addBindValue(static_cast<int>(DocumentStatus::Generating));
    g.exec();
}
