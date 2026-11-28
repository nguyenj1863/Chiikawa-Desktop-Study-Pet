#include "study/ingest/DocumentExtractor.h"

#include "study/ingest/ZipReader.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QtMath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <optional>
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
    static const QStringList images = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                       QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                                       QStringLiteral("tif"), QStringLiteral("tiff")};
    if (images.contains(suffix)) {
        return DocumentKind::Image;
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
    case DocumentKind::Image:
        return QStringLiteral("Image");
    case DocumentKind::Unknown:
        break;
    }
    return QStringLiteral("File");
}

QString normalizeSymbolFonts(const QString& text)
{
    const auto isPrivate = [](QChar c) { return c.unicode() >= 0xF020 && c.unicode() <= 0xF0FF; };
    if (std::none_of(text.cbegin(), text.cend(), isPrivate)) {
        return text;
    }
    // Adobe Symbol encoding for the bytes slides actually use in maths.
    static const QHash<int, char16_t> symbol = {
        {0x22, u'∀'}, {0x24, u'∃'}, {0x27, u'∋'}, {0x2A, u'∗'}, {0x2D, u'−'}, {0x40, u'≅'},
        {0x41, u'Α'}, {0x42, u'Β'}, {0x43, u'Χ'}, {0x44, u'Δ'}, {0x45, u'Ε'}, {0x46, u'Φ'}, {0x47, u'Γ'},
        {0x48, u'Η'}, {0x49, u'Ι'}, {0x4A, u'ϑ'}, {0x4B, u'Κ'}, {0x4C, u'Λ'}, {0x4D, u'Μ'}, {0x4E, u'Ν'},
        {0x4F, u'Ο'}, {0x50, u'Π'}, {0x51, u'Θ'}, {0x52, u'Ρ'}, {0x53, u'Σ'}, {0x54, u'Τ'}, {0x55, u'Υ'},
        {0x56, u'ς'}, {0x57, u'Ω'}, {0x58, u'Ξ'}, {0x59, u'Ψ'}, {0x5A, u'Ζ'}, {0x5E, u'⊥'},
        {0x61, u'α'}, {0x62, u'β'}, {0x63, u'χ'}, {0x64, u'δ'}, {0x65, u'ε'}, {0x66, u'φ'}, {0x67, u'γ'},
        {0x68, u'η'}, {0x69, u'ι'}, {0x6A, u'ϕ'}, {0x6B, u'κ'}, {0x6C, u'λ'}, {0x6D, u'μ'}, {0x6E, u'ν'},
        {0x6F, u'ο'}, {0x70, u'π'}, {0x71, u'θ'}, {0x72, u'ρ'}, {0x73, u'σ'}, {0x74, u'τ'}, {0x75, u'υ'},
        {0x76, u'ϖ'}, {0x77, u'ω'}, {0x78, u'ξ'}, {0x79, u'ψ'}, {0x7A, u'ζ'}, {0x7E, u'∼'},
        {0xA2, u'′'}, {0xA3, u'≤'}, {0xA5, u'∞'}, {0xAB, u'↔'}, {0xAC, u'←'}, {0xAD, u'↑'}, {0xAE, u'→'},
        {0xAF, u'↓'}, {0xB0, u'°'}, {0xB1, u'±'}, {0xB2, u'″'}, {0xB3, u'≥'}, {0xB4, u'×'}, {0xB5, u'∝'},
        {0xB6, u'∂'}, {0xB7, u'•'}, {0xB8, u'÷'}, {0xB9, u'≠'}, {0xBA, u'≡'}, {0xBB, u'≈'}, {0xBC, u'…'},
        {0xC4, u'⊗'}, {0xC5, u'⊕'}, {0xC6, u'∅'}, {0xC7, u'∩'}, {0xC8, u'∪'}, {0xC9, u'⊃'}, {0xCA, u'⊇'},
        {0xCB, u'⊄'}, {0xCC, u'⊂'}, {0xCD, u'⊆'}, {0xCE, u'∈'}, {0xCF, u'∉'}, {0xD0, u'∠'}, {0xD1, u'∇'},
        {0xD5, u'∏'}, {0xD6, u'√'}, {0xD7, u'⋅'}, {0xD8, u'¬'}, {0xD9, u'∧'}, {0xDA, u'∨'}, {0xDB, u'⇔'},
        {0xDC, u'⇐'}, {0xDD, u'⇑'}, {0xDE, u'⇒'}, {0xDF, u'⇓'}, {0xE5, u'∑'}, {0xF2, u'∫'},
    };
    // Wingdings bullet glyphs; as a word of their own they are list bullets, never letters.
    static const QSet<int> bullets = {0x6C, 0x6E, 0x6F, 0x71, 0x75, 0x76, 0x77, 0x9F, 0xA7, 0xA8, 0xD8, 0xFC};
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!isPrivate(c)) {
            out.append(c);
            continue;
        }
        const int code = c.unicode() - 0xF000;
        const bool startsWord = i == 0 || text.at(i - 1).isSpace();
        const bool endsWord = i + 1 >= text.size() || text.at(i + 1).isSpace();
        if (bullets.contains(code) && startsWord && endsWord) {
            out.append(QChar(u'•'));
        } else if (const auto it = symbol.constFind(code); it != symbol.cend()) {
            out.append(QChar(it.value()));
        }
    }
    return out;
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

QString tableToText(const TableData& table)
{
    QStringList lines;
    if (!table.headers.isEmpty()) {
        lines.append(table.headers.join(QStringLiteral(" | ")));
    }
    for (const QStringList& row : table.rows) {
        lines.append(row.join(QStringLiteral(" | ")));
    }
    return lines.join(QLatin1Char('\n'));
}

QString tableToJson(const TableData& table)
{
    if (table.isEmpty()) {
        return QString();
    }
    QJsonArray rows;
    for (const QStringList& row : table.rows) {
        rows.append(QJsonArray::fromStringList(row));
    }
    const QJsonObject obj{{QStringLiteral("headers"), QJsonArray::fromStringList(table.headers)},
                          {QStringLiteral("rows"), rows}};
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

TableData tableFromJson(const QString& json)
{
    TableData t;
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    for (const QJsonValue& h : obj.value(QStringLiteral("headers")).toArray()) {
        t.headers.append(h.toString());
    }
    for (const QJsonValue& r : obj.value(QStringLiteral("rows")).toArray()) {
        QStringList row;
        for (const QJsonValue& cell : r.toArray()) {
            row.append(cell.toString());
        }
        t.rows.append(row);
    }
    return t;
}

bool sameUnit(SegmentKind assetKind, SegmentKind chunkKind)
{
    const auto unit = [](SegmentKind k) { return k == SegmentKind::SlideNotes ? SegmentKind::Slide : k; };
    return unit(assetKind) == unit(chunkKind);
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

struct SlidePicture {
    QString relationshipId;
    QString altText;
};

struct SlideText {
    QString text;
    bool hasPicture = false;
    QList<SlidePicture> pictures;
    QVector<TableData> tables;
};

/** Images smaller than this on either side are icons or bullets, not figures. */
constexpr int kMinFigurePx = 64;
/** Figures are re-encoded no larger than this, which is plenty for the question UI and vision models. */
constexpr int kMaxFigurePx = 1600;

/** Default names PowerPoint gives pictures; they say nothing about the content. */
bool isGenericPictureName(const QString& text)
{
    static const QRegularExpression generic(QStringLiteral(R"(^(?:picture|image|graphic|grafik|bild|content placeholder|chart)\s*\d*$)"),
                                            QRegularExpression::CaseInsensitiveOption);
    return text.trimmed().isEmpty() || generic.match(text.trimmed()).hasMatch();
}

/** Decodes and normalises a figure: PNG or JPEG kept, other formats and huge images re-encoded. */
bool normaliseImage(const QByteArray& data, QByteArray* out, QString* format)
{
    QImage image = QImage::fromData(data);
    if (image.isNull() || image.width() < kMinFigurePx || image.height() < kMinFigurePx) {
        return false;
    }
    const bool png = data.startsWith("\x89PNG");
    const bool jpeg = data.startsWith("\xFF\xD8");
    if ((png || jpeg) && image.width() <= kMaxFigurePx && image.height() <= kMaxFigurePx) {
        *out = data;
        *format = png ? QStringLiteral("png") : QStringLiteral("jpg");
        return true;
    }
    if (image.width() > kMaxFigurePx || image.height() > kMaxFigurePx) {
        image = image.scaled(kMaxFigurePx, kMaxFigurePx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QBuffer buffer(out);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return false;
    }
    *format = QStringLiteral("png");
    return true;
}

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
    // Pictures (p:pic with an a:blip) and tables (a:tbl / a:tr / a:tc)
    bool inPicture = false;
    SlidePicture picture;
    bool inTable = false;
    TableData table;
    QStringList row;
    bool inCell = false;
    QString cell;

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
                inPicture = true;
                picture = SlidePicture();
            } else if (inPicture && name == QStringLiteral("cNvPr")) {
                const auto attrs = xml.attributes();
                const QString descr = attrs.value(QStringLiteral("descr")).toString().trimmed();
                const QString title = attrs.value(QStringLiteral("title")).toString().trimmed();
                picture.altText = !descr.isEmpty() ? descr : title;
            } else if (inPicture && name == QStringLiteral("blip")) {
                for (const auto& attr : xml.attributes()) {
                    if (attr.name() == QStringLiteral("embed") && attr.namespaceUri().contains(QStringLiteral("relationships"))) {
                        picture.relationshipId = attr.value().toString();
                    }
                }
            } else if (name == QStringLiteral("tbl") && xml.namespaceUri().contains(QStringLiteral("drawingml"))) {
                inTable = true;
                table = TableData();
            } else if (inTable && name == QStringLiteral("tr")) {
                row.clear();
            } else if (inTable && name == QStringLiteral("tc")) {
                inCell = true;
                cell.clear();
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
            if (inCell) {
                cell.append(xml.text());
            }
        } else if (xml.isEndElement()) {
            const auto name = xml.name();
            if (name == QStringLiteral("t")) {
                inText = false;
            } else if (name == QStringLiteral("pic")) {
                if (inPicture && !picture.relationshipId.isEmpty()) {
                    out.pictures.append(picture);
                }
                inPicture = false;
            } else if (inTable && name == QStringLiteral("tc")) {
                row.append(cell.simplified());
                inCell = false;
            } else if (inTable && name == QStringLiteral("tr")) {
                table.rows.append(row);
            } else if (name == QStringLiteral("tbl") && xml.namespaceUri().contains(QStringLiteral("drawingml"))) {
                // The first row is treated as the header row, as in almost every slide table.
                if (!table.rows.isEmpty()) {
                    table.headers = table.rows.takeFirst();
                }
                if (!table.isEmpty()) {
                    out.tables.append(table);
                }
                inTable = false;
            } else if (inCell && name == QStringLiteral("p") && xml.namespaceUri().contains(QStringLiteral("drawingml"))) {
                cell.append(QLatin1Char(' '));
                const bool keep = !shapeSkipped && (!onlyBody || shapeIsBody);
                const QString trimmed = paragraph.simplified();
                if (keep && !trimmed.isEmpty()) {
                    paragraphs.append(trimmed);
                }
                paragraph.clear();
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
    out.text = StudyText::normalizeSymbolFonts(paragraphs.join(QLatin1Char('\n')));
    return out;
}

/**
 * A page whose text is mostly short labels: node names, edge weights, table cells, boxes of a
 * diagram. Its words are unreadable without the drawing, so the page image is kept as a figure.
 */
bool looksLikeDiagram(const QString& pageText)
{
    QStringList lines;
    for (const QString& line : pageText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts)) {
        if (!line.trimmed().isEmpty()) {
            lines.append(line.trimmed());
        }
    }
    if (lines.size() < 6) {
        return false;
    }
    int shortLines = 0;
    int words = 0;
    for (const QString& line : std::as_const(lines)) {
        const qsizetype n = line.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        words += static_cast<int>(n);
        shortLines += n <= 3 ? 1 : 0;
    }
    return words <= 120 && shortLines * 10 >= static_cast<int>(lines.size()) * 6;
}

#ifdef CHIIKAWA_HAS_QTPDF
/** Page image shown beside questions; long enough edge to read slide-sized text, small on disk. */
constexpr int kPageImageLongEdge = 1400;

/** Renders a PDF page onto white (PDF pages are transparent where nothing is drawn). */
QImage renderPdfPage(QPdfDocument& doc, int page, int longEdge)
{
    const QSizeF points = doc.pagePointSize(page);
    if (points.isEmpty()) {
        return {};
    }
    const double scale = longEdge / std::max(points.width(), points.height());
    const QSize size(std::max(1, qRound(points.width() * scale)), std::max(1, qRound(points.height() * scale)));
    const QImage rendered = doc.render(page, size);
    if (rendered.isNull()) {
        return {};
    }
    QImage page_(rendered.size(), QImage::Format_RGB32);
    page_.fill(Qt::white);
    QPainter painter(&page_);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return page_;
}
#endif

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
    case DocumentKind::Image:
        return extractImage(path);
    case DocumentKind::Manual:
    case DocumentKind::Unknown:
        break;
    }
    return failure(kind, QStringLiteral("Unsupported file type. Use PDF, PPTX, TXT, MD, or an image."));
}

ExtractionResult extractImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(DocumentKind::Image, QStringLiteral("The image could not be opened."));
    }
    const QByteArray data = file.readAll();
    QByteArray bytes;
    QString format;
    if (!normaliseImage(data, &bytes, &format)) {
        return failure(DocumentKind::Image, QStringLiteral("This image could not be read, or it is too small."));
    }
    // One "page" with no text layer: the image is kept to show with questions, and OCR reads it.
    ExtractionResult r;
    r.kind = DocumentKind::Image;
    r.unitCount = 1;
    r.unsupportedUnits = {1};
    ExtractedAsset asset;
    asset.unitKind = SegmentKind::Page;
    asset.unitNumber = 1;
    asset.kind = SupportKind::Image;
    asset.imageData = bytes;
    asset.imageFormat = format;   // captioned with the document title when stored
    r.assets.append(asset);
    r.ok = true;
    return r;
}

QRectF figureRegion(const QSizeF& pageSize, const QRectF& caption, const QList<QRectF>& textLines, bool tableCaption)
{
    if (pageSize.isEmpty() || caption.isEmpty()) {
        return {};
    }
    // Body text lines bound the figure; short runs (labels inside a diagram, cell text) do not.
    double left = pageSize.width();
    double right = 0.0;
    QList<QRectF> body;
    for (const QRectF& line : textLines) {
        if (line.width() > pageSize.width() * 0.4 && !line.intersects(caption)) {
            body.append(line);
            left = std::min(left, line.left());
            right = std::max(right, line.right());
        }
    }
    if (right <= left) {   // no body text: the figure can use the page's width
        left = 0.0;
        right = pageSize.width();
    }
    left = std::min(left, caption.left());
    right = std::max(right, caption.right());

    const auto above = [&]() {
        double top = 0.0;
        for (const QRectF& line : body) {
            if (line.bottom() <= caption.top() + 1.0) {
                top = std::max(top, line.bottom());
            }
        }
        return QRectF(QPointF(left, top), QPointF(right, caption.bottom()));
    };
    const auto below = [&]() {
        double bottom = pageSize.height();
        for (const QRectF& line : body) {
            if (line.top() >= caption.bottom() - 1.0) {
                bottom = std::min(bottom, line.top());
            }
        }
        return QRectF(QPointF(left, caption.top()), QPointF(right, bottom));
    };
    const double minHeight = pageSize.height() * 0.15;
    // The band must hold the figure itself, not only the caption.
    const auto bigEnough = [&](const QRectF& r) { return r.height() - caption.height() >= minHeight; };
    const QRectF first = tableCaption ? below() : above();
    if (bigEnough(first)) {
        return first;
    }
    const QRectF second = tableCaption ? above() : below();
    return bigEnough(second) ? second : QRectF();
}

QImage renderPdfPageImage(const QString& path, int pageNumber, int longEdge)
{
#ifdef CHIIKAWA_HAS_QTPDF
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None || pageNumber < 1 || pageNumber > doc.pageCount()) {
        return {};
    }
    const QImage image = renderPdfPage(doc, pageNumber - 1, longEdge);
    doc.close();
    return image;
#else
    Q_UNUSED(path);
    Q_UNUSED(pageNumber);
    Q_UNUSED(longEdge);
    return {};
#endif
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
    // A caption line ("Figure 3: ...", "Table 2.") means the page carries a figure or table the
    // text alone does not show, so the page image is kept to show beside questions about it.
    static const QRegularExpression captionLine(
        QStringLiteral(R"(^\s*((?:figure|fig\.|table|chart|graph|diagram|exhibit)\s*\d+[\w.-]*\s*[:.\-–]?.*)$)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const auto pageAsset = [&](int page, const QString& caption, const QRectF& region) {
        QImage image = renderPdfPage(doc, page, kPageImageLongEdge);
        const QSizeF points = doc.pagePointSize(page);
        if (!image.isNull() && region.isValid() && !points.isEmpty()) {
            // Keep only the figure (and its caption): the original drawing, without the page around it.
            constexpr double kPadPoints = 6.0;
            const double scale = image.width() / points.width();
            const QRectF padded = region.adjusted(-kPadPoints, -kPadPoints, kPadPoints, kPadPoints)
                                      .intersected(QRectF(QPointF(0, 0), points));
            const QRect pixels(qFloor(padded.left() * scale), qFloor(padded.top() * scale),
                               qCeil(padded.width() * scale), qCeil(padded.height() * scale));
            image = image.copy(pixels.intersected(image.rect()));
        }
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPG", 85)) {
            return;
        }
        ExtractedAsset asset;
        asset.unitKind = SegmentKind::Page;
        asset.unitNumber = page + 1;
        asset.kind = SupportKind::Image;
        asset.imageData = bytes;
        asset.imageFormat = QStringLiteral("jpg");
        asset.caption = caption;
        r.assets.append(asset);
    };
    for (int page = 0; page < doc.pageCount(); ++page) {
        const QString text = doc.getAllText(page).text();
        if (visibleChars(text) < kMinPageTextChars) {
            r.unsupportedUnits.append(page + 1);
            pageAsset(page, QStringLiteral("Page %1 (scanned)").arg(page + 1), QRectF());
            continue;
        }
        Segment seg;
        seg.kind = SegmentKind::Page;
        seg.number = page + 1;
        seg.text = StudyText::normalizeSymbolFonts(text);
        r.segments.append(seg);
        const QRegularExpressionMatch caption = captionLine.match(text);
        if (caption.hasMatch()) {
            // Crop to the figure when the caption and the text around it can be located.
            const QPdfSelection captionSel = doc.getSelectionAtIndex(page, static_cast<int>(caption.capturedStart(1)),
                                                                     static_cast<int>(caption.capturedLength(1)));
            // Character indexes can drift from the extracted text; only trust a box that holds the caption.
            const QString found = captionSel.text().simplified();
            const QString wanted = caption.captured(1).simplified();
            const QRectF captionBox = !found.isEmpty() && found.left(12) == wanted.left(12) ? captionSel.boundingRectangle()
                                                                                             : QRectF();
            QList<QRectF> lines;
            for (const QPolygonF& poly : doc.getAllText(page).bounds()) {
                lines.append(poly.boundingRect());
            }
            const bool table = caption.captured(1).trimmed().startsWith(QStringLiteral("table"), Qt::CaseInsensitive);
            pageAsset(page, caption.captured(1).simplified().left(160),
                      figureRegion(doc.pagePointSize(page), captionBox, lines, table));
        } else if (looksLikeDiagram(text)) {
            // No caption, but the page is mostly labels (a graph, a diagram, a table): the words
            // alone lose what connects them, so the page itself is kept to show with questions.
            pageAsset(page, QStringLiteral("Page %1").arg(page + 1), QRectF());
        }
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
    struct PendingPicture {
        int slide = 0;
        QString media;
        QString alt;
    };
    QList<PendingPicture> pictures;
    QHash<QString, int> mediaUse;   // how many slides show each picture (logos repeat on every slide)
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

        // Figures and tables on the slide itself (layouts and masters are not read, so no backgrounds).
        QSet<QString> seenOnSlide;
        for (const SlidePicture& pic : slide.pictures) {
            const auto rel = rels.value(pic.relationshipId);
            if (!rel.first.endsWith(QStringLiteral("/image")) || rel.second.isEmpty() || seenOnSlide.contains(rel.second)) {
                continue;
            }
            seenOnSlide.insert(rel.second);
            ++mediaUse[rel.second];
            pictures.append({number, rel.second, pic.altText});
        }
        for (const TableData& table : slide.tables) {
            ExtractedAsset asset;
            asset.unitKind = SegmentKind::Slide;
            asset.unitNumber = number;
            asset.kind = SupportKind::Table;
            asset.table = table;
            r.assets.append(asset);
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

    // Keep real figures: skip pictures repeated across many slides (logos, decorations) and tiny ones.
    const int repeatLimit = std::max(3, static_cast<int>(slidePaths.size()) / 3);
    QHash<QString, std::optional<QPair<QByteArray, QString>>> decoded;
    for (const PendingPicture& p : std::as_const(pictures)) {
        if (mediaUse.value(p.media) >= repeatLimit) {
            continue;
        }
        if (!decoded.contains(p.media)) {
            std::optional<QPair<QByteArray, QString>> value;
            if (const auto data = zip.read(p.media)) {
                QByteArray bytes;
                QString format;
                if (normaliseImage(*data, &bytes, &format)) {
                    value = qMakePair(bytes, format);
                }
            }
            decoded.insert(p.media, value);
        }
        const auto& image = decoded.value(p.media);
        if (!image) {
            continue;
        }
        ExtractedAsset asset;
        asset.unitKind = SegmentKind::Slide;
        asset.unitNumber = p.slide;
        asset.kind = SupportKind::Image;
        asset.imageData = image->first;
        asset.imageFormat = image->second;
        asset.caption = isGenericPictureName(p.alt) ? QString() : p.alt;
        r.assets.append(asset);
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

    // Markdown tables become table assets of the section they appear in (their text stays in it too).
    static const QRegularExpression tableRow(QStringLiteral(R"(^\s*\|.*\|\s*$)"));
    static const QRegularExpression separatorRow(QStringLiteral(R"(^\s*\|?[\s:|-]+\|?\s*$)"));
    QStringList tableLines;
    const auto flushTable = [&]() {
        if (tableLines.size() < 2) {
            tableLines.clear();
            return;
        }
        TableData table;
        for (const QString& raw : std::as_const(tableLines)) {
            if (separatorRow.match(raw).hasMatch()) {
                continue;
            }
            QString body = raw.trimmed();
            body = body.mid(1, body.size() - 2);
            QStringList cells;
            for (const QString& cell : body.split(QLatin1Char('|'))) {
                cells.append(cell.trimmed());
            }
            if (table.headers.isEmpty() && table.rows.isEmpty()) {
                table.headers = cells;
            } else {
                table.rows.append(cells);
            }
        }
        if (!table.rows.isEmpty()) {
            ExtractedAsset asset;
            asset.unitKind = SegmentKind::Section;
            asset.unitNumber = static_cast<int>(r.segments.size()) + 1;
            asset.kind = SupportKind::Table;
            asset.table = table;
            r.assets.append(asset);
        }
        tableLines.clear();
    };

    bool previousBlank = true;
    for (const QString& line : lines) {
        if (markdown && tableRow.match(line).hasMatch()) {
            tableLines.append(line);
        } else if (!tableLines.isEmpty()) {
            flushTable();
        }
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
    flushTable();
    flush();
    r.unitCount = static_cast<int>(r.segments.size());
    if (r.segments.isEmpty()) {
        return failure(DocumentKind::Text, QStringLiteral("The file has no text."));
    }
    r.ok = true;
    return r;
}

} // namespace DocumentExtractor
