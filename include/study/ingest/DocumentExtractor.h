#pragma once

#include "study/StudyTypes.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

/**
 * Local text extraction with page/slide references. Runs on worker threads;
 * creates no widgets. Pages or slides without extractable text (scans,
 * picture-only slides) are reported in unsupportedUnits instead of being
 * silently skipped; the ingestion worker then reads them with OcrEngine. Figures and tables
 * are returned as assets so questions can show them.
 */
namespace DocumentExtractor {

inline constexpr qint64 kMaxFileBytes = 200LL * 1024 * 1024;

/** A page with fewer non-space characters than this is treated as scanned/image-only. */
inline constexpr int kMinPageTextChars = 25;

[[nodiscard]] bool pdfSupported();

/** Dispatches on the file extension (.pdf, .pptx, .txt, .md). */
[[nodiscard]] ExtractionResult extractFile(const QString& path);

[[nodiscard]] ExtractionResult extractPdf(const QString& path);
[[nodiscard]] ExtractionResult extractPptx(const QByteArray& archive);
[[nodiscard]] ExtractionResult extractText(const QByteArray& bytes, bool markdown);
/** An image file: one page, kept as a figure, with its text left for OCR. */
[[nodiscard]] ExtractionResult extractImage(const QString& path);

/** Renders a PDF page (1-based) on white with the given long edge, for OCR; null if it fails. */
[[nodiscard]] QImage renderPdfPageImage(const QString& path, int pageNumber, int longEdge);

/**
 * Where a captioned figure or table sits on a page, in page units: the caption plus the largest
 * band free of body text next to it (above a figure caption, below a table caption, the other
 * side if that one is too small). Body text lines are those wider than 40% of the page, so labels
 * inside a diagram do not split it. Returns a null rect when no band covers at least 15% of the
 * page height; the caller then keeps the whole page.
 */
[[nodiscard]] QRectF figureRegion(const QSizeF& pageSize, const QRectF& caption, const QList<QRectF>& textLines,
                                  bool tableCaption);

} // namespace DocumentExtractor
