#pragma once

#include "ai/LlmClient.h"
#include "study/StudyTypes.h"

#include <QString>
#include <QStringList>
#include <optional>

/**
 * Reading figures with a local vision model. OCR only returns the words in an image; a
 * vision-language model can say what a diagram's arrows mean, read a chart's values, or
 * rebuild a table. What it reads is stored on the asset once and reused for every question.
 * Pure helpers: the request goes through LlmClient like any other.
 */
namespace FigureInterpreter {

/** Long edge images are scaled to before sending, to keep small models quick. */
inline constexpr int kMaxImageEdge = 1280;

struct Reading {
    QString kind;              // diagram, chart, table, equation, screenshot, photo, text, decorative
    bool legible = false;      // false: unreadable, decorative, or contradicted by the image's own text
    QString title;
    QString summary;
    QStringList elements;      // labelled parts and what they are
    QStringList relationships; // "A -> B: sends requests"
    TableData table;
    QStringList values;        // numbers, axes, data points
    QString readableText;      // text the model could read in the image

    /** The reading as plain text for prompts, evidence checks and the student. */
    [[nodiscard]] QString toText() const;
};

[[nodiscard]] QByteArray schema();

/**
 * Request for one image. context is the caption and nearby slide or page text, which helps the
 * model name things the way the course does. model empty uses the client's model.
 */
[[nodiscard]] LlmRequest buildRequest(const QByteArray& image, const QString& context, const QString& model);

/** Loads an asset image, scales it to kMaxImageEdge and encodes it as JPEG; empty on failure. */
[[nodiscard]] QByteArray prepareImage(const QString& path);

[[nodiscard]] std::optional<Reading> parse(const QString& json, QString* error = nullptr);

/**
 * Cross-check against OCR: when the image's text was read separately, most labels the model
 * lists must actually appear in it; otherwise the reading is treated as unreliable.
 */
[[nodiscard]] bool agreesWithOcr(const Reading& reading, const QString& ocrText);

} // namespace FigureInterpreter
