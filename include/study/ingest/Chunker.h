#pragma once

#include "study/StudyTypes.h"

/**
 * Cleans extracted segments and splits them into retrieval-sized chunks.
 * A chunk never spans two pages or slides, so every chunk keeps an exact
 * source reference.
 */
namespace Chunker {

inline constexpr int kTargetChars = 800;
inline constexpr int kMaxChars = 1100;
inline constexpr int kMinChars = 40;

/** Joins hyphenated line breaks, unwraps lines, and normalizes whitespace. */
[[nodiscard]] QString cleanText(const QString& text);

/**
 * Lines that repeat on most pages (running headers/footers) or that are just
 * page numbers. Only applied to documents with at least 4 page segments.
 */
[[nodiscard]] QStringList repeatedLines(const QVector<Segment>& segments);

[[nodiscard]] QVector<Chunk> chunk(const QVector<Segment>& segments);

} // namespace Chunker
