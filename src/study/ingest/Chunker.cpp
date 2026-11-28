#include "study/ingest/Chunker.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace Chunker {
namespace {

bool isPageNumberLine(const QString& line)
{
    static const QRegularExpression re(QStringLiteral("^(page\\s*)?\\d{1,4}(\\s*(/|of)\\s*\\d{1,4})?$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(line.trimmed()).hasMatch();
}

/** Splits an over-long paragraph at sentence boundaries. */
QStringList splitSentences(const QString& paragraph)
{
    static const QRegularExpression boundary(QStringLiteral("(?<=[.!?])\\s+(?=[A-Z0-9(\"'])"));
    QStringList pieces = paragraph.split(boundary, Qt::SkipEmptyParts);
    QStringList out;
    for (QString piece : pieces) {
        // Hard wrap pathological sentences (tables, code) at word boundaries.
        while (piece.size() > kMaxChars) {
            qsizetype cut = piece.lastIndexOf(QLatin1Char(' '), kTargetChars);
            if (cut <= 0) {
                cut = kTargetChars;
            }
            out.append(piece.left(cut).trimmed());
            piece = piece.mid(cut).trimmed();
        }
        if (!piece.isEmpty()) {
            out.append(piece);
        }
    }
    return out;
}

int visible(const QString& s)
{
    int n = 0;
    for (const QChar ch : s) {
        if (!ch.isSpace()) {
            ++n;
        }
    }
    return n;
}

} // namespace

QString cleanText(const QString& text)
{
    QString t = text;
    t.replace(QChar(0x00AD), QString());                 // soft hyphen
    t.replace(QChar(0xFB01), QStringLiteral("fi"));      // common ligatures in PDFs
    t.replace(QChar(0xFB02), QStringLiteral("fl"));
    t.replace(QChar(0x2022), QStringLiteral("\n"));     // bullet glyphs start a new line
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    static const QRegularExpression hyphenBreak(QStringLiteral("(\\w)-\\n\\s*([a-z])"));
    t.replace(hyphenBreak, QStringLiteral("\\1\\2"));

    // Paragraphs are separated by blank lines or by lines that look like list items.
    const QStringList lines = t.split(QLatin1Char('\n'));
    QStringList paragraphs;
    QString current;
    static const QRegularExpression listItem(QStringLiteral("^\\s*([-*•▪◦]|\\d{1,2}[.)])\\s+"));
    for (const QString& raw : lines) {
        const QString line = raw.simplified();
        if (line.isEmpty()) {
            if (!current.isEmpty()) {
                paragraphs.append(current);
                current.clear();
            }
            continue;
        }
        if (listItem.match(raw).hasMatch() && !current.isEmpty()) {
            paragraphs.append(current);
            current.clear();
        }
        current = current.isEmpty() ? line : current + QLatin1Char(' ') + line;
    }
    if (!current.isEmpty()) {
        paragraphs.append(current);
    }
    return paragraphs.join(QStringLiteral("\n\n"));
}

QStringList repeatedLines(const QVector<Segment>& segments)
{
    int pages = 0;
    QHash<QString, int> counts;
    for (const Segment& seg : segments) {
        if (seg.kind != SegmentKind::Page) {
            continue;
        }
        ++pages;
        QSet<QString> seen;
        for (const QString& raw : seg.text.split(QLatin1Char('\n'))) {
            const QString line = raw.simplified();
            if (!line.isEmpty() && line.size() <= 80 && !seen.contains(line)) {
                seen.insert(line);
                counts[line]++;
            }
        }
    }
    QStringList out;
    if (pages < 4) {
        return out;
    }
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() * 2 > pages) {
            out.append(it.key());
        }
    }
    return out;
}

QVector<Chunk> chunk(const QVector<Segment>& segments)
{
    const QStringList repeated = repeatedLines(segments);
    const QSet<QString> repeatedSet(repeated.cbegin(), repeated.cend());

    QVector<Chunk> out;
    int ordinal = 0;
    for (const Segment& seg : segments) {
        QStringList kept;
        for (const QString& raw : seg.text.split(QLatin1Char('\n'))) {
            const QString line = raw.simplified();
            if (seg.kind == SegmentKind::Page && (repeatedSet.contains(line) || isPageNumberLine(line))) {
                continue;
            }
            kept.append(raw);
        }
        const QString cleaned = cleanText(kept.join(QLatin1Char('\n')));
        if (visible(cleaned) < kMinChars) {
            continue;
        }

        QStringList units;
        for (const QString& para : cleaned.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
            if (para.size() > kMaxChars) {
                units.append(splitSentences(para));
            } else {
                units.append(para);
            }
        }

        QString current;
        const auto flush = [&]() {
            if (visible(current) >= kMinChars) {
                Chunk c;
                c.refKind = seg.kind;
                c.refNumber = seg.number;
                c.ordinal = ordinal++;
                c.text = current.trimmed();
                out.append(c);
            } else if (!current.trimmed().isEmpty() && !out.isEmpty()
                       && out.last().refKind == seg.kind && out.last().refNumber == seg.number
                       && out.last().text.size() + current.size() < kMaxChars) {
                // Fold a short tail into the previous chunk from the same location.
                out.last().text += QStringLiteral("\n") + current.trimmed();
            }
            current.clear();
        };
        for (const QString& unit : units) {
            if (!current.isEmpty() && current.size() + unit.size() + 1 > kTargetChars) {
                flush();
            }
            current = current.isEmpty() ? unit : current + QLatin1Char('\n') + unit;
        }
        flush();
    }
    return out;
}

} // namespace Chunker
