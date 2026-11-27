#include "study/Retriever.h"
#include "study/StudyDatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// The version 1 schema exactly as shipped (tables named "courses" with course_id columns).
const QStringList kVersion1Schema = {
    "CREATE TABLE courses (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, created_at INTEGER NOT NULL)",
    "CREATE TABLE documents (id INTEGER PRIMARY KEY, course_id INTEGER NOT NULL REFERENCES courses(id) ON DELETE CASCADE,"
    " title TEXT NOT NULL, original_path TEXT NOT NULL DEFAULT '', stored_path TEXT NOT NULL DEFAULT '',"
    " kind INTEGER NOT NULL, status INTEGER NOT NULL, error TEXT NOT NULL DEFAULT '', unit_count INTEGER NOT NULL DEFAULT 0,"
    " unsupported TEXT NOT NULL DEFAULT '', selected INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL,"
    " updated_at INTEGER NOT NULL)",
    "CREATE TABLE chunks (id INTEGER PRIMARY KEY, document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
    " course_id INTEGER NOT NULL, ref_kind INTEGER NOT NULL, ref_number INTEGER NOT NULL, ordinal INTEGER NOT NULL,"
    " text TEXT NOT NULL)",
    "CREATE INDEX chunks_document ON chunks(document_id)",
    "CREATE VIRTUAL TABLE chunks_fts USING fts5(text, content='chunks', content_rowid='id', tokenize='porter unicode61')",
    "CREATE TRIGGER chunks_ai AFTER INSERT ON chunks BEGIN INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text); END",
    "CREATE TRIGGER chunks_ad AFTER DELETE ON chunks BEGIN"
    " INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text); END",
    "CREATE TRIGGER chunks_au AFTER UPDATE ON chunks BEGIN"
    " INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text);"
    " INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text); END",
    "CREATE TABLE questions (id TEXT PRIMARY KEY, document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
    " course_id INTEGER NOT NULL, chunk_id INTEGER REFERENCES chunks(id) ON DELETE SET NULL, type INTEGER NOT NULL,"
    " prompt TEXT NOT NULL, concept TEXT NOT NULL DEFAULT '', difficulty INTEGER NOT NULL DEFAULT 1,"
    " payload TEXT NOT NULL, model TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL)",
    "CREATE INDEX questions_course ON questions(course_id, type)",
    "CREATE TABLE attempts (id INTEGER PRIMARY KEY, question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,"
    " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE, course_id INTEGER NOT NULL,"
    " type INTEGER NOT NULL, kind INTEGER NOT NULL, verdict INTEGER NOT NULL, score REAL NOT NULL, xp INTEGER NOT NULL,"
    " answered_at INTEGER NOT NULL)",
    "CREATE INDEX attempts_course ON attempts(course_id, answered_at)",
    "CREATE TABLE concept_stats (document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
    " course_id INTEGER NOT NULL, concept TEXT NOT NULL, seen INTEGER NOT NULL DEFAULT 0,"
    " correct INTEGER NOT NULL DEFAULT 0, partial INTEGER NOT NULL DEFAULT 0, incorrect INTEGER NOT NULL DEFAULT 0,"
    " last_seen INTEGER NOT NULL DEFAULT 0, needs_review INTEGER NOT NULL DEFAULT 0, due_at INTEGER, ease REAL,"
    " PRIMARY KEY (document_id, concept))",
};

/** Builds a populated version 1 library file in the directory. */
bool buildVersion1Library(const QString& dir, bool withDanglingDocument = false)
{
    const QString name = QStringLiteral("migration_fixture_%1").arg(QTest::currentTestFunction());
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(dir + QStringLiteral("/library.db"));
        ok = db.open();
        QSqlQuery q(db);
        for (const QString& sql : kVersion1Schema) {
            ok = ok && q.exec(sql);
        }
        const QStringList data = {
            "INSERT INTO courses(id, name, created_at) VALUES (1, 'Databases', 1000), (2, 'Biology', 2000)",
            "INSERT INTO documents(id, course_id, title, kind, status, created_at, updated_at) VALUES"
            " (10, 1, 'Joins', 1, 4, 1000, 1000), (11, 2, 'Cells', 0, 2, 2000, 2000)",
            "INSERT INTO chunks(id, document_id, course_id, ref_kind, ref_number, ordinal, text) VALUES"
            " (100, 10, 1, 1, 4, 0, 'A LEFT JOIN returns every row from the left table.'),"
            " (101, 11, 2, 0, 2, 0, 'Mitochondria produce most of the cell energy.')",
            "INSERT INTO questions(id, document_id, course_id, chunk_id, type, prompt, concept, difficulty, payload, created_at)"
            " VALUES ('q1', 10, 1, 100, 1, 'What does a LEFT JOIN return?', 'left join', 1, '{}', 3000)",
            "INSERT INTO attempts(question_id, document_id, course_id, type, kind, verdict, score, xp, answered_at)"
            " VALUES ('q1', 10, 1, 1, 0, 0, 1.0, 20, 4000), ('q1', 10, 1, 1, 1, 2, 0.0, 0, 5000)",
            "INSERT INTO concept_stats(document_id, course_id, concept, seen, correct, needs_review) VALUES (10, 1, 'left join', 2, 1, 1)",
        };
        for (const QString& sql : data) {
            ok = ok && q.exec(sql);
        }
        if (withDanglingDocument) {
            q.exec(QStringLiteral("INSERT INTO documents(id, course_id, title, kind, status, created_at, updated_at)"
                                  " VALUES (12, 99, 'Orphan', 1, 4, 1, 1)"));
        }
        ok = ok && q.exec(QStringLiteral("PRAGMA user_version = 1"));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QStringList columns(StudyDatabase& db, const QString& table)
{
    QSqlQuery q(db.connection());
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    QStringList out;
    while (q.next()) {
        out.append(q.value(1).toString());
    }
    return out;
}

int scalar(StudyDatabase& db, const QString& sql)
{
    QSqlQuery q(db.connection());
    q.exec(sql);
    return q.next() ? q.value(0).toInt() : -1;
}

} // namespace

class TestStudyMigration : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void upgradesVersion1KeepingEveryRow();
    void backsUpTheOldFileBeforeUpgrading();
    void upgradedLibraryAcceptsNestedSetsAndNewTables();
    void failedUpgradeLeavesTheVersion1FileUsable();
    void freshLibrariesStartAtTheCurrentVersion();

private:
    QTemporaryDir* m_dir = nullptr;
};

void TestStudyMigration::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    StudyDatabase::setStorageDirectory(m_dir->path());
}

void TestStudyMigration::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

void TestStudyMigration::upgradesVersion1KeepingEveryRow()
{
    QVERIFY(buildVersion1Library(m_dir->path()));
    StudyDatabase db;
    QVERIFY2(db.open(), qPrintable(db.lastError()));
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")), StudyDatabase::kSchemaVersion);

    // Courses became study sets with their IDs and names intact.
    const QVector<StudySet> sets = db.studySets();
    QCOMPARE(sets.size(), 2);
    QCOMPARE(sets.at(0).name, QStringLiteral("Biology"));
    QCOMPARE(sets.at(1).name, QStringLiteral("Databases"));
    QCOMPARE(sets.at(1).id, 1);
    QVERIFY(columns(db, QStringLiteral("study_sets")).contains(QStringLiteral("created_at")));
    QVERIFY(columns(db, QStringLiteral("study_sets")).contains(QStringLiteral("parent_id")));

    // Documents, chunks, questions, attempts, and concept stats all follow their set.
    for (const QString& table : {QStringLiteral("documents"), QStringLiteral("chunks"), QStringLiteral("questions"),
                                 QStringLiteral("attempts"), QStringLiteral("concept_stats")}) {
        const QStringList cols = columns(db, table);
        QVERIFY2(cols.contains(QStringLiteral("set_id")), qPrintable(table));
        QVERIFY2(!cols.contains(QStringLiteral("course_id")), qPrintable(table));
    }
    QCOMPARE(db.documents(1).size(), 1);
    QCOMPARE(db.documents(1).first().title, QStringLiteral("Joins"));
    QCOMPARE(db.documents(1).first().setId, 1);
    QCOMPARE(db.documents(2).first().title, QStringLiteral("Cells"));
    QCOMPARE(db.chunks(10).size(), 1);
    QCOMPARE(db.chunks(10).first().setId, 1);
    const auto question = db.question(QStringLiteral("q1"));
    QVERIFY(question);
    QCOMPARE(question->setId, 1);
    QCOMPARE(question->category, QStringLiteral("Databases"));   // joined from study_sets
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM attempts")), 2);
    QCOMPARE(scalar(db, QStringLiteral("SELECT SUM(xp) FROM attempts")), 20);
    QCOMPARE(scalar(db, QStringLiteral("SELECT needs_review FROM concept_stats WHERE concept = 'left join'")), 1);

    // Full-text search still finds the migrated chunks, scoped by set.
    FtsRetriever retriever(&db);
    QCOMPARE(retriever.search(QStringLiteral("left join"), 1, 5).size(), 1);
    QVERIFY(retriever.search(QStringLiteral("left join"), 2, 5).isEmpty());
    QCOMPARE(retriever.search(QStringLiteral("mitochondria"), 2, 5).first().chunk.setId, 2);

    // Cascades still work through the renamed tables.
    QVERIFY(db.removeStudySet(1));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM documents")), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM questions")), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM attempts")), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM concept_stats")), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM chunks_fts WHERE chunks_fts MATCH 'join'")), 0);
}

void TestStudyMigration::backsUpTheOldFileBeforeUpgrading()
{
    QVERIFY(buildVersion1Library(m_dir->path()));
    const QByteArray original = [&]() {
        QFile f(m_dir->filePath(QStringLiteral("library.db")));
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }();
    {
        StudyDatabase db;
        QVERIFY2(db.open(), qPrintable(db.lastError()));
    }
    const QString backup = m_dir->filePath(QString::fromLatin1(StudyDatabase::kVersion1BackupName));
    QVERIFY(QFileInfo::exists(backup));

    // The backup is a usable version 1 library with the original course tables.
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("backup_check"));
        raw.setDatabaseName(backup);
        QVERIFY(raw.open());
        QSqlQuery q(raw);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM courses")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        q.finish();
        raw.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("backup_check"));
    QVERIFY(!original.isEmpty());
}

void TestStudyMigration::upgradedLibraryAcceptsNestedSetsAndNewTables()
{
    QVERIFY(buildVersion1Library(m_dir->path()));
    StudyDatabase db;
    QVERIFY2(db.open(), qPrintable(db.lastError()));
    QSqlQuery q(db.connection());

    // Sub-sets: the same name is allowed under different parents but not twice under one parent.
    QVERIFY(q.exec(QStringLiteral("INSERT INTO study_sets(name, parent_id, created_at) VALUES ('Week 1', 1, 1)")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO study_sets(name, parent_id, created_at) VALUES ('Week 1', 2, 1)")));
    QVERIFY(!q.exec(QStringLiteral("INSERT INTO study_sets(name, parent_id, created_at) VALUES ('WEEK 1', 1, 1)")));
    QVERIFY(!q.exec(QStringLiteral("INSERT INTO study_sets(name, parent_id, created_at) VALUES ('databases', NULL, 1)")));
    // Deleting a parent removes its sub-sets.
    QVERIFY(q.exec(QStringLiteral("DELETE FROM study_sets WHERE id = 2")));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM study_sets")), 2);   // Databases + its Week 1

    // New tables exist and are empty; the rating columns are added to attempts.
    for (const QString& table : {QStringLiteral("card_schedule"), QStringLiteral("practice_sessions"), QStringLiteral("filtered_sets")}) {
        QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)), 0);
    }
    QVERIFY(columns(db, QStringLiteral("attempts")).contains(QStringLiteral("rating")));
    QVERIFY(columns(db, QStringLiteral("attempts")).contains(QStringLiteral("rating_source")));
    // Existing attempts keep working with no rating yet.
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM attempts WHERE rating IS NULL AND rating_source = 0")), 2);

    // A schedule row follows its question's lifetime.
    QVERIFY(q.exec(QStringLiteral("INSERT INTO card_schedule(question_id, set_id) VALUES ('q1', 1)")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM documents WHERE id = 10")));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM card_schedule")), 0);
}

void TestStudyMigration::failedUpgradeLeavesTheVersion1FileUsable()
{
    QVERIFY(buildVersion1Library(m_dir->path(), true));   // a document points at a course that does not exist
    {
        StudyDatabase db;
        QVERIFY(!db.open());
        QVERIFY(!db.lastError().isEmpty());
    }
    // Nothing was changed: still version 1, still the original tables, and a backup exists.
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("failed_check"));
        raw.setDatabaseName(m_dir->filePath(QStringLiteral("library.db")));
        QVERIFY(raw.open());
        QSqlQuery q(raw);
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM courses")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM documents")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 3);
        q.finish();
        raw.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("failed_check"));
    QVERIFY(QFileInfo::exists(m_dir->filePath(QString::fromLatin1(StudyDatabase::kVersion1BackupName))));
}

void TestStudyMigration::freshLibrariesStartAtTheCurrentVersion()
{
    StudyDatabase db;
    QVERIFY2(db.open(), qPrintable(db.lastError()));
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")), StudyDatabase::kSchemaVersion);
    QVERIFY(!QFileInfo::exists(m_dir->filePath(QString::fromLatin1(StudyDatabase::kVersion1BackupName))));   // nothing to back up
    const auto first = db.addStudySet(QStringLiteral("MCAT"));
    QVERIFY(first);
    QVERIFY(!db.addStudySet(QStringLiteral("mcat")).has_value());
    QCOMPARE(db.studySets().size(), 1);

    // Opening an already-current library twice changes nothing.
    StudyDatabase again;
    QVERIFY(again.open());
    QCOMPARE(again.studySets().size(), 1);
}

QTEST_MAIN(TestStudyMigration)
#include "test_study_migration.moc"
