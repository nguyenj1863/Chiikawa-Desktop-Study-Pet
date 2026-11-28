// macOS OCR backend: Apple's Vision framework (VNRecognizeTextRequest). Built into macOS,
// offline, nothing to bundle.
#include "study/ingest/OcrEngine.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

#include <QImage>

namespace {

class VisionOcrEngine final : public OcrEngine {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Apple Vision"); }
    [[nodiscard]] QStringList languages() const override
    {
        QStringList out;
        @autoreleasepool {
            VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];
            request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            NSError* error = nil;
            NSArray<NSString*>* langs = [request supportedRecognitionLanguagesAndReturnError:&error];
            for (NSString* l in langs) {
                out.append(QString::fromNSString(l));
            }
        }
        return out;
    }

    [[nodiscard]] OcrResult recognize(const QImage& input) override
    {
        OcrResult result;
        if (input.isNull()) {
            result.error = QStringLiteral("Empty image.");
            return result;
        }
        @autoreleasepool {
            CGImageRef cgImage = input.toCGImage();
            if (!cgImage) {
                result.error = QStringLiteral("The image could not be prepared for text recognition.");
                return result;
            }
            VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];
            request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            request.usesLanguageCorrection = YES;
            VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
            NSError* error = nil;
            const BOOL ok = [handler performRequests:@[request] error:&error];
            CGImageRelease(cgImage);
            if (!ok) {
                result.error = error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("Text recognition failed.");
                return result;
            }
            QStringList texts;
            double confidenceSum = 0.0;
            const double w = input.width();
            const double h = input.height();
            for (VNRecognizedTextObservation* observation in request.results) {
                VNRecognizedText* best = [[observation topCandidates:1] firstObject];
                if (!best) {
                    continue;
                }
                OcrLine line;
                line.text = QString::fromNSString(best.string);
                // Vision boxes are normalised with the origin at the bottom left.
                const CGRect b = observation.boundingBox;
                line.box = QRectF(b.origin.x * w, (1.0 - b.origin.y - b.size.height) * h, b.size.width * w, b.size.height * h);
                texts.append(line.text);
                confidenceSum += best.confidence;
                result.lines.append(line);
            }
            result.text = texts.join(QLatin1Char('\n'));
            result.confidence = result.lines.isEmpty() ? 0.0 : confidenceSum / result.lines.size();
            result.ok = true;
        }
        return result;
    }
};

} // namespace

std::unique_ptr<OcrEngine> createPlatformOcrEngine()
{
    return std::make_unique<VisionOcrEngine>();
}
