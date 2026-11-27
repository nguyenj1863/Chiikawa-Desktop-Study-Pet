#pragma once

// Test-only builders for Study Library fixtures: a minimal ZIP writer (stored and
// deflate entries), a PPTX package builder, and raw DEFLATE data via qCompress.
// Fixtures are generated at test time so no binary files are checked in.

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QtEndian>
#include <array>

namespace StudyTestUtils {

inline quint32 crc32(const QByteArray& data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : data) {
        crc = table[(crc ^ static_cast<quint8>(ch)) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/** Raw RFC 1951 DEFLATE data: qCompress output minus its size prefix, zlib header, and Adler-32. */
inline QByteArray rawDeflate(const QByteArray& data, int level = 6)
{
    const QByteArray z = qCompress(data, level);
    return z.mid(6, z.size() - 6 - 4);
}

class ZipWriter {
public:
    void add(const QString& name, const QByteArray& data, bool deflate = true)
    {
        Entry e;
        e.name = name.toUtf8();
        e.crc = crc32(data);
        e.size = static_cast<quint32>(data.size());
        e.method = deflate ? 8 : 0;
        e.payload = deflate ? rawDeflate(data) : data;
        m_entries.append(e);
    }

    /** Corrupts the stored CRC of an entry so readers must reject it. */
    void breakCrc(const QString& name)
    {
        for (Entry& e : m_entries) {
            if (e.name == name.toUtf8()) {
                e.crc ^= 0xDEADBEEFu;
            }
        }
    }

    [[nodiscard]] QByteArray finish() const
    {
        QByteArray out;
        QByteArray central;
        for (const Entry& e : m_entries) {
            const quint32 offset = static_cast<quint32>(out.size());
            out += le32(0x04034b50u);
            out += le16(20) + le16(0) + le16(e.method) + le16(0) + le16(0);
            out += le32(e.crc) + le32(static_cast<quint32>(e.payload.size())) + le32(e.size);
            out += le16(static_cast<quint16>(e.name.size())) + le16(0);
            out += e.name;
            out += e.payload;

            central += le32(0x02014b50u);
            central += le16(20) + le16(20) + le16(0) + le16(e.method) + le16(0) + le16(0);
            central += le32(e.crc) + le32(static_cast<quint32>(e.payload.size())) + le32(e.size);
            central += le16(static_cast<quint16>(e.name.size())) + le16(0) + le16(0) + le16(0) + le16(0);
            central += le32(0) + le32(offset);
            central += e.name;
        }
        const quint32 cdOffset = static_cast<quint32>(out.size());
        out += central;
        out += le32(0x06054b50u) + le16(0) + le16(0);
        out += le16(static_cast<quint16>(m_entries.size())) + le16(static_cast<quint16>(m_entries.size()));
        out += le32(static_cast<quint32>(central.size())) + le32(cdOffset) + le16(0);
        return out;
    }

private:
    struct Entry {
        QByteArray name;
        QByteArray payload;
        quint32 crc = 0;
        quint32 size = 0;
        quint16 method = 0;
    };

    static QByteArray le16(quint16 v)
    {
        QByteArray b(2, '\0');
        qToLittleEndian(v, b.data());
        return b;
    }
    static QByteArray le32(quint32 v)
    {
        QByteArray b(4, '\0');
        qToLittleEndian(v, b.data());
        return b;
    }

    QList<Entry> m_entries;
};

struct TestSlide {
    QString title;
    QStringList bullets;
    QString notes;
    bool picture = false;
    QString slideNumberText = QStringLiteral("99");   // placeholder text that must be ignored
};

inline QByteArray slideXml(const TestSlide& s)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
        " xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"><p:cSld><p:spTree>");
    if (!s.title.isEmpty()) {
        xml += QStringLiteral("<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Title\"/><p:cNvSpPr/><p:nvPr><p:ph type=\"title\"/></p:nvPr></p:nvSpPr>"
                              "<p:txBody><a:bodyPr/><a:p><a:r><a:t>%1</a:t></a:r></a:p></p:txBody></p:sp>")
                   .arg(s.title.toHtmlEscaped());
    }
    if (!s.bullets.isEmpty()) {
        xml += QStringLiteral("<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Content\"/><p:cNvSpPr/><p:nvPr><p:ph idx=\"1\"/></p:nvPr></p:nvSpPr><p:txBody><a:bodyPr/>");
        for (const QString& b : s.bullets) {
            // Split each bullet into two runs to exercise run concatenation.
            const qsizetype half = b.size() / 2;
            xml += QStringLiteral("<a:p><a:r><a:t>%1</a:t></a:r><a:r><a:t>%2</a:t></a:r></a:p>")
                       .arg(b.left(half).toHtmlEscaped(), b.mid(half).toHtmlEscaped());
        }
        xml += QStringLiteral("</p:txBody></p:sp>");
    }
    xml += QStringLiteral("<p:sp><p:nvSpPr><p:cNvPr id=\"4\" name=\"Slide Number\"/><p:cNvSpPr/><p:nvPr><p:ph type=\"sldNum\"/></p:nvPr></p:nvSpPr>"
                          "<p:txBody><a:bodyPr/><a:p><a:r><a:t>%1</a:t></a:r></a:p></p:txBody></p:sp>")
               .arg(s.slideNumberText);
    if (s.picture) {
        xml += QStringLiteral("<p:pic><p:nvPicPr><p:cNvPr id=\"5\" name=\"Picture\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr>"
                              "<p:blipFill><a:blip r:embed=\"rId9\"/></p:blipFill><p:spPr/></p:pic>");
    }
    xml += QStringLiteral("</p:spTree></p:cSld></p:sld>");
    return xml.toUtf8();
}

inline QByteArray notesXml(const QString& notes)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
               "<p:notes xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
               " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
               " xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"><p:cSld><p:spTree>"
               "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image\"/><p:cNvSpPr/><p:nvPr><p:ph type=\"sldImg\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>"
               "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes\"/><p:cNvSpPr/><p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr>"
               "<p:txBody><a:bodyPr/><a:p><a:r><a:t>%1</a:t></a:r></a:p></p:txBody></p:sp>"
               "<p:sp><p:nvSpPr><p:cNvPr id=\"4\" name=\"Slide Number\"/><p:cNvSpPr/><p:nvPr><p:ph type=\"sldNum\"/></p:nvPr></p:nvSpPr>"
               "<p:txBody><a:bodyPr/><a:p><a:r><a:t>12</a:t></a:r></a:p></p:txBody></p:sp>"
               "</p:spTree></p:cSld></p:notes>")
        .arg(notes.toHtmlEscaped())
        .toUtf8();
}

/**
 * Builds a PPTX package. Slide files are named in reverse so the reader must use
 * presentation.xml's slide list (not file names) to get the order right.
 */
inline QByteArray buildPptx(const QList<TestSlide>& slides)
{
    ZipWriter zip;
    const QString relNs = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    QString presRels = QStringLiteral("<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    QString sldIds;
    const int n = static_cast<int>(slides.size());
    for (int i = 0; i < n; ++i) {
        const int fileNumber = n - i;   // reversed file numbering
        const QString rid = QStringLiteral("rId%1").arg(i + 10);
        presRels += QStringLiteral("<Relationship Id=\"%1\" Type=\"%2/slide\" Target=\"slides/slide%3.xml\"/>")
                        .arg(rid, relNs).arg(fileNumber);
        sldIds += QStringLiteral("<p:sldId id=\"%1\" r:id=\"%2\"/>").arg(256 + i).arg(rid);

        const TestSlide& s = slides.at(i);
        zip.add(QStringLiteral("ppt/slides/slide%1.xml").arg(fileNumber), slideXml(s), i % 2 == 0);
        QString slideRels = QStringLiteral("<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                           "<Relationship Id=\"rId1\" Type=\"%1/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>")
                                .arg(relNs);
        if (!s.notes.isEmpty()) {
            slideRels += QStringLiteral("<Relationship Id=\"rId2\" Type=\"%1/notesSlide\" Target=\"../notesSlides/notesSlide%2.xml\"/>")
                             .arg(relNs).arg(fileNumber);
            zip.add(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(fileNumber), notesXml(s.notes));
        }
        slideRels += QStringLiteral("</Relationships>");
        zip.add(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(fileNumber), slideRels.toUtf8());
    }
    presRels += QStringLiteral("</Relationships>");
    const QString pres = QStringLiteral(
                             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                             "<p:presentation xmlns:r=\"%1\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
                             "<p:sldIdLst>%2</p:sldIdLst></p:presentation>")
                             .arg(relNs, sldIds);
    zip.add(QStringLiteral("[Content_Types].xml"), QByteArray("<?xml version=\"1.0\"?><Types/>"), false);
    zip.add(QStringLiteral("ppt/presentation.xml"), pres.toUtf8());
    zip.add(QStringLiteral("ppt/_rels/presentation.xml.rels"), presRels.toUtf8());
    return zip.finish();
}

} // namespace StudyTestUtils
