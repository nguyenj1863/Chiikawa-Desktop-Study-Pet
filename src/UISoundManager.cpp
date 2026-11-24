#include "UISoundManager.h"

#include <QSoundEffect>
#include <QUrl>
#include <algorithm>

UISoundManager::UISoundManager(QObject* parent)
    : QObject(parent)
{
    ensureLoaded();
}

UISoundManager::~UISoundManager()
{
    if (m_clickEffect) {
        m_clickEffect->stop();
    }
}

QString UISoundManager::resourceUrl()
{
    return QStringLiteral("qrc:/assets/sounds/soft_pixel_pop.wav");
}

void UISoundManager::ensureLoaded()
{
    if (m_clickEffect) {
        return;
    }
    m_clickEffect = new QSoundEffect(this);
    m_clickEffect->setSource(QUrl(resourceUrl()));
    m_clickEffect->setLoopCount(1);
    m_clickEffect->setVolume(static_cast<float>(m_volume));
}

void UISoundManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled && m_clickEffect && m_clickEffect->isPlaying()) {
        m_clickEffect->stop();
    }
}

void UISoundManager::setVolume(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_clickEffect) {
        m_clickEffect->setVolume(static_cast<float>(m_volume));
    }
}

bool UISoundManager::isLoaded() const
{
    return m_clickEffect && m_clickEffect->status() != QSoundEffect::Error
           && !m_clickEffect->source().isEmpty();
}

void UISoundManager::playClick()
{
    if (!m_enabled) {
        return;
    }

    // Throttle rapid repeated clicks to prevent playback floods or harsh overlap
    if (m_lastPlayTimer.isValid() && m_lastPlayTimer.elapsed() < kMinClickIntervalMs) {
        return;
    }
    m_lastPlayTimer.restart();
    ++m_playCount;

    if (m_volume <= 0.0) {
        return;
    }

    ensureLoaded();
    if (!m_clickEffect || m_clickEffect->status() == QSoundEffect::Error) {
        return;
    }

    if (m_clickEffect->isPlaying()) {
        m_clickEffect->stop();
    }
    m_clickEffect->play();
}
