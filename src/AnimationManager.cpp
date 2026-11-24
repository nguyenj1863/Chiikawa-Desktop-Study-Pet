#include "AnimationManager.h"

#include <QLabel>
#include <QMovie>
#include <QPixmap>
#include <QTransform>
#include <algorithm>
#include <chrono>

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
        applyScaledSize(movie);
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

void AnimationManager::applyScaledSize(QMovie* movie)
{
    if (!movie) {
        return;
    }
    movie->setScaledSize(QSize(m_targetSize, m_targetSize));
}

void AnimationManager::setTargetSize(int size)
{
    m_targetSize = std::max(32, size);
    for (auto it = m_movies.begin(); it != m_movies.end(); ++it) {
        applyScaledSize(it.value());
    }
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
    QPixmap frame = movie->currentPixmap();
    if (frame.isNull()) {
        return;
    }
    // QMovie can return native-resolution frames even after setScaledSize.
    // Always normalize so the pet stays a consistent size.
    if (frame.height() != m_targetSize || frame.width() != m_targetSize) {
        frame = frame.scaled(QSize(m_targetSize, m_targetSize),
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    if (m_mirrored) {
        frame = frame.transformed(QTransform().scale(-1, 1));
    }
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

    // Drive display ourselves so mirroring works without QMovie fighting setPixmap
    m_label->setMovie(nullptr);
    applyScaledSize(movie);

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
