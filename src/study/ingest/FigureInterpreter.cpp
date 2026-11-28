#include "study/ingest/FigureInterpreter.h"

#include "study/generation/QuestionValidator.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace FigureInterpreter {
namespace {

const char* const kSystemPrompt =
    "You describe images from a student's course material for a quiz writer who cannot see them. "
    "Describe only what is actually visible in the image; never guess or add outside knowledge. "
    "Copy labels, numbers and words exactly as written. If the image is blurry, unreadable, or only "
    "decorative (a logo, a photo of a person, clip art, a drawing with no labels), set \"kind\" to "
    "\"decorative\" and \"legible\" to false. Reply with JSON only.";

QStringList strings(const QJsonValue& value, int max)
{
    QStringList out;
    for (const QJsonValue& v : value.toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty() && out.size() < max) {
            out.append(s.left(200));
        }
    }
    return out;
}

} // namespace

QString Reading::toText() const
{
    QStringList lines;
    lines.append(title.isEmpty() ? QStringLiteral("A %1.").arg(kind) : QStringLiteral("A %1: %2.").arg(kind, title));
    if (!summary.isEmpty()) {
        lines.append(summary);
    }
    if (!elements.isEmpty()) {
        lines.append(QStringLiteral("Parts: %1").arg(elements.join(QStringLiteral("; "))));
    }
    if (!relationships.isEmpty()) {
        lines.append(QStringLiteral("Connections: %1").arg(relationships.join(QStringLiteral("; "))));
    }
    if (!table.isEmpty()) {
        lines.append(QStringLiteral("Table:\n%1").arg(StudyText::tableToText(table)));
    }
    if (!values.isEmpty()) {
        lines.append(QStringLiteral("Values: %1").arg(values.join(QStringLiteral("; "))));
    }
    return lines.join(QLatin1Char('\n'));
}

QByteArray schema()
{
    // Ordered: the model decides what it is looking at and whether it can read it before describing it.
    return R"({"type":"object","properties":{)"
           R"("kind":{"type":"string","enum":["diagram","chart","table","equation","screenshot","photo","text","decorative"]},)"
           R"("legible":{"type":"boolean"},)"
           R"("title":{"type":"string"},)"
           R"("readable_text":{"type":"string"},)"
           R"("summary":{"type":"string"},)"
           R"("elements":{"type":"array","maxItems":12,"items":{"type":"string"}},)"
           R"("relationships":{"type":"array","maxItems":12,"items":{"type":"string"}},)"
           R"("table_rows":{"type":"array","maxItems":12,"items":{"type":"array","maxItems":8,"items":{"type":"string"}}},)"
           R"("values":{"type":"array","maxItems":12,"items":{"type":"string"}})"
           R"(},"required":["kind","legible","title","readable_text","summary","elements","relationships","table_rows","values"]})";
}

LlmRequest buildRequest(const QByteArray& image, const QString& context, const QString& model)
{
    LlmRequest r;
    r.system = QString::fromLatin1(kSystemPrompt);
    r.prompt = QStringLiteral(
                   "Describe this image so questions can be written about it.\n"
                   "- \"readable_text\": every word or number you can read in it, exactly as written.\n"
                   "- \"summary\": 1-3 sentences on what it shows.\n"
                   "- \"elements\": the labelled parts and what each is.\n"
                   "- \"relationships\": how parts connect, as \"A -> B: meaning\" (arrows, flows, hierarchy).\n"
                   "- \"table_rows\": for a table, its rows (the first row is the header); otherwise empty.\n"
                   "- \"values\": for a chart, its axes and the values shown; otherwise empty.\n"
                   "%1")
                   .arg(context.trimmed().isEmpty()
                            ? QString()
                            : QStringLiteral("\nFor naming only, the text around the image says:\n\"\"\"\n%1\n\"\"\"").arg(context.left(800)));
    r.schema = schema();
    r.images = {image};
    r.model = model;
    r.temperature = 0.1;
    r.maxTokens = 900;
    r.timeoutMs = 240000;   // vision on a laptop CPU can take a while
    return r;
}

QByteArray prepareImage(const QString& path)
{
    QImage image(path);
    if (image.isNull()) {
        return {};
    }
    if (image.width() > kMaxImageEdge || image.height() > kMaxImageEdge) {
        image = image.scaled(kMaxImageEdge, kMaxImageEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Transparent figures are put on white so dark lines stay visible.
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    const QImage src = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(flat.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const int a = qAlpha(in[x]);
            out[x] = qRgb((qRed(in[x]) * a + 255 * (255 - a)) / 255, (qGreen(in[x]) * a + 255 * (255 - a)) / 255,
                          (qBlue(in[x]) * a + 255 * (255 - a)) / 255);
        }
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!flat.save(&buffer, "JPG", 88)) {
        return {};
    }
    return bytes;
}

std::optional<Reading> parse(const QString& json, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<Reading> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    const qsizetype open = json.indexOf(QLatin1Char('{'));
    const qsizetype close = json.lastIndexOf(QLatin1Char('}'));
    if (open < 0 || close <= open) {
        return fail(QStringLiteral("reply is not JSON"));
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.mid(open, close - open + 1).toUtf8());
    if (!doc.isObject()) {
        return fail(QStringLiteral("reply is not a JSON object"));
    }
    const QJsonObject obj = doc.object();
    Reading r;
    r.kind = obj.value(QStringLiteral("kind")).toString().trimmed().toLower();
    r.legible = obj.value(QStringLiteral("legible")).toBool(false);
    r.title = obj.value(QStringLiteral("title")).toString().trimmed().left(160);
    r.summary = obj.value(QStringLiteral("summary")).toString().trimmed().left(800);
    r.readableText = obj.value(QStringLiteral("readable_text")).toString().trimmed().left(2000);
    r.elements = strings(obj.value(QStringLiteral("elements")), 12);
    r.relationships = strings(obj.value(QStringLiteral("relationships")), 12);
    r.values = strings(obj.value(QStringLiteral("values")), 12);
    QVector<QStringList> rows;
    for (const QJsonValue& row : obj.value(QStringLiteral("table_rows")).toArray()) {
        const QStringList cells = strings(row, 8);
        if (!cells.isEmpty()) {
            rows.append(cells);
        }
    }
    if (rows.size() >= 2) {
        r.table.headers = rows.takeFirst();
        r.table.rows = rows;
    }
    if (r.kind.isEmpty()) {
        return fail(QStringLiteral("missing kind"));
    }
    // Decorative pictures and near-empty descriptions are never asked about. A figure with no
    // readable words, numbers or cells is treated as decorative too: small models describe clip
    // art as a "diagram", and nothing in such a picture lets a student check an answer.
    if (r.kind == QLatin1String("decorative") || r.kind == QLatin1String("photo")
        || (r.summary.size() < 20 && r.elements.isEmpty() && r.table.isEmpty() && r.values.isEmpty())
        || (r.readableText.isEmpty() && r.table.isEmpty() && r.values.isEmpty())) {
        r.legible = false;
    }
    return r;
}

bool agreesWithOcr(const Reading& reading, const QString& ocrText)
{
    const QString ocr = QuestionValidator::normalizeForMatch(ocrText);
    if (ocr.size() < 20 || reading.elements.size() < 3) {
        return true;   // nothing to compare against
    }
    int checked = 0;
    int found = 0;
    for (const QString& element : reading.elements) {
        // The label is the part before any explanation ("Cache: stores recent lines" -> "Cache").
        const QString label = QuestionValidator::normalizeForMatch(element.section(QLatin1Char(':'), 0, 0).section(QStringLiteral(" - "), 0, 0));
        if (label.isEmpty() || label.size() > 40) {
            continue;
        }
        ++checked;
        if ((QLatin1Char(' ') + ocr + QLatin1Char(' ')).contains(QLatin1Char(' ') + label + QLatin1Char(' '))) {
            ++found;
        }
    }
    return checked < 3 || found * 2 >= checked;
}

} // namespace FigureInterpreter
