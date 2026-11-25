#include "AnimationManager.h"

#include <QImage>
#include <QLabel>
#include <QMovie>
#include <QPixmap>
#include <QTransform>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

/**
 * Lightweight 3x3 edge-preserving unsharp mask applied once per cached upscaled
 * frame so Chiikawa's dark line-art outlines and eyes stay crisp at large sizes
 * without introducing transparent fringe halos.
 */
QImage sharpenUpscaledSprite(const QImage& input, double amount)
{
    if (input.isNull() || input.width() < 3 || input.height() < 3 || amount <= 0.01) {
        return input;
    }

    const QImage src = (input.format() == QImage::Format_ARGB32)
        ? input
        : input.convertToFormat(QImage::Format_ARGB32);
    QImage out(src.size(), QImage::Format_ARGB32);

    const int w = src.width();
    const int h = src.height();

    for (int y = 0; y < h; ++y) {
        const auto* rowPrev = reinterpret_cast<const QRgb*>(src.constScanLine(std::max(0, y - 1)));
        const auto* rowCurr = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        const auto* rowNext = reinterpret_cast<const QRgb*>(src.constScanLine(std::min(h - 1, y + 1)));
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));

        for (int x = 0; x < w; ++x) {
            const QRgb center = rowCurr[x];
            const int a = qAlpha(center);
            if (a < 24) {
                dst[x] = center;
                continue;
            }

            const int xm = std::max(0, x - 1);
            const int xp = std::min(w - 1, x + 1);
            const QRgb neighbors[4] = { rowPrev[x], rowNext[x], rowCurr[xm], rowCurr[xp] };

            int sumR = 0;
            int sumG = 0;
            int sumB = 0;
            int weightSum = 0;
            for (const QRgb nb : neighbors) {
                const int na = qAlpha(nb);
                if (na > 16) {
                    sumR += qRed(nb) * na;
                    sumG += qGreen(nb) * na;
                    sumB += qBlue(nb) * na;
                    weightSum += na;
                }
            }

            if (weightSum <= 0) {
                dst[x] = center;
                continue;
            }

            const double blurR = static_cast<double>(sumR) / static_cast<double>(weightSum);
            const double blurG = static_cast<double>(sumG) / static_cast<double>(weightSum);
            const double blurB = static_cast<double>(sumB) / static_cast<double>(weightSum);

            const int r = std::clamp(qRound(qRed(center) + (qRed(center) - blurR) * amount), 0, 255);
            const int g = std::clamp(qRound(qGreen(center) + (qGreen(center) - blurG) * amount), 0, 255);
            const int b = std::clamp(qRound(qBlue(center) + (qBlue(center) - blurB) * amount), 0, 255);
            dst[x] = qRgba(r, g, b, a);
        }
    }

    return out;
}

quint64 frameCacheKey(AnimationId id, int frameNumber, bool mirrored)
{
    return (static_cast<quint64>(static_cast<int>(id)) << 33)
        | (static_cast<quint64>(std::max(0, frameNumber)) << 1)
        | (mirrored ? 1ULL : 0ULL);
}

} // namespace

AnimationManager::AnimationManager(QLabel* displayLabel, QObject* parent)
    : QObject(parent)
    , m_label(displayLabel)
    , m_rng(static_cast<unsigned>(
          std::chrono::steady_clock::now().time_since_epoch().count()))
{
    m_autoSwitchTimer.setSingleShot(true);
    connect(&m_autoSwitchTimer, &QTimer::timeout, this, &AnimationManager::onAutoSwitchTimeout);

    if (m_label) {
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setAttribute(Qt::WA_TranslucentBackground);
        m_label->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        m_label->setScaledContents(false);
    }
}

AnimationManager::~AnimationManager()
{
    m_autoSwitchTimer.stop();
    for (auto* movie : m_movies) {
        if (movie) {
            movie->stop();
        }
    }
}

QString AnimationManager::resourcePath(AnimationId id)
{
    switch (id) {
    case AnimationId::Skip:
        return QStringLiteral(":/assets/chiikawa_skip.gif");
    case AnimationId::Stomp:
        return QStringLiteral(":/assets/chiikawa_stomp.gif");
    case AnimationId::Twirl:
        return QStringLiteral(":/assets/chiikawa_twirl.gif");
    case AnimationId::Fire:
        return QStringLiteral(":/assets/chiikawa_fire.gif");
    default:
        return {};
    }
}

QString AnimationManager::displayName(AnimationId id)
{
    switch (id) {
    case AnimationId::Skip:
        return QStringLiteral("Skip");
    case AnimationId::Stomp:
        return QStringLiteral("Stomp");
    case AnimationId::Twirl:
        return QStringLiteral("Twirl");
    case AnimationId::Fire:
        return QStringLiteral("Fire");
    default:
        return QStringLiteral("Unknown");
    }
}

QMovie* AnimationManager::currentMovie() const
{
    return m_movies.value(static_cast<int>(m_current), nullptr);
}

bool AnimationManager::loadAll()
{
    bool ok = true;
    for (int i = 0; i < static_cast<int>(AnimationId::Count); ++i) {
        const auto id = static_cast<AnimationId>(i);
        auto* movie = new QMovie(resourcePath(id), QByteArray(), this);
        movie->setCacheMode(QMovie::CacheAll);
        if (!movie->isValid()) {
            ok = false;
            delete movie;
            continue;
        }
        connect(movie, &QMovie::frameChanged, this, &AnimationManager::onFrameChanged);
        m_movies.insert(i, movie);
    }

    m_ready = !m_movies.isEmpty();
    if (m_ready) {
        play(AnimationId::Skip, false);
        if (m_autoSwitchEnabled) {
            scheduleNextAutoSwitch();
        }
    }
    return m_ready && ok;
}

void AnimationManager::clearFrameCacheIfNeeded(int physSize, qreal dpr)
{
    if (m_cachedPhysSize != physSize || std::abs(m_cachedDpr - dpr) > 0.001) {
        m_frameCache.clear();
        m_cachedPhysSize = physSize;
        m_cachedDpr = dpr;
    }
}

void AnimationManager::setTargetSize(int size)
{
    const int clamped = std::max(32, size);
    if (m_targetSize == clamped) {
        return;
    }
    m_targetSize = clamped;
    m_frameCache.clear();
    updateDisplayedFrame();
}

void AnimationManager::setMirrored(bool mirrored)
{
    if (m_mirrored == mirrored) {
        return;
    }
    m_mirrored = mirrored;
    updateDisplayedFrame();
}

void AnimationManager::updateDisplayedFrame()
{
    auto* movie = currentMovie();
    if (!movie || !m_label) {
        return;
    }

    const qreal dpr = std::max<qreal>(1.0, m_label->devicePixelRatioF());
    const int physSize = std::max(32, qRound(m_targetSize * dpr));
    clearFrameCacheIfNeeded(physSize, dpr);

    const int frameNumber = movie->currentFrameNumber();
    const quint64 key = frameCacheKey(m_current, frameNumber, m_mirrored);
    if (auto it = m_frameCache.constFind(key); it != m_frameCache.constEnd()) {
        m_label->setPixmap(it.value());
        emit frameUpdated(it.value(), m_current);
        return;
    }

    // Decode at full native resolution (no QMovie::setScaledSize) and scale once
    // into HiDPI physical pixels so sprites stay sharp at every size and DPI.
    QImage native = movie->currentImage();
    if (native.isNull()) {
        return;
    }

    QImage scaled = native.scaled(QSize(physSize, physSize),
                                  Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation);

    const int nativeMax = std::max(1, std::max(native.width(), native.height()));
    if (physSize > nativeMax) {
        const double ratio = static_cast<double>(physSize) / static_cast<double>(nativeMax);
        const double sharpenAmount = std::clamp((ratio - 1.0) * 0.65, 0.20, 0.60);
        scaled = sharpenUpscaledSprite(scaled, sharpenAmount);
    }

    if (m_mirrored) {
        scaled = scaled.transformed(QTransform().scale(-1, 1));
    }

    QPixmap frame = QPixmap::fromImage(scaled);
    frame.setDevicePixelRatio(dpr);
    m_frameCache.insert(key, frame);

    m_label->setPixmap(frame);
    emit frameUpdated(frame, m_current);
}

void AnimationManager::onFrameChanged()
{
    auto* movie = qobject_cast<QMovie*>(sender());
    if (!movie || movie != currentMovie()) {
        return;
    }
    updateDisplayedFrame();
}

void AnimationManager::play(AnimationId id, bool mirrored)
{
    auto* movie = m_movies.value(static_cast<int>(id), nullptr);
    if (!movie || !m_label) {
        return;
    }

    if (auto* previous = currentMovie(); previous && previous != movie) {
        previous->stop();
    }

    m_current = id;
    m_mirrored = mirrored;

    // Drive display ourselves so mirroring and HiDPI scaling work without QMovie fighting setPixmap
    m_label->setMovie(nullptr);

    if (movie->state() != QMovie::Running) {
        movie->start();
    } else {
        updateDisplayedFrame();
    }

    emit animationChanged(id);
}

AnimationId AnimationManager::pickRandom(bool avoidCurrent)
{
    const int count = static_cast<int>(AnimationId::Count);
    std::uniform_int_distribution<int> dist(0, count - 1);
    AnimationId chosen = static_cast<AnimationId>(dist(m_rng));
    if (avoidCurrent && count > 1) {
        int guard = 0;
        while (chosen == m_current && guard++ < 8) {
            chosen = static_cast<AnimationId>(dist(m_rng));
        }
        if (chosen == m_current) {
            chosen = static_cast<AnimationId>((static_cast<int>(m_current) + 1) % count);
        }
    }
    return chosen;
}

void AnimationManager::playRandom(bool avoidCurrent)
{
    play(pickRandom(avoidCurrent), m_mirrored);
}

void AnimationManager::playCelebration()
{
    play(AnimationId::Twirl, false);
}

void AnimationManager::setAutoSwitchEnabled(bool enabled)
{
    m_autoSwitchEnabled = enabled;
    if (enabled) {
        scheduleNextAutoSwitch();
    } else {
        m_autoSwitchTimer.stop();
    }
}

void AnimationManager::scheduleNextAutoSwitch()
{
    if (!m_autoSwitchEnabled) {
        return;
    }
    std::uniform_int_distribution<int> dist(10'000, 30'000);
    m_autoSwitchTimer.start(dist(m_rng));
}

void AnimationManager::onAutoSwitchTimeout()
{
    if (!m_autoSwitchEnabled) {
        return;
    }
    std::uniform_int_distribution<int> roll(0, 9);
    if (roll(m_rng) == 0) {
        play(AnimationId::Fire, m_mirrored);
    } else {
        playRandom(true);
    }
    scheduleNextAutoSwitch();
}
