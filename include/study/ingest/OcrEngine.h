#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

/** One recognised line of text and where it is on the image (image pixels). */
struct OcrLine {
    QString text;
    QRectF box;
};

struct OcrResult {
    bool ok = false;
    QString text;              // lines joined with newlines, in reading order
    QVector<OcrLine> lines;
    double confidence = 0.0;   // 0..1; engines without scores estimate it from the text
    QString language;          // BCP-47 tag used, e.g. "en-US"
    QString error;
};

/**
 * Offline text recognition for scanned pages, picture-only slides and imported images.
 * The built-in OS engine is used (Windows.Media.Ocr on Windows, Apple Vision on macOS):
 * nothing is downloaded or bundled, and nothing leaves the computer. recognize() blocks, so
 * it is called from the ingestion worker thread.
 */
class OcrEngine {
public:
    virtual ~OcrEngine() = default;

    /** False when the system has no OCR language installed (or no engine on this platform). */
    [[nodiscard]] virtual bool available() const = 0;
    /** "Windows OCR", "Apple Vision". */
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual QStringList languages() const = 0;
    [[nodiscard]] virtual OcrResult recognize(const QImage& image) = 0;

    /** The platform engine (or one that reports unavailable). Tests can replace the factory. */
    [[nodiscard]] static std::unique_ptr<OcrEngine> create();
    static void setFactoryForTests(std::function<std::unique_ptr<OcrEngine>()> factory);

    /** One line for Preferences: "Scanned pages are read with Windows OCR (en-US)." */
    [[nodiscard]] static QString statusText();

    /**
     * Rough quality of recognised text when the engine gives no score: the share of letters
     * and digits among visible characters, lowered for very short results.
     */
    [[nodiscard]] static double estimateConfidence(const QString& text);
};

/** Platform backends (defined in src/platform/...). */
std::unique_ptr<OcrEngine> createPlatformOcrEngine();
