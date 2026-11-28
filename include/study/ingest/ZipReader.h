#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

/**
 * Minimal read-only ZIP archive reader for Office Open XML files (PPTX).
 * Supports stored and deflate entries; rejects encryption, ZIP64, and
 * entries whose declared size exceeds kMaxEntryBytes.
 */
class ZipReader {
public:
    static constexpr qint64 kMaxEntryBytes = 64LL * 1024 * 1024;

    /** Parses the central directory; returns false for anything that is not a readable ZIP. */
    bool open(const QByteArray& archive);
    bool openFile(const QString& path);

    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] QString errorString() const { return m_error; }
    [[nodiscard]] QStringList entryNames() const { return m_order; }
    [[nodiscard]] bool contains(const QString& name) const { return m_entries.contains(name); }

    /** Returns the decompressed entry, or nullopt when missing or corrupt. */
    [[nodiscard]] std::optional<QByteArray> read(const QString& name);

    /** Raw DEFLATE (RFC 1951) decoder; exposed for tests. */
    [[nodiscard]] static std::optional<QByteArray> inflateRaw(const QByteArray& compressed, qint64 expectedSize);

private:
    struct Entry {
        quint16 method = 0;
        quint16 flags = 0;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QString m_error;
    bool m_open = false;
};
