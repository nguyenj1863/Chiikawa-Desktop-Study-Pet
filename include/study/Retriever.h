#pragma once

#include "study/StudyTypes.h"

#include <QList>
#include <QString>

class StudyDatabase;

/**
 * Finds study passages relevant to a query. FtsRetriever (keyword/BM25) is the
 * first implementation; an embedding-based retriever can implement the same
 * interface later without changing callers.
 */
class Retriever {
public:
    virtual ~Retriever() = default;

    /**
     * Returns up to limit chunks from the study set, best first. When documentIds is
     * non-empty, results are restricted to those documents.
     */
    [[nodiscard]] virtual QVector<SearchHit> search(const QString& query,
                                                    qint64 setId,
                                                    int limit,
                                                    const QList<qint64>& documentIds = {}) = 0;
};

/** SQLite FTS5 retriever with the porter stemmer and BM25 ranking. */
class FtsRetriever : public Retriever {
public:
    explicit FtsRetriever(StudyDatabase* db) : m_db(db) {}

    [[nodiscard]] QVector<SearchHit> search(const QString& query,
                                            qint64 setId,
                                            int limit,
                                            const QList<qint64>& documentIds = {}) override;

    /** Builds a safe FTS5 MATCH expression (quoted terms joined by OR); empty when no terms. */
    [[nodiscard]] static QString matchExpression(const QString& query);

private:
    StudyDatabase* m_db = nullptr;
};
