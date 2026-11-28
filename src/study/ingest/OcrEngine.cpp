#include "study/ingest/OcrEngine.h"

#include <algorithm>

namespace {

std::function<std::unique_ptr<OcrEngine>()>& factory()
{
    static std::function<std::unique_ptr<OcrEngine>()> f;
    return f;
}

/** Used on platforms without a built-in engine: reports unavailable, never recognises anything. */
class NullOcrEngine final : public OcrEngine {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] QString name() const override { return QStringLiteral("OCR"); }
    [[nodiscard]] QStringList languages() const override { return {}; }
    [[nodiscard]] OcrResult recognize(const QImage&) override
    {
        OcrResult r;
        r.error = QStringLiteral("Text recognition is not available on this system.");
        return r;
    }
};

} // namespace

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
std::unique_ptr<OcrEngine> createPlatformOcrEngine()
{
    return std::make_unique<NullOcrEngine>();
}
#endif

std::unique_ptr<OcrEngine> OcrEngine::create()
{
    if (factory()) {
        return factory()();
    }
    std::unique_ptr<OcrEngine> engine = createPlatformOcrEngine();
    return engine ? std::move(engine) : std::make_unique<NullOcrEngine>();
}

void OcrEngine::setFactoryForTests(std::function<std::unique_ptr<OcrEngine>()> f)
{
    factory() = std::move(f);
}

QString OcrEngine::statusText()
{
    const std::unique_ptr<OcrEngine> engine = create();
    if (!engine->available()) {
#ifdef Q_OS_WIN
        return QStringLiteral("Scanned pages can't be read yet: add a language with text recognition in Windows "
                              "Settings > Time & language > Language.");
#else
        return QStringLiteral("Scanned pages can't be read on this system.");
#endif
    }
    const QStringList langs = engine->languages();
    return QStringLiteral("Scanned pages and images are read on this computer with %1%2.")
        .arg(engine->name(), langs.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(langs.mid(0, 3).join(QStringLiteral(", "))));
}

double OcrEngine::estimateConfidence(const QString& text)
{
    int visible = 0;
    int wordChars = 0;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            continue;
        }
        ++visible;
        if (ch.isLetterOrNumber()) {
            ++wordChars;
        }
    }
    if (visible == 0) {
        return 0.0;
    }
    const double ratio = static_cast<double>(wordChars) / visible;
    const double lengthFactor = std::min(1.0, visible / 40.0);
    return ratio * (0.6 + 0.4 * lengthFactor);
}
