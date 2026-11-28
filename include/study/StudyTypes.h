#pragma once

#include <QByteArray>
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
    Unknown,
    Manual,   // "My questions": questions the student wrote; no file, no text to generate from
    Image     // a photo, screenshot or scanned page; its text is read by OCR
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
    bool ocr = false;        // text was read from an image (scanned page, picture-only slide)
};

/** What a supporting visual is. Values are persisted; append only. */
enum class SupportKind {
    Image = 0,   // a picture, diagram, chart, or a rendered page
    Table = 1,   // a table with its cells
    Data = 2     // a short data snippet quoted from the material
};

/** A table: an optional header row and the body rows. */
struct TableData {
    QStringList headers;
    QVector<QStringList> rows;

    [[nodiscard]] bool isEmpty() const { return headers.isEmpty() && rows.isEmpty(); }
};

/** A figure or table found while reading a document, before it is stored. */
struct ExtractedAsset {
    SegmentKind unitKind = SegmentKind::Page;
    int unitNumber = 0;
    SupportKind kind = SupportKind::Image;
    QByteArray imageData;      // encoded image (PNG or JPEG) for SupportKind::Image
    QString imageFormat;       // "png" or "jpg"
    QString caption;           // alt text or the caption line found near it
    TableData table;           // for SupportKind::Table
    QString text;              // text read from the image by OCR
};

/**
 * A stored figure, table or data snippet that questions can show. Images are files under
 * StudyDatabase::assetsDirectory(documentId); a vision model may add an interpretation.
 */
struct SupportAsset {
    qint64 id = 0;
    qint64 documentId = 0;
    SegmentKind unitKind = SegmentKind::Page;
    int unitNumber = 0;
    SupportKind kind = SupportKind::Image;
    QString path;              // image file
    QString caption;
    int width = 0;
    int height = 0;
    TableData table;
    QString text;              // text read from the image (or a data snippet)
    QString interpretation;    // what a vision model saw, as readable text
    QString interpretedBy;     // model that produced it
    bool interpreted = false;
    bool legible = true;
};

struct ExtractionResult {
    bool ok = false;
    QString error;                 // set when ok == false
    DocumentKind kind = DocumentKind::Unknown;
    int unitCount = 0;             // total pages or slides
    QVector<Segment> segments;
    QList<int> unsupportedUnits;   // pages/slides with no extractable text (scans, images)
    QVector<ExtractedAsset> assets;   // pictures and tables to show beside questions
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

/** Table as plain text rows ("a | b | c"), for prompts and evidence checks. */
[[nodiscard]] QString tableToText(const TableData& table);
[[nodiscard]] QString tableToJson(const TableData& table);
[[nodiscard]] TableData tableFromJson(const QString& json);

/**
 * Turns symbol-font glyphs into real characters. Slides typeset maths in the Symbol font and
 * bullets in Wingdings; PDF text extraction returns both as private-use code points (U+F000 plus
 * the font's byte), so "∃ c ≥ 0" arrives as " c  0" and Θ as "". Standalone
 * Wingdings bullet bytes become "•", the rest map through the Symbol font's table; unknown ones
 * are dropped. Text without such code points is returned unchanged.
 */
[[nodiscard]] QString normalizeSymbolFonts(const QString& text);

/** Whether a slide/page/section asset belongs to a chunk's source unit (slide notes use their slide). */
[[nodiscard]] bool sameUnit(SegmentKind assetKind, SegmentKind chunkKind);

} // namespace StudyText
