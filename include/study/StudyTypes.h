#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Plain data types shared by the Study Library pipeline (extraction, storage,
 * retrieval, generation). No Qt object ownership or UI dependencies.
 */

enum class DocumentKind {
    Pdf,
    Pptx,
    Text,
    Unknown
};

/** Processing lifecycle of an imported document. Values are persisted; append only. */
enum class DocumentStatus {
    Queued = 0,
    Extracting = 1,
    Extracted = 2,     // text stored; questions not generated yet (e.g. no model available)
    Generating = 3,
    Ready = 4,         // questions generated
    Failed = 5,
    Unsupported = 6    // no extractable text at all (e.g. fully scanned PDF)
};

enum class SegmentKind {
    Page,
    Slide,
    SlideNotes,
    Section
};

/** One page, slide, slide-notes block, or text section with its source reference. */
struct Segment {
    SegmentKind kind = SegmentKind::Page;
    int number = 0;          // 1-based page or slide number; section ordinal for text
    QString heading;         // optional section heading (text notes)
    QString text;
};

struct ExtractionResult {
    bool ok = false;
    QString error;                 // set when ok == false
    DocumentKind kind = DocumentKind::Unknown;
    int unitCount = 0;             // total pages or slides
    QVector<Segment> segments;
    QList<int> unsupportedUnits;   // pages/slides with no extractable text (scans, images)
};

struct StudySet {
    qint64 id = 0;
    QString name;
};

struct DocumentRecord {
    qint64 id = 0;
    qint64 setId = 0;
    QString title;
    QString originalPath;
    QString storedPath;
    DocumentKind kind = DocumentKind::Unknown;
    DocumentStatus status = DocumentStatus::Queued;
    QString error;
    int unitCount = 0;
    QList<int> unsupportedUnits;
    bool selected = true;    // included in study sessions
    int chunkCount = 0;      // filled by queries that join counts
    int questionCount = 0;
};

/** A retrieval unit: cleaned text from one source location. */
struct Chunk {
    qint64 id = 0;
    qint64 documentId = 0;
    qint64 setId = 0;
    SegmentKind refKind = SegmentKind::Page;
    int refNumber = 0;
    int ordinal = 0;
    QString text;
};

struct SearchHit {
    Chunk chunk;
    double score = 0.0;   // higher is more relevant
};

namespace StudyText {

/** Human-readable source reference, e.g. "p. 5", "slide 3", "slide 3 notes", "section 2". */
[[nodiscard]] QString referenceLabel(SegmentKind kind, int number);

[[nodiscard]] QString statusLabel(DocumentStatus status);

[[nodiscard]] DocumentKind kindForPath(const QString& path);

[[nodiscard]] QString kindLabel(DocumentKind kind);

/** Compact "p. 1-3, 7" style list for unsupported pages/slides. */
[[nodiscard]] QString compactRanges(const QList<int>& numbers);

} // namespace StudyText
