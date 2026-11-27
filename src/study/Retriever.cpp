#include "study/Retriever.h"

#include "study/StudyDatabase.h"

#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>

namespace {

const QSet<QString>& stopWords()
{
    static const QSet<QString> words = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("are"),
        QStringLiteral("what"), QStringLiteral("which"), QStringLiteral("with"), QStringLiteral("that"),
        QStringLiteral("this"), QStringLiteral("from"), QStringLiteral("does"), QStringLiteral("how"),
        QStringLiteral("why"), QStringLiteral("when"), QStringLiteral("into"), QStringLiteral("its"),
        QStringLiteral("not"), QStringLiteral("but"), QStringLiteral("all"), QStringLiteral("can"),
        QStringLiteral("you"), QStringLiteral("your"), QStringLiteral("was"), QStringLiteral("were"),
        QStringLiteral("has"), QStringLiteral("have"), QStringLiteral("between"), QStringLiteral("about"),
        QStringLiteral("of"), QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("to"),
        QStringLiteral("is"), QStringLiteral("an"), QStringLiteral("or"), QStringLiteral("as"),
        QStringLiteral("at"), QStringLiteral("by"), QStringLiteral("be"), QStringLiteral("it"),
        QStringLiteral("do"), QStringLiteral("if"), QStringLiteral("so"), QStringLiteral("we"),
    };
    return words;
}

} // namespace

QString FtsRetriever::matchExpression(const QString& query)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}]+"));
    QStringList terms;
    QSet<QString> seen;
    auto it = word.globalMatch(query.toLower());
    while (it.hasNext() && terms.size() < 24) {
        const QString w = it.next().captured(0);
        if (w.size() < 2 || stopWords().contains(w) || seen.contains(w)) {
            continue;
        }
        seen.insert(w);
        terms.append(QLatin1Char('"') + w + QLatin1Char('"'));
    }
    return terms.join(QStringLiteral(" OR "));
}

QVector<SearchHit> FtsRetriever::search(const QString& query, qint64 setId, int limit,
                                        const QList<qint64>& documentIds)
{
    QVector<SearchHit> out;
    const QString match = matchExpression(query);
    if (!m_db || match.isEmpty() || limit <= 0) {
        return out;
    }
    QString sql = QStringLiteral(
        "SELECT c.id, c.document_id, c.set_id, c.ref_kind, c.ref_number, c.ordinal, c.text, bm25(chunks_fts)"
        " FROM chunks_fts JOIN chunks c ON c.id = chunks_fts.rowid"
        " WHERE chunks_fts MATCH ? AND c.set_id = ?");
    if (!documentIds.isEmpty()) {
        QStringList ids;
        for (const qint64 id : documentIds) {
            ids.append(QString::number(id));
        }
        sql += QStringLiteral(" AND c.document_id IN (%1)").arg(ids.join(QLatin1Char(',')));
    }
    sql += QStringLiteral(" ORDER BY bm25(chunks_fts) LIMIT ?");

    QSqlQuery q(m_db->connection());
    q.prepare(sql);
    q.addBindValue(match);
    q.addBindValue(setId);
    q.addBindValue(limit);
    if (!q.exec()) {
        return out;
    }
    while (q.next()) {
        SearchHit hit;
        hit.chunk.id = q.value(0).toLongLong();
        hit.chunk.documentId = q.value(1).toLongLong();
        hit.chunk.setId = q.value(2).toLongLong();
        hit.chunk.refKind = static_cast<SegmentKind>(q.value(3).toInt());
        hit.chunk.refNumber = q.value(4).toInt();
        hit.chunk.ordinal = q.value(5).toInt();
        hit.chunk.text = q.value(6).toString();
        hit.score = -q.value(7).toDouble();   // bm25() is lower-is-better
        out.append(hit);
    }
    return out;
}
