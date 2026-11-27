#pragma once

#include "study/StudyTypes.h"

#include <QByteArray>
#include <QString>

/**
 * Local text extraction with page/slide references. Runs on worker threads;
 * creates no widgets. Pages or slides without extractable text (scans,
 * picture-only slides) are reported in unsupportedUnits instead of being
 * silently skipped. OCR and image understanding are not performed.
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

} // namespace DocumentExtractor
