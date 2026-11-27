#include "study/ZipReader.h"

#include <QFile>
#include <array>
#include <cstring>

namespace {

quint16 le16(const char* p)
{
    return static_cast<quint16>(static_cast<quint8>(p[0]) | (static_cast<quint8>(p[1]) << 8));
}

quint32 le32(const char* p)
{
    return static_cast<quint32>(le16(p)) | (static_cast<quint32>(le16(p + 2)) << 16);
}

quint32 crc32(const QByteArray& data)
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

// ---------------------------------------------------------------------------
// RFC 1951 inflate, structured after zlib's "puff" reference decoder.
// ---------------------------------------------------------------------------
constexpr int kMaxBits = 15;
constexpr int kMaxLitLen = 286;
constexpr int kMaxDist = 30;
constexpr int kFixedLitLen = 288;

struct Huffman {
    std::array<short, kMaxBits + 1> count{};
    std::array<short, kFixedLitLen> symbol{};
};

class Inflater {
public:
    Inflater(const QByteArray& in, qint64 limit) : m_in(in), m_limit(limit) {}

    bool run()
    {
        int last = 0;
        do {
            if (!bits(1, &last)) {
                return false;
            }
            int type = 0;
            if (!bits(2, &type)) {
                return false;
            }
            bool ok = false;
            if (type == 0) {
                ok = stored();
            } else if (type == 1) {
                ok = fixed();
            } else if (type == 2) {
                ok = dynamic();
            }
            if (!ok) {
                return false;
            }
        } while (!last);
        return true;
    }

    QByteArray output() const { return m_out; }

private:
    bool bits(int need, int* value)
    {
        long val = m_bitBuf;
        while (m_bitCount < need) {
            if (m_pos >= m_in.size()) {
                return false;
            }
            val |= static_cast<long>(static_cast<quint8>(m_in.at(m_pos++))) << m_bitCount;
            m_bitCount += 8;
        }
        m_bitBuf = static_cast<int>(val >> need);
        m_bitCount -= need;
        *value = static_cast<int>(val & ((1L << need) - 1));
        return true;
    }

    bool put(char byte)
    {
        if (m_out.size() >= m_limit) {
            return false;
        }
        m_out.append(byte);
        return true;
    }

    bool stored()
    {
        m_bitBuf = 0;
        m_bitCount = 0;
        if (m_pos + 4 > m_in.size()) {
            return false;
        }
        const quint16 len = le16(m_in.constData() + m_pos);
        const quint16 nlen = le16(m_in.constData() + m_pos + 2);
        m_pos += 4;
        if (len != static_cast<quint16>(~nlen)) {
            return false;
        }
        if (m_pos + len > m_in.size() || m_out.size() + len > m_limit) {
            return false;
        }
        m_out.append(m_in.constData() + m_pos, len);
        m_pos += len;
        return true;
    }

    bool decode(const Huffman& h, int* symbol)
    {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            int bit = 0;
            if (!bits(1, &bit)) {
                return false;
            }
            code |= bit;
            const int count = h.count[len];
            if (code - count < first) {
                *symbol = h.symbol[index + (code - first)];
                return true;
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }

    // Returns 0 for a complete code, >0 for incomplete, <0 for over-subscribed.
    static int construct(Huffman* h, const short* length, int n)
    {
        h->count.fill(0);
        for (int s = 0; s < n; ++s) {
            h->count[length[s]]++;
        }
        if (h->count[0] == n) {
            return 0;
        }
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= h->count[len];
            if (left < 0) {
                return left;
            }
        }
        std::array<short, kMaxBits + 1> offs{};
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) {
            offs[len + 1] = static_cast<short>(offs[len] + h->count[len]);
        }
        for (int s = 0; s < n; ++s) {
            if (length[s] != 0) {
                h->symbol[offs[length[s]]++] = static_cast<short>(s);
            }
        }
        return left;
    }

    bool codes(const Huffman& lencode, const Huffman& distcode)
    {
        static constexpr short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                           3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                            6145, 8193, 12289, 16385, 24577};
        static constexpr short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        int symbol = 0;
        do {
            if (!decode(lencode, &symbol)) {
                return false;
            }
            if (symbol < 256) {
                if (!put(static_cast<char>(symbol))) {
                    return false;
                }
            } else if (symbol > 256) {
                symbol -= 257;
                if (symbol >= 29) {
                    return false;
                }
                int extra = 0;
                if (!bits(lext[symbol], &extra)) {
                    return false;
                }
                const int len = lbase[symbol] + extra;
                if (!decode(distcode, &symbol) || symbol >= 30) {
                    return false;
                }
                if (!bits(dext[symbol], &extra)) {
                    return false;
                }
                const qsizetype dist = dbase[symbol] + extra;
                if (dist > m_out.size()) {
                    return false;
                }
                for (int i = 0; i < len; ++i) {
                    if (!put(m_out.at(m_out.size() - dist))) {
                        return false;
                    }
                }
            }
        } while (symbol != 256);
        return true;
    }

    bool fixed()
    {
        static Huffman lencode;
        static Huffman distcode;
        static const bool built = [] {
            std::array<short, kFixedLitLen> lengths{};
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < kFixedLitLen; ++s) lengths[s] = 8;
            construct(&lencode, lengths.data(), kFixedLitLen);
            std::array<short, kMaxDist> dl{};
            dl.fill(5);
            construct(&distcode, dl.data(), kMaxDist);
            return true;
        }();
        Q_UNUSED(built);
        return codes(lencode, distcode);
    }

    bool dynamic()
    {
        static constexpr short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = 0;
        int ndist = 0;
        int ncode = 0;
        if (!bits(5, &nlen) || !bits(5, &ndist) || !bits(4, &ncode)) {
            return false;
        }
        nlen += 257;
        ndist += 1;
        ncode += 4;
        if (nlen > kMaxLitLen || ndist > kMaxDist) {
            return false;
        }
        std::array<short, kMaxLitLen + kMaxDist> lengths{};
        int index = 0;
        for (; index < ncode; ++index) {
            int v = 0;
            if (!bits(3, &v)) {
                return false;
            }
            lengths[order[index]] = static_cast<short>(v);
        }
        for (; index < 19; ++index) {
            lengths[order[index]] = 0;
        }
        Huffman lencode;
        Huffman distcode;
        if (construct(&lencode, lengths.data(), 19) != 0) {
            return false;
        }
        index = 0;
        while (index < nlen + ndist) {
            int symbol = 0;
            if (!decode(lencode, &symbol)) {
                return false;
            }
            if (symbol < 16) {
                lengths[index++] = static_cast<short>(symbol);
                continue;
            }
            short len = 0;
            int rep = 0;
            if (symbol == 16) {
                if (index == 0) {
                    return false;
                }
                len = lengths[index - 1];
                if (!bits(2, &rep)) {
                    return false;
                }
                rep += 3;
            } else if (symbol == 17) {
                if (!bits(3, &rep)) {
                    return false;
                }
                rep += 3;
            } else {
                if (!bits(7, &rep)) {
                    return false;
                }
                rep += 11;
            }
            if (index + rep > nlen + ndist) {
                return false;
            }
            while (rep--) {
                lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) {
            return false;
        }
        const int err = construct(&lencode, lengths.data(), nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) {
            return false;
        }
        const int derr = construct(&distcode, lengths.data() + nlen, ndist);
        if (derr < 0 || (derr > 0 && ndist - distcode.count[0] != 1)) {
            return false;
        }
        return codes(lencode, distcode);
    }

    const QByteArray& m_in;
    qint64 m_limit = 0;
    qsizetype m_pos = 0;
    int m_bitBuf = 0;
    int m_bitCount = 0;
    QByteArray m_out;
};

} // namespace

std::optional<QByteArray> ZipReader::inflateRaw(const QByteArray& compressed, qint64 expectedSize)
{
    Inflater inflater(compressed, expectedSize >= 0 ? expectedSize : kMaxEntryBytes);
    if (!inflater.run()) {
        return std::nullopt;
    }
    QByteArray out = inflater.output();
    if (expectedSize >= 0 && out.size() != expectedSize) {
        return std::nullopt;
    }
    return out;
}

bool ZipReader::openFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Could not open the file.");
        m_open = false;
        return false;
    }
    if (file.size() > 512LL * 1024 * 1024) {
        m_error = QStringLiteral("The file is too large.");
        m_open = false;
        return false;
    }
    return open(file.readAll());
}

bool ZipReader::open(const QByteArray& archive)
{
    m_data = archive;
    m_entries.clear();
    m_order.clear();
    m_open = false;
    m_error.clear();

    constexpr int kEocdSize = 22;
    if (m_data.size() < kEocdSize) {
        m_error = QStringLiteral("Not a ZIP archive.");
        return false;
    }
    // The end-of-central-directory record sits in the last 64 KiB + 22 bytes.
    qsizetype eocd = -1;
    const qsizetype minPos = std::max<qsizetype>(0, m_data.size() - kEocdSize - 0xFFFF);
    for (qsizetype i = m_data.size() - kEocdSize; i >= minPos; --i) {
        if (le32(m_data.constData() + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        m_error = QStringLiteral("Not a ZIP archive.");
        return false;
    }
    const char* e = m_data.constData() + eocd;
    const quint16 total = le16(e + 10);
    const quint32 cdSize = le32(e + 12);
    const quint32 cdOffset = le32(e + 16);
    if (total == 0xFFFF || cdOffset == 0xFFFFFFFFu) {
        m_error = QStringLiteral("ZIP64 archives are not supported.");
        return false;
    }
    if (static_cast<qint64>(cdOffset) + cdSize > eocd) {
        m_error = QStringLiteral("Corrupt ZIP directory.");
        return false;
    }

    qsizetype p = cdOffset;
    for (int i = 0; i < total; ++i) {
        if (p + 46 > eocd || le32(m_data.constData() + p) != 0x02014b50u) {
            m_error = QStringLiteral("Corrupt ZIP directory.");
            return false;
        }
        const char* c = m_data.constData() + p;
        Entry entry;
        entry.flags = le16(c + 8);
        entry.method = le16(c + 10);
        entry.crc = le32(c + 16);
        entry.compressedSize = le32(c + 20);
        entry.uncompressedSize = le32(c + 24);
        const quint16 nameLen = le16(c + 28);
        const quint16 extraLen = le16(c + 30);
        const quint16 commentLen = le16(c + 32);
        entry.localHeaderOffset = le32(c + 42);
        if (p + 46 + nameLen > eocd) {
            m_error = QStringLiteral("Corrupt ZIP directory.");
            return false;
        }
        const QString name = QString::fromUtf8(c + 46, nameLen);
        if (!m_entries.contains(name)) {
            m_order.append(name);
        }
        m_entries.insert(name, entry);
        p += 46 + nameLen + extraLen + commentLen;
    }
    m_open = true;
    return true;
}

std::optional<QByteArray> ZipReader::read(const QString& name)
{
    if (!m_open) {
        return std::nullopt;
    }
    const auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd()) {
        return std::nullopt;
    }
    const Entry& entry = it.value();
    if (entry.flags & 0x1) {
        m_error = QStringLiteral("Encrypted ZIP entries are not supported.");
        return std::nullopt;
    }
    if (entry.uncompressedSize > kMaxEntryBytes) {
        m_error = QStringLiteral("ZIP entry is too large.");
        return std::nullopt;
    }
    const qsizetype lh = entry.localHeaderOffset;
    if (lh + 30 > m_data.size() || le32(m_data.constData() + lh) != 0x04034b50u) {
        return std::nullopt;
    }
    const quint16 nameLen = le16(m_data.constData() + lh + 26);
    const quint16 extraLen = le16(m_data.constData() + lh + 28);
    const qsizetype start = lh + 30 + nameLen + extraLen;
    if (start + entry.compressedSize > m_data.size()) {
        return std::nullopt;
    }
    const QByteArray raw = m_data.mid(start, entry.compressedSize);

    std::optional<QByteArray> out;
    if (entry.method == 0) {
        out = raw;
    } else if (entry.method == 8) {
        out = inflateRaw(raw, entry.uncompressedSize);
    } else {
        m_error = QStringLiteral("Unsupported ZIP compression method.");
        return std::nullopt;
    }
    if (!out || out->size() != static_cast<qsizetype>(entry.uncompressedSize) || crc32(*out) != entry.crc) {
        m_error = QStringLiteral("Corrupt ZIP entry.");
        return std::nullopt;
    }
    return out;
}
