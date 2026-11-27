#include "study/DocumentExtractor.h"

#include "study/ZipReader.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <algorithm>

#ifdef CHIIKAWA_HAS_QTPDF
#  include <QPdfDocument>
#  include <QPdfSelection>
#endif

namespace StudyText {

QString referenceLabel(SegmentKind kind, int number)
{
    switch (kind) {
    case SegmentKind::Page:
        return QStringLiteral("p. %1").arg(number);
    case SegmentKind::Slide:
        return QStringLiteral("slide %1").arg(number);
    case SegmentKind::SlideNotes:
        return QStringLiteral("slide %1 notes").arg(number);
    case SegmentKind::Section:
        return QStringLiteral("section %1").arg(number);
    }
    return QString();
}

QString statusLabel(DocumentStatus status)
{
    switch (status) {
    case DocumentStatus::Queued:
        return QStringLiteral("Queued");
    case DocumentStatus::Extracting:
        return QStringLiteral("Reading…");
    case DocumentStatus::Extracted:
        return QStringLiteral("Text ready");
    case DocumentStatus::Generating:
        return QStringLiteral("Generating…");
    case DocumentStatus::Ready:
        return QStringLiteral("Ready");
    case DocumentStatus::Failed:
        return QStringLiteral("Failed");
    case DocumentStatus::Unsupported:
        return QStringLiteral("No readable text");
    }
    return QString();
}

DocumentKind kindForPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        return DocumentKind::Pdf;
    }
    if (suffix == QStringLiteral("pptx")) {
        return DocumentKind::Pptx;
    }
    if (suffix == QStringLiteral("txt") || suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
        return DocumentKind::Text;
    }
    return DocumentKind::Unknown;
}

QString kindLabel(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Pdf:
        return QStringLiteral("PDF");
    case DocumentKind::Pptx:
        return QStringLiteral("Slides");
    case DocumentKind::Text:
        return QStringLiteral("Notes");
    case DocumentKind::Manual:
        return QStringLiteral("Your questions");
    case DocumentKind::Unknown:
        break;
    }
    return QStringLiteral("File");
}

QString compactRanges(const QList<int>& numbers)
{
    QList<int> sorted = numbers;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    QStringList parts;
    for (qsizetype i = 0; i < sorted.size();) {
        qsizetype j = i;
        while (j + 1 < sorted.size() && sorted.at(j + 1) == sorted.at(j) + 1) {
            ++j;
        }
        parts.append(j == i ? QString::number(sorted.at(i))
                            : QStringLiteral("%1-%2").arg(sorted.at(i)).arg(sorted.at(j)));
        i = j + 1;
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace StudyText

namespace DocumentExtractor {
namespace {

int visibleChars(const QString& text)
{
    int n = 0;
    for (const QChar ch : text) {
        if (!ch.isSpace()) {
            ++n;
        }
    }
    return n;
}

ExtractionResult failure(DocumentKind kind, const QString& message)
{
    ExtractionResult r;
    r.ok = false;
    r.kind = kind;
    r.error = message;
    return r;
}

// --- PPTX -------------------------------------------------------------------

QString resolveTarget(const QString& baseDir, const QString& target)
{
    if (target.startsWith(QLatin1Char('/'))) {
        return target.mid(1);
    }
    QStringList parts = baseDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& piece : target.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (piece == QStringLiteral("..")) {
            if (!parts.isEmpty()) {
                parts.removeLast();
            }
        } else if (piece != QStringLiteral(".")) {
            parts.append(piece);
        }
    }
    return parts.join(QLatin1Char('/'));
}

/** Relationship Id -> {Type, resolved Target} for a .rels part. */
QHash<QString, QPair<QString, QString>> readRels(ZipReader& zip, const QString& relsPath, const QString& baseDir)
{
    QHash<QString, QPair<QString, QString>> out;
    const auto data = zip.read(relsPath);
    if (!data) {
        return out;
    }
    QXmlStreamReader xml(*data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("Relationship")) {
            const auto attrs = xml.attributes();
            if (attrs.value(QStringLiteral("TargetMode")) == QStringLiteral("External")) {
                continue;
            }
            out.insert(attrs.value(QStringLiteral("Id")).toString(),
                       {attrs.value(QStringLiteral("Type")).toString(),
                        resolveTarget(baseDir, attrs.value(QStringLiteral("Target")).toString())});
        }
    }
    return out;
}

struct SlideText {
    QString text;
    bool hasPicture = false;
};

/**
 * Collects paragraph text from DrawingML shapes. Placeholder shapes whose type is
 * in skipTypes (slide number, date, footer, slide image) are ignored. When
 * onlyBody is set (notes pages), only body placeholders contribute.
 */
SlideText readShapesText(const QByteArray& xmlData, bool onlyBody)
{
    static const QStringList skipTypes = {QStringLiteral("sldNum"), QStringLiteral("dt"), QStringLiteral("ftr"),
                                          QStringLiteral("sldImg"), QStringLiteral("hdr")};
    SlideText out;
    QXmlStreamReader xml(xmlData);
    QStringList paragraphs;
    QString paragraph;
    int shapeDepth = 0;
    bool shapeSkipped = false;
    bool shapeIsBody = false;
    bool inText = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QStringLiteral("sp") || name == QStringLiteral("graphicFrame")) {
                if (shapeDepth == 0) {
                    shapeSkipped = false;
                    shapeIsBody = false;
                }
                ++shapeDepth;
            } else if (name == QStringLiteral("pic")) {
                out.hasPicture = true;
            } else if (name == QStringLiteral("ph")) {
                const QString type = xml.attributes().value(QStringLiteral("type")).toString();
                if (skipTypes.contains(type)) {
                    shapeSkipped = true;
                }
                if (type.isEmpty() || type == QStringLiteral("body")) {
                    shapeIsBody = true;
                }
            } else if (name == QStringLiteral("p") && xml.namespaceUri().contains(QStringLiteral("drawingml"))) {
                paragraph.clear();
            } else if (name == QStringLiteral("t") && xml.namespaceUri().contains(QStringLiteral("drawingml"))) {
                inText = true;
            } else if (name == QStringLiteral("br")) {
                paragraph.append(QLatin1Char(' '));
            }
        } else if (xml.isCharacters() && inText) {
            paragraph.append(xml.text());
        } else if (xml.isEndElement()) {
            const auto name = xml.name();
            if (name == QStringLiteral("t")) {
                inText = false;
            } else if (name == QStringLiteral("p") && xml.namespaceUri().contains(QStringLiteral("drawingml"))) {
                const bool keep = !shapeSkipped && (!onlyBody || shapeIsBody);
                const QString trimmed = paragraph.simplified();
                if (keep && !trimmed.isEmpty()) {
                    paragraphs.append(trimmed);
                }
                paragraph.clear();
            } else if (name == QStringLiteral("sp") || name == QStringLiteral("graphicFrame")) {
                shapeDepth = std::max(0, shapeDepth - 1);
            }
        }
    }
    out.text = paragraphs.join(QLatin1Char('\n'));
    return out;
}

QString decodeText(const QByteArray& bytes)
{
    QByteArray data = bytes;
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(data);
    if (utf8.hasError()) {
        text = QString::fromLocal8Bit(data);
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

} // namespace

bool pdfSupported()
{
#ifdef CHIIKAWA_HAS_QTPDF
    return true;
#else
    return false;
#endif
}

ExtractionResult extractFile(const QString& path)
{
    const DocumentKind kind = StudyText::kindForPath(path);
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return failure(kind, QStringLiteral("The file could not be found."));
    }
    if (info.size() > kMaxFileBytes) {
        return failure(kind, QStringLiteral("The file is larger than 200 MB."));
    }
    switch (kind) {
    case DocumentKind::Pdf:
        return extractPdf(path);
    case DocumentKind::Pptx: {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return failure(kind, QStringLiteral("The file could not be opened."));
        }
        return extractPptx(file.readAll());
    }
    case DocumentKind::Text: {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return failure(kind, QStringLiteral("The file could not be opened."));
        }
        const QString suffix = info.suffix().toLower();
        return extractText(file.readAll(), suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown"));
    }
    case DocumentKind::Manual:
    case DocumentKind::Unknown:
        break;
    }
    return failure(kind, QStringLiteral("Unsupported file type. Use PDF, PPTX, TXT, or MD."));
}

ExtractionResult extractPdf(const QString& path)
{
#ifdef CHIIKAWA_HAS_QTPDF
    QPdfDocument doc;
    const QPdfDocument::Error err = doc.load(path);
    if (err == QPdfDocument::Error::IncorrectPassword) {
        return failure(DocumentKind::Pdf, QStringLiteral("This PDF is password-protected."));
    }
    if (err != QPdfDocument::Error::None) {
        return failure(DocumentKind::Pdf, QStringLiteral("This PDF could not be read."));
    }
    ExtractionResult r;
    r.kind = DocumentKind::Pdf;
    r.unitCount = doc.pageCount();
    for (int page = 0; page < doc.pageCount(); ++page) {
        const QString text = doc.getAllText(page).text();
        if (visibleChars(text) < kMinPageTextChars) {
            r.unsupportedUnits.append(page + 1);
            continue;
        }
        Segment seg;
        seg.kind = SegmentKind::Page;
        seg.number = page + 1;
        seg.text = text;
        r.segments.append(seg);
    }
    doc.close();
    r.ok = true;
    return r;
#else
    Q_UNUSED(path);
    return failure(DocumentKind::Pdf, QStringLiteral("PDF support was not included in this build."));
#endif
}

ExtractionResult extractPptx(const QByteArray& archive)
{
    ZipReader zip;
    if (!zip.open(archive) || !zip.contains(QStringLiteral("ppt/presentation.xml"))) {
        return failure(DocumentKind::Pptx, QStringLiteral("This is not a readable PowerPoint (.pptx) file."));
    }

    // Slide order comes from presentation.xml's sldIdLst, resolved through its relationships.
    QStringList slidePaths;
    const auto presRels = readRels(zip, QStringLiteral("ppt/_rels/presentation.xml.rels"), QStringLiteral("ppt"));
    if (const auto pres = zip.read(QStringLiteral("ppt/presentation.xml"))) {
        QXmlStreamReader xml(*pres);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("sldId")) {
                for (const auto& attr : xml.attributes()) {
                    if (attr.name() == QStringLiteral("id") && attr.namespaceUri().contains(QStringLiteral("relationships"))) {
                        const auto rel = presRels.value(attr.value().toString());
                        if (!rel.second.isEmpty() && zip.contains(rel.second)) {
                            slidePaths.append(rel.second);
                        }
                    }
                }
            }
        }
    }
    if (slidePaths.isEmpty()) {
        // Fallback: numeric order of ppt/slides/slideN.xml.
        static const QRegularExpression re(QStringLiteral("^ppt/slides/slide(\\d+)\\.xml$"));
        QList<QPair<int, QString>> found;
        for (const QString& name : zip.entryNames()) {
            const auto m = re.match(name);
            if (m.hasMatch()) {
                found.append({m.captured(1).toInt(), name});
            }
        }
        std::sort(found.begin(), found.end());
        for (const auto& f : found) {
            slidePaths.append(f.second);
        }
    }
    if (slidePaths.isEmpty()) {
        return failure(DocumentKind::Pptx, QStringLiteral("This presentation has no slides."));
    }

    ExtractionResult r;
    r.kind = DocumentKind::Pptx;
    r.unitCount = static_cast<int>(slidePaths.size());
    for (qsizetype i = 0; i < slidePaths.size(); ++i) {
        const int number = static_cast<int>(i + 1);
        const QString& slidePath = slidePaths.at(i);
        const auto data = zip.read(slidePath);
        if (!data) {
            r.unsupportedUnits.append(number);
            continue;
        }
        const SlideText slide = readShapesText(*data, false);

        // Speaker notes linked from the slide's relationships.
        QString notesText;
        const QString dir = slidePath.section(QLatin1Char('/'), 0, -2);
        const QString file = slidePath.section(QLatin1Char('/'), -1);
        const auto rels = readRels(zip, dir + QStringLiteral("/_rels/") + file + QStringLiteral(".rels"), dir);
        for (const auto& rel : rels) {
            if (rel.first.endsWith(QStringLiteral("/notesSlide"))) {
                if (const auto notes = zip.read(rel.second)) {
                    notesText = readShapesText(*notes, true).text;
                }
            }
        }

        if (visibleChars(slide.text) == 0 && visibleChars(notesText) == 0) {
            if (slide.hasPicture) {
                r.unsupportedUnits.append(number);
            }
            continue;
        }
        if (!slide.text.trimmed().isEmpty()) {
            r.segments.append({SegmentKind::Slide, number, QString(), slide.text});
        }
        if (!notesText.trimmed().isEmpty()) {
            r.segments.append({SegmentKind::SlideNotes, number, QString(), notesText});
        }
    }
    r.ok = true;
    return r;
}

ExtractionResult extractText(const QByteArray& bytes, bool markdown)
{
    ExtractionResult r;
    r.kind = DocumentKind::Text;
    const QString text = decodeText(bytes);
    const QStringList lines = text.split(QLatin1Char('\n'));

    static const QRegularExpression heading(QStringLiteral("^\\s{0,3}#{1,6}\\s+(.+?)\\s*#*\\s*$"));
    QString currentHeading;
    QStringList current;
    int paragraphsInSection = 0;
    const auto flush = [&]() {
        const QString body = current.join(QLatin1Char('\n')).trimmed();
        if (!body.isEmpty()) {
            Segment seg;
            seg.kind = SegmentKind::Section;
            seg.number = static_cast<int>(r.segments.size()) + 1;
            seg.heading = currentHeading;
            seg.text = currentHeading.isEmpty() ? body : currentHeading + QLatin1Char('\n') + body;
            r.segments.append(seg);
        }
        current.clear();
        paragraphsInSection = 0;
    };

    bool previousBlank = true;
    for (const QString& line : lines) {
        const auto m = markdown ? heading.match(line) : QRegularExpressionMatch();
        if (markdown && m.hasMatch()) {
            flush();
            currentHeading = m.captured(1).trimmed();
            previousBlank = true;
            continue;
        }
        const bool blank = line.trimmed().isEmpty();
        if (!blank && previousBlank) {
            ++paragraphsInSection;
            // Plain notes without headings are grouped into sections of ~8 paragraphs.
            if (currentHeading.isEmpty() && paragraphsInSection > 8) {
                flush();
                paragraphsInSection = 1;
            }
        }
        current.append(line);
        previousBlank = blank;
    }
    flush();
    r.unitCount = static_cast<int>(r.segments.size());
    if (r.segments.isEmpty()) {
        return failure(DocumentKind::Text, QStringLiteral("The file has no text."));
    }
    r.ok = true;
    return r;
}

} // namespace DocumentExtractor
