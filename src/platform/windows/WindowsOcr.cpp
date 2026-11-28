// Windows OCR backend: Windows.Media.Ocr through C++/WinRT. Built into Windows 10 and later,
// offline, nothing to bundle. Recognition needs an installed language with OCR support.
#include "study/ingest/OcrEngine.h"

#ifdef Q_OS_WIN

#include <QtGlobal>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cstring>
#include <thread>

namespace {

using namespace winrt::Windows::Graphics::Imaging;
namespace WinOcr = winrt::Windows::Media::Ocr;

/** Runs WinRT work on a fresh multi-threaded apartment thread (never on the UI's STA thread). */
template <typename F>
void onMtaThread(F&& work)
{
    std::thread t([&work]() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
        }
        work();
        winrt::uninit_apartment();
    });
    t.join();
}

class WindowsOcrEngine final : public ::OcrEngine {
public:
    WindowsOcrEngine()
    {
        onMtaThread([this]() {
            try {
                for (const auto& lang : WinOcr::OcrEngine::AvailableRecognizerLanguages()) {
                    m_languages.append(QString::fromWCharArray(lang.LanguageTag().c_str()));
                }
                m_maxDimension = static_cast<int>(WinOcr::OcrEngine::MaxImageDimension());
            } catch (...) {
                m_languages.clear();
            }
        });
    }

    [[nodiscard]] bool available() const override { return !m_languages.isEmpty(); }
    [[nodiscard]] QString name() const override { return QStringLiteral("Windows OCR"); }
    [[nodiscard]] QStringList languages() const override { return m_languages; }

    [[nodiscard]] ::OcrResult recognize(const QImage& input) override
    {
        ::OcrResult result;
        if (!available()) {
            result.error = QStringLiteral("No OCR language is installed in Windows.");
            return result;
        }
        if (input.isNull()) {
            result.error = QStringLiteral("Empty image.");
            return result;
        }
        // The engine caps image size; small text needs enough pixels, so only shrink when required.
        QImage image = input;
        const int limit = std::max(1000, m_maxDimension > 0 ? m_maxDimension : 10000);
        if (image.width() > limit || image.height() > limit) {
            image = image.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);   // BGRA in memory
        const double scaleBack = static_cast<double>(input.width()) / image.width();

        onMtaThread([&]() {
            try {
                auto engine = WinOcr::OcrEngine::TryCreateFromUserProfileLanguages();
                if (!engine) {
                    engine = WinOcr::OcrEngine::TryCreateFromLanguage(
                        winrt::Windows::Globalization::Language(winrt::hstring(m_languages.first().toStdWString())));
                }
                if (!engine) {
                    result.error = QStringLiteral("Windows OCR could not start.");
                    return;
                }
                SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, image.width(), image.height(), BitmapAlphaMode::Premultiplied);
                const uint32_t bytes = static_cast<uint32_t>(image.sizeInBytes());
                winrt::Windows::Storage::Streams::Buffer buffer(bytes);
                std::memcpy(buffer.data(), image.constBits(), bytes);
                buffer.Length(bytes);
                bitmap.CopyFromBuffer(buffer);

                const auto ocr = engine.RecognizeAsync(bitmap).get();
                QStringList texts;
                for (const auto& line : ocr.Lines()) {
                    ::OcrLine out;
                    out.text = QString::fromWCharArray(line.Text().c_str());
                    QRectF box;
                    for (const auto& word : line.Words()) {
                        const auto r = word.BoundingRect();
                        const QRectF wr(r.X * scaleBack, r.Y * scaleBack, r.Width * scaleBack, r.Height * scaleBack);
                        box = box.isNull() ? wr : box.united(wr);
                    }
                    out.box = box;
                    texts.append(out.text);
                    result.lines.append(out);
                }
                result.text = texts.join(QLatin1Char('\n'));
                result.language = QString::fromWCharArray(engine.RecognizerLanguage().LanguageTag().c_str());
                result.confidence = ::OcrEngine::estimateConfidence(result.text);
                result.ok = true;
            } catch (const winrt::hresult_error& e) {
                result.error = QString::fromWCharArray(e.message().c_str());
            } catch (...) {
                result.error = QStringLiteral("Windows OCR failed.");
            }
        });
        return result;
    }

private:
    QStringList m_languages;
    int m_maxDimension = 0;
};

} // namespace

std::unique_ptr<::OcrEngine> createPlatformOcrEngine()
{
    return std::make_unique<WindowsOcrEngine>();
}

#endif // Q_OS_WIN
