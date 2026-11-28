#include "study/StudyDatabase.h"

#include "study/progress/ScheduleStore.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QFile>
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
    d.setId = q.value(1).toLongLong();
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
    "d.id, d.set_id, d.title, d.original_path, d.stored_path, d.kind, d.status, d.error, "
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
    // Visuals: stored assets by id; data snippets inline. Resolved to full refs when read.
    QJsonArray support;
    for (const SupportRef& s : q.support) {
        QJsonObject item{{QStringLiteral("kind"), static_cast<int>(s.kind)}};
        if (s.assetId > 0) {
            item.insert(QStringLiteral("asset"), s.assetId);
        } else {
            item.insert(QStringLiteral("text"), s.text);
            item.insert(QStringLiteral("caption"), s.caption);
            if (!s.table.isEmpty()) {
                item.insert(QStringLiteral("table"), StudyText::tableToJson(s.table));
            }
        }
        support.append(item);
    }
    QJsonObject payload{
        {QStringLiteral("options"), options},
        {QStringLiteral("expected"), q.expectedAnswer},
        {QStringLiteral("key_points"), QJsonArray::fromStringList(q.keyConcepts)},
        {QStringLiteral("aliases"), aliases},
        {QStringLiteral("explanation"), q.explanation},
        {QStringLiteral("evidence"), source.excerpt},
        {QStringLiteral("location"), source.location},
    };
    if (!support.isEmpty()) {
        payload.insert(QStringLiteral("support"), support);
    }
    if (q.evidenceFromVisual) {
        payload.insert(QStringLiteral("evidence_source"), QStringLiteral("visual"));
    }
    return payload;
}

const QString kQuestionColumns = QStringLiteral(
    "qn.id, qn.document_id, qn.set_id, qn.chunk_id, qn.type, qn.prompt, qn.concept, qn.difficulty, "
    "qn.payload, d.title, co.name, qn.origin, qn.edited_at");

const QString kAssetColumns = QStringLiteral(
    "id, document_id, unit_kind, unit_number, kind, path, caption, width, height, table_json, text, "
    "interpretation, interpreted_by, interpreted_at, legible");

SupportAsset readAsset(const QSqlQuery& q)
{
    SupportAsset a;
    a.id = q.value(0).toLongLong();
    a.documentId = q.value(1).toLongLong();
    a.unitKind = static_cast<SegmentKind>(q.value(2).toInt());
    a.unitNumber = q.value(3).toInt();
    a.kind = static_cast<SupportKind>(q.value(4).toInt());
    a.path = q.value(5).toString();
    a.caption = q.value(6).toString();
    a.width = q.value(7).toInt();
    a.height = q.value(8).toInt();
    a.table = StudyText::tableFromJson(q.value(9).toString());
    a.text = q.value(10).toString();
    a.interpretation = q.value(11).toString();
    a.interpretedBy = q.value(12).toString();
    a.interpreted = !q.value(13).isNull();
    a.legible = q.value(14).isNull() || q.value(14).toBool();
    return a;
}

SupportRef refFromAsset(const SupportAsset& a)
{
    SupportRef r;
    r.kind = a.kind;
    r.assetId = a.id;
    r.imagePath = a.path;
    r.caption = a.caption;
    r.table = a.table;
    r.text = a.text;
    r.interpretation = a.interpretation;
    return r;
}

const QString kQuestionFrom = QStringLiteral(
    " FROM questions qn JOIN documents d ON d.id = qn.document_id JOIN study_sets co ON co.id = qn.set_id");

Question readQuestion(const QSqlQuery& q)
{
    Question out;
    out.id = q.value(0).toString();
    out.documentId = q.value(1).toLongLong();
    out.setId = q.value(2).toLongLong();
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
    out.chunkId = chunkId;
    out.origin = static_cast<QuestionOrigin>(q.value(11).toInt());
    out.editedAt = q.value(12).toLongLong();
    out.evidenceFromVisual = payload.value(QStringLiteral("evidence_source")).toString() == QLatin1String("visual");
    // Unresolved here: asset refs carry only their id until StudyDatabase::resolveSupport fills them in.
    for (const QJsonValue& v : payload.value(QStringLiteral("support")).toArray()) {
        const QJsonObject o = v.toObject();
        SupportRef ref;
        ref.kind = static_cast<SupportKind>(o.value(QStringLiteral("kind")).toInt());
        ref.assetId = o.value(QStringLiteral("asset")).toInteger();
        ref.text = o.value(QStringLiteral("text")).toString();
        ref.caption = o.value(QStringLiteral("caption")).toString();
        ref.table = StudyText::tableFromJson(o.value(QStringLiteral("table")).toString());
        out.support.append(ref);
    }
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

QString StudyDatabase::assetsDirectory(qint64 documentId)
{
    return filesDirectory() + QStringLiteral("/assets/%1").arg(documentId);
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
    v.finish();   // an open read statement would lock the tables the upgrade rebuilds
    if (version >= kSchemaVersion) {
        return true;
    }

    const QStringList schemaV1 = {
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

    // Upgrading real data: keep a copy of the old file (named for its version) before touching it.
    if (version >= 1 && version <= 3) {
        exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        const char* names[] = {kVersion1BackupName, kVersion2BackupName, kVersion3BackupName};
        const QString backup = storageDirectory() + QLatin1Char('/') + QString::fromLatin1(names[version - 1]);
        QFile::remove(backup);
        if (!QFile::copy(databasePath(), backup)) {
            m_lastError = QStringLiteral("Could not back up the Study Library before upgrading it.");
            return false;
        }
    }

    // Table rebuilds need foreign keys off; the pragma has no effect inside a transaction.
    exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
    QSqlDatabase db = connection();
    db.transaction();
    const auto fail = [&]() {
        db.rollback();
        exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        return false;
    };

    if (version < 1) {
        for (const QString& sql : schemaV1) {
            if (!exec(sql)) {
                return fail();
            }
        }
    }
    if (version < 2) {
        const QStringList upgradeV2 = {
            // courses -> study_sets (child foreign keys are rewritten by the rename)
            QStringLiteral("ALTER TABLE courses RENAME TO study_sets"),
            QStringLiteral("ALTER TABLE documents RENAME COLUMN course_id TO set_id"),
            QStringLiteral("ALTER TABLE chunks RENAME COLUMN course_id TO set_id"),
            QStringLiteral("ALTER TABLE questions RENAME COLUMN course_id TO set_id"),
            QStringLiteral("ALTER TABLE attempts RENAME COLUMN course_id TO set_id"),
            QStringLiteral("ALTER TABLE concept_stats RENAME COLUMN course_id TO set_id"),
            // Rebuild study_sets with parents and per-parent unique names (names were globally unique).
            QStringLiteral("CREATE TABLE study_sets_new ("
                           " id INTEGER PRIMARY KEY,"
                           " name TEXT NOT NULL COLLATE NOCASE,"
                           " parent_id INTEGER REFERENCES study_sets(id) ON DELETE CASCADE,"
                           " sort_order INTEGER NOT NULL DEFAULT 0,"
                           " created_at INTEGER NOT NULL)"),
            QStringLiteral("INSERT INTO study_sets_new(id, name, parent_id, sort_order, created_at)"
                           " SELECT id, name, NULL, 0, created_at FROM study_sets"),
            QStringLiteral("DROP TABLE study_sets"),
            QStringLiteral("ALTER TABLE study_sets_new RENAME TO study_sets"),
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS study_sets_unique_name"
                           " ON study_sets(COALESCE(parent_id, 0), name COLLATE NOCASE)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS study_sets_parent ON study_sets(parent_id)"),
            // Self-assessment and automatic grading both record the 4-level rating that schedules reviews.
            QStringLiteral("ALTER TABLE attempts ADD COLUMN rating INTEGER"),
            QStringLiteral("ALTER TABLE attempts ADD COLUMN rating_source INTEGER NOT NULL DEFAULT 0"),
            // Spaced repetition state, one row per question (state 0 new, 1 learning, 2 review).
            QStringLiteral("CREATE TABLE IF NOT EXISTS card_schedule ("
                           " question_id TEXT PRIMARY KEY REFERENCES questions(id) ON DELETE CASCADE,"
                           " set_id INTEGER NOT NULL,"
                           " state INTEGER NOT NULL DEFAULT 0,"
                           " due_day INTEGER,"
                           " due_at INTEGER,"
                           " interval_days REAL NOT NULL DEFAULT 0,"
                           " ease REAL NOT NULL DEFAULT 2.5,"
                           " reps INTEGER NOT NULL DEFAULT 0,"
                           " lapses INTEGER NOT NULL DEFAULT 0,"
                           " last_review INTEGER)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS card_schedule_due ON card_schedule(set_id, due_day)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS practice_sessions ("
                           " id INTEGER PRIMARY KEY,"
                           " set_id INTEGER NOT NULL DEFAULT 0,"
                           " mode INTEGER NOT NULL,"
                           " started_at INTEGER NOT NULL,"
                           " ended_at INTEGER,"
                           " answered INTEGER NOT NULL DEFAULT 0,"
                           " correct INTEGER NOT NULL DEFAULT 0)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS practice_sessions_time ON practice_sessions(started_at)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS filtered_sets ("
                           " id INTEGER PRIMARY KEY,"
                           " name TEXT NOT NULL COLLATE NOCASE,"
                           " parent_id INTEGER REFERENCES study_sets(id) ON DELETE CASCADE,"
                           " spec_json TEXT NOT NULL,"
                           " reschedule INTEGER NOT NULL DEFAULT 1,"
                           " created_at INTEGER NOT NULL)"),
        };
        for (const QString& sql : upgradeV2) {
            if (!exec(sql)) {
                return fail();
            }
        }
        // Every reference must still resolve after the rebuild; otherwise keep the old file untouched.
        QSqlQuery check(db);
        const bool consistent = check.exec(QStringLiteral("PRAGMA foreign_key_check")) && !check.next();
        check.finish();
        if (!consistent) {
            m_lastError = QStringLiteral("The Study Library upgrade found inconsistent data.");
            return fail();
        }
        check.finish();
        // Seed review schedules from the answers already given (attempts are not otherwise changed).
        if (!ScheduleStore::replayHistory(db)) {
            m_lastError = QStringLiteral("Could not build review schedules from earlier answers.");
            return fail();
        }
    }
    if (version < 3) {
        const QStringList upgradeV3 = {
            // XP is gone: progress is answers, accuracy, due reviews, and concepts to review.
            QStringLiteral("ALTER TABLE attempts DROP COLUMN xp"),
            // Where a question came from: 0 generated, 1 written by the student, 2 generated then edited.
            QStringLiteral("ALTER TABLE questions ADD COLUMN origin INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral("ALTER TABLE questions ADD COLUMN edited_at INTEGER"),
            QStringLiteral("UPDATE questions SET origin = 1 WHERE document_id IN (SELECT id FROM documents WHERE kind = %1)")
                .arg(static_cast<int>(DocumentKind::Manual)),
        };
        for (const QString& sql : upgradeV3) {
            if (!exec(sql)) {
                return fail();
            }
        }
    }
    if (version < 4) {
        const QStringList upgradeV4 = {
            // Figures, tables and data snippets shown beside questions. Images are files next to the
            // library; a vision model's reading of an image is stored once and reused.
            QStringLiteral("CREATE TABLE IF NOT EXISTS support_assets ("
                           " id INTEGER PRIMARY KEY,"
                           " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                           " unit_kind INTEGER NOT NULL,"
                           " unit_number INTEGER NOT NULL,"
                           " kind INTEGER NOT NULL,"
                           " path TEXT NOT NULL DEFAULT '',"
                           " caption TEXT NOT NULL DEFAULT '',"
                           " width INTEGER NOT NULL DEFAULT 0,"
                           " height INTEGER NOT NULL DEFAULT 0,"
                           " table_json TEXT NOT NULL DEFAULT '',"
                           " text TEXT NOT NULL DEFAULT '',"
                           " interpretation TEXT NOT NULL DEFAULT '',"
                           " interpreted_by TEXT NOT NULL DEFAULT '',"
                           " interpreted_at INTEGER,"
                           " legible INTEGER,"
                           " created_at INTEGER NOT NULL)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS support_assets_unit ON support_assets(document_id, unit_number)"),
            // Pages or slides whose text came from OCR, so sources can say "p. 4 (scanned)".
            QStringLiteral("CREATE TABLE IF NOT EXISTS document_units ("
                           " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                           " unit_number INTEGER NOT NULL,"
                           " ocr INTEGER NOT NULL DEFAULT 0,"
                           " ocr_confidence REAL,"
                           " PRIMARY KEY (document_id, unit_number))"),
        };
        for (const QString& sql : upgradeV4) {
            if (!exec(sql)) {
                return fail();
            }
        }
    }
    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        return fail();
    }
    const bool committed = db.commit();
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    return committed;
}

std::optional<qint64> StudyDatabase::addStudySet(const QString& name)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty()) {
        m_lastError = QStringLiteral("Study set name is empty.");
        return std::nullopt;
    }
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO study_sets(name, created_at) VALUES (?, ?)"));
    q.addBindValue(text(trimmed));
    q.addBindValue(nowMs());
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return std::nullopt;
    }
    return q.lastInsertId().toLongLong();
}

bool StudyDatabase::renameStudySet(qint64 id, const QString& name)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty()) {
        return false;
    }
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE study_sets SET name = ? WHERE id = ?"));
    q.addBindValue(text(trimmed));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

bool StudyDatabase::removeStudySet(qint64 id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM study_sets WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

QVector<StudySet> StudyDatabase::studySets()
{
    QVector<StudySet> out;
    QSqlQuery q(connection());
    q.exec(QStringLiteral("SELECT id, name FROM study_sets ORDER BY name COLLATE NOCASE"));
    while (q.next()) {
        out.append({q.value(0).toLongLong(), q.value(1).toString()});
    }
    return out;
}

std::optional<qint64> StudyDatabase::addDocument(qint64 setId, const QString& title,
                                                 const QString& originalPath, DocumentKind kind)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO documents(set_id, title, original_path, kind, status, created_at, updated_at)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(setId);
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

QVector<DocumentRecord> StudyDatabase::documents(qint64 setId)
{
    QVector<DocumentRecord> out;
    QSqlQuery q(connection());
    if (setId > 0) {
        q.prepare(QStringLiteral("SELECT %1 FROM documents d WHERE d.set_id = ? ORDER BY d.created_at, d.id")
                      .arg(kDocumentColumns));
        q.addBindValue(setId);
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
    ins.prepare(QStringLiteral("INSERT INTO chunks(document_id, set_id, ref_kind, ref_number, ordinal, text)"
                               " VALUES (?, ?, ?, ?, ?, ?)"));
    for (const Chunk& c : chunks) {
        ins.addBindValue(documentId);
        ins.addBindValue(doc->setId);
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
    q.prepare(QStringLiteral("SELECT id, document_id, set_id, ref_kind, ref_number, ordinal, text"
                             " FROM chunks WHERE document_id = ? ORDER BY ordinal"));
    q.addBindValue(documentId);
    if (q.exec()) {
        while (q.next()) {
            Chunk c;
            c.id = q.value(0).toLongLong();
            c.documentId = q.value(1).toLongLong();
            c.setId = q.value(2).toLongLong();
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
    q.prepare(QStringLiteral("SELECT id, document_id, set_id, ref_kind, ref_number, ordinal, text"
                             " FROM chunks WHERE id = ?"));
    q.addBindValue(chunkId);
    if (q.exec() && q.next()) {
        Chunk c;
        c.id = q.value(0).toLongLong();
        c.documentId = q.value(1).toLongLong();
        c.setId = q.value(2).toLongLong();
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
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO questions(id, document_id, set_id, chunk_id, type, prompt,"
                             " concept, difficulty, payload, model, created_at, origin, edited_at)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(question.id);
    q.addBindValue(question.documentId);
    q.addBindValue(question.setId);
    q.addBindValue(chunkId > 0 ? QVariant(chunkId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(static_cast<int>(question.type));
    q.addBindValue(text(question.prompt));
    q.addBindValue(text(question.topic));
    q.addBindValue(static_cast<int>(question.difficulty));
    q.addBindValue(QString::fromUtf8(QJsonDocument(questionPayload(question)).toJson(QJsonDocument::Compact)));
    q.addBindValue(text(model));
    q.addBindValue(nowMs());
    q.addBindValue(static_cast<int>(question.origin));
    q.addBindValue(question.editedAt > 0 ? QVariant(question.editedAt) : QVariant(QMetaType::fromType<qint64>()));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<Question> StudyDatabase::question(const QString& id)
{
    std::optional<Question> out;
    {
        QSqlQuery q(connection());
        q.prepare(QStringLiteral("SELECT ") + kQuestionColumns + kQuestionFrom + QStringLiteral(" WHERE qn.id = ?"));
        q.addBindValue(id);
        if (q.exec() && q.next()) {
            out = readQuestion(q);
        }
    }
    if (out) {
        resolveSupport(*out);
    }
    return out;
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
    q.finish();
    resolveSupport(out);
    return out;
}

QVector<Question> StudyDatabase::setQuestions(qint64 setId, const QList<QuestionType>& types, bool selectedOnly)
{
    QVector<Question> out;
    QString sql = QStringLiteral("SELECT ") + kQuestionColumns + kQuestionFrom + QStringLiteral(" WHERE qn.set_id = ?");
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
    q.addBindValue(setId);
    if (q.exec()) {
        while (q.next()) {
            out.append(readQuestion(q));
        }
    }
    q.finish();
    resolveSupport(out);
    return out;
}

bool StudyDatabase::updateQuestion(const Question& question)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE questions SET type = ?, prompt = ?, concept = ?, difficulty = ?, payload = ?,"
                             " origin = ?, edited_at = ? WHERE id = ?"));
    q.addBindValue(static_cast<int>(question.type));
    q.addBindValue(text(question.prompt));
    q.addBindValue(text(question.topic));
    q.addBindValue(static_cast<int>(question.difficulty));
    q.addBindValue(QString::fromUtf8(QJsonDocument(questionPayload(question)).toJson(QJsonDocument::Compact)));
    q.addBindValue(static_cast<int>(question.origin));
    q.addBindValue(question.editedAt > 0 ? QVariant(question.editedAt) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(question.id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool StudyDatabase::removeQuestion(const QString& id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM questions WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

std::optional<qint64> StudyDatabase::manualDocument(qint64 setId)
{
    {
        QSqlQuery q(connection());
        q.prepare(QStringLiteral("SELECT id FROM documents WHERE set_id = ? AND kind = ? ORDER BY id LIMIT 1"));
        q.addBindValue(setId);
        q.addBindValue(static_cast<int>(DocumentKind::Manual));
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
    }
    const auto id = addDocument(setId, QStringLiteral("My questions"), QString(), DocumentKind::Manual);
    if (id) {
        setDocumentStatus(*id, DocumentStatus::Ready);
    }
    return id;
}

bool StudyDatabase::removeQuestions(qint64 documentId)
{
    QSqlQuery check(connection());
    check.prepare(QStringLiteral("SELECT kind FROM documents WHERE id = ?"));
    check.addBindValue(documentId);
    if (check.exec() && check.next() && check.value(0).toInt() == static_cast<int>(DocumentKind::Manual)) {
        return false;   // never bulk-delete what the student wrote; use removeQuestion
    }
    check.finish();

    // Questions the student edited are theirs now: regenerating a document keeps them.
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM questions WHERE document_id = ? AND origin = ?"));
    q.addBindValue(documentId);
    q.addBindValue(static_cast<int>(QuestionOrigin::Generated));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<qint64> StudyDatabase::addSupportAsset(const SupportAsset& a)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO support_assets(document_id, unit_kind, unit_number, kind, path, caption, width, height,"
                             " table_json, text, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(a.documentId);
    q.addBindValue(static_cast<int>(a.unitKind));
    q.addBindValue(a.unitNumber);
    q.addBindValue(static_cast<int>(a.kind));
    q.addBindValue(text(a.path));
    q.addBindValue(text(a.caption));
    q.addBindValue(a.width);
    q.addBindValue(a.height);
    q.addBindValue(text(StudyText::tableToJson(a.table)));
    q.addBindValue(text(a.text));
    q.addBindValue(nowMs());
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return std::nullopt;
    }
    return q.lastInsertId().toLongLong();
}

std::optional<SupportAsset> StudyDatabase::supportAsset(qint64 id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT %1 FROM support_assets WHERE id = ?").arg(kAssetColumns));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readAsset(q);
    }
    return std::nullopt;
}

QVector<SupportAsset> StudyDatabase::supportAssets(qint64 documentId, int unitNumber)
{
    QVector<SupportAsset> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT %1 FROM support_assets WHERE document_id = ?%2 ORDER BY unit_number, id")
                  .arg(kAssetColumns, unitNumber > 0 ? QStringLiteral(" AND unit_number = %1").arg(unitNumber) : QString()));
    q.addBindValue(documentId);
    if (q.exec()) {
        while (q.next()) {
            out.append(readAsset(q));
        }
    }
    return out;
}

bool StudyDatabase::removeSupportAssets(qint64 documentId)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM support_assets WHERE document_id = ?"));
    q.addBindValue(documentId);
    const bool ok = q.exec();
    QDir(assetsDirectory(documentId)).removeRecursively();
    return ok;
}

bool StudyDatabase::setAssetInterpretation(qint64 id, const QString& interpretation, const QString& model, bool legible,
                                           const QString& readableText)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE support_assets SET interpretation = ?, interpreted_by = ?, interpreted_at = ?, legible = ?,"
                             " text = CASE WHEN ? = '' THEN text ELSE ? END WHERE id = ?"));
    q.addBindValue(text(interpretation));
    q.addBindValue(text(model));
    q.addBindValue(nowMs());
    q.addBindValue(legible ? 1 : 0);
    q.addBindValue(text(readableText));
    q.addBindValue(text(readableText));
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return q.numRowsAffected() == 1;
}

void StudyDatabase::resolveSupport(Question& question)
{
    for (SupportRef& ref : question.support) {
        if (ref.assetId <= 0) {
            continue;
        }
        if (const auto asset = supportAsset(ref.assetId)) {
            ref = refFromAsset(*asset);
        }
    }
    // An asset that no longer exists (document re-read) is dropped rather than shown empty.
    question.support.erase(std::remove_if(question.support.begin(), question.support.end(),
                                          [](const SupportRef& r) {
                                              return r.kind == SupportKind::Image ? r.imagePath.isEmpty()
                                                                                  : r.table.isEmpty() && r.text.isEmpty();
                                          }),
                           question.support.end());
}

void StudyDatabase::resolveSupport(QVector<Question>& questions)
{
    for (Question& q : questions) {
        if (!q.support.isEmpty()) {
            resolveSupport(q);
        }
    }
}

bool StudyDatabase::setUnitOcr(qint64 documentId, int unitNumber, double confidence)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO document_units(document_id, unit_number, ocr, ocr_confidence) VALUES (?, ?, 1, ?)"
                             " ON CONFLICT(document_id, unit_number) DO UPDATE SET ocr = 1, ocr_confidence = excluded.ocr_confidence"));
    q.addBindValue(documentId);
    q.addBindValue(unitNumber);
    q.addBindValue(confidence);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool StudyDatabase::clearOcrUnits(qint64 documentId)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("DELETE FROM document_units WHERE document_id = ?"));
    q.addBindValue(documentId);
    return q.exec();
}

QHash<int, double> StudyDatabase::ocrUnits(qint64 documentId)
{
    QHash<int, double> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT unit_number, ocr_confidence FROM document_units WHERE document_id = ? AND ocr = 1"));
    q.addBindValue(documentId);
    if (q.exec()) {
        while (q.next()) {
            out.insert(q.value(0).toInt(), q.value(1).toDouble());
        }
    }
    return out;
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
