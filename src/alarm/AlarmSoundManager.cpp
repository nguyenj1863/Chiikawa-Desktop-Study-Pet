#include "alarm/AlarmSoundManager.h"
#include "app/SettingsDefaults.h"

#include <QSoundEffect>
#include <QUrl>
#include <algorithm>

AlarmSoundManager::AlarmSoundManager(QObject* parent)
    : QObject(parent)
{
    m_repeatTimer.setSingleShot(false);
    connect(&m_repeatTimer, &QTimer::timeout, this, &AlarmSoundManager::onRepeatTick);
}

AlarmSoundManager::~AlarmSoundManager()
{
    stop();
}

QList<AlarmSoundManager::SoundOption> AlarmSoundManager::availableSounds()
{
    return {
        {QStringLiteral("soft_chime"), QStringLiteral("Soft Chime"), QStringLiteral("qrc:/assets/sounds/soft_chime.wav")},
        {QStringLiteral("tiny_bell"), QStringLiteral("Tiny Bell"), QStringLiteral("qrc:/assets/sounds/tiny_bell.wav")},
        {QStringLiteral("cozy_pop"), QStringLiteral("Cozy Pop"), QStringLiteral("qrc:/assets/sounds/cozy_pop.wav")},
    };
}

QString AlarmSoundManager::resourceUrlFor(const QString& soundId)
{
    for (const auto& opt : availableSounds()) {
        if (opt.id == soundId) {
            return opt.resourceUrl;
        }
    }
    return QStringLiteral("qrc:/assets/sounds/soft_chime.wav");
}

QString AlarmSoundManager::displayNameFor(const QString& soundId)
{
    for (const auto& opt : availableSounds()) {
        if (opt.id == soundId) {
            return opt.label;
        }
    }
    return QStringLiteral("Soft Chime");
}

void AlarmSoundManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled) {
        stop();
    }
}

void AlarmSoundManager::setVolume(double volume)
{
    m_volume = std::clamp(volume, SettingsDefaults::kMinAlarmVolume, SettingsDefaults::kMaxAlarmVolume);
    for (auto* effect : m_effects) {
        if (effect) {
            effect->setVolume(static_cast<float>(m_volume));
        }
    }
}

void AlarmSoundManager::setSelectedSound(const QString& soundId)
{
    for (const auto& opt : availableSounds()) {
        if (opt.id == soundId) {
            m_selectedSound = soundId;
            return;
        }
    }
    m_selectedSound = SettingsDefaults::defaultAlarmSoundId();
}

QSoundEffect* AlarmSoundManager::effectFor(const QString& soundId)
{
    const QString key = soundId.isEmpty() ? m_selectedSound : soundId;
    if (auto it = m_effects.constFind(key); it != m_effects.constEnd()) {
        return it.value();
    }

    auto* effect = new QSoundEffect(this);
    effect->setSource(QUrl(resourceUrlFor(key)));
    effect->setLoopCount(1);
    effect->setVolume(static_cast<float>(m_volume));
    m_effects.insert(key, effect);
    return effect;
}

void AlarmSoundManager::playSingleInternal(const QString& soundId)
{
    // Prevent overlapping audio by stopping all effects first.
    for (auto* eff : m_effects) {
        if (eff && eff->isPlaying()) {
            eff->stop();
        }
    }

    const QString key = soundId.isEmpty() ? m_selectedSound : soundId;
    QSoundEffect* effect = effectFor(key);
    if (!effect) {
        return;
    }
    effect->setVolume(static_cast<float>(m_volume));
    m_activeSoundId = key;
    effect->play();
    emit playbackStarted(key);
}

void AlarmSoundManager::playOnce()
{
    if (!m_enabled) {
        return;
    }
    stop();
    playSingleInternal(m_selectedSound);
}

void AlarmSoundManager::playAlarmSequence()
{
    if (!m_enabled) {
        return;
    }
    stop();
    m_remainingRepeats = kAlarmRepeatCount - 1;
    playSingleInternal(m_selectedSound);
    if (m_remainingRepeats > 0) {
        m_repeatTimer.start(kAlarmRepeatIntervalMs);
    }
}

void AlarmSoundManager::preview(const QString& soundId)
{
    // Preview works even if user is testing sounds before toggling, as long as volume > 0
    stop();
    playSingleInternal(soundId.isEmpty() ? m_selectedSound : soundId);
}

void AlarmSoundManager::stop()
{
    const bool wasActive = isPlaying() || m_repeatTimer.isActive();
    m_repeatTimer.stop();
    m_remainingRepeats = 0;
    for (auto* effect : m_effects) {
        if (effect && effect->isPlaying()) {
            effect->stop();
        }
    }
    m_activeSoundId.clear();
    if (wasActive) {
        emit playbackStopped();
    }
}

bool AlarmSoundManager::isPlaying() const
{
    if (m_repeatTimer.isActive()) {
        return true;
    }
    for (const auto* effect : m_effects) {
        if (effect && effect->isPlaying()) {
            return true;
        }
    }
    return false;
}

void AlarmSoundManager::onRepeatTick()
{
    if (!m_enabled || m_remainingRepeats <= 0) {
        m_repeatTimer.stop();
        m_remainingRepeats = 0;
        return;
    }
    --m_remainingRepeats;
    playSingleInternal(m_selectedSound);
    if (m_remainingRepeats <= 0) {
        m_repeatTimer.stop();
    }
}
