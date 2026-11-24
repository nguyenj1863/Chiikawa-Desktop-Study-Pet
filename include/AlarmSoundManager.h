#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

class QSoundEffect;

/**
 * Non-blocking kawaii alarm and completion sound playback via Qt Multimedia (QSoundEffect).
 * Prevents overlapping playback and bounds alarm repeats so audio never plays indefinitely.
 */
class AlarmSoundManager : public QObject {
    Q_OBJECT

public:
    struct SoundOption {
        QString id;
        QString label;
        QString resourceUrl;
    };

    explicit AlarmSoundManager(QObject* parent = nullptr);
    ~AlarmSoundManager() override;

    [[nodiscard]] static QList<SoundOption> availableSounds();
    [[nodiscard]] static QString resourceUrlFor(const QString& soundId);
    [[nodiscard]] static QString displayNameFor(const QString& soundId);

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    void setVolume(double volume);
    [[nodiscard]] double volume() const { return m_volume; }

    void setSelectedSound(const QString& soundId);
    [[nodiscard]] QString selectedSound() const { return m_selectedSound; }

    /** Plays a single gentle completion chime (e.g. study timer finished). */
    void playOnce();

    /** Plays a bounded sequence of gentle chimes for clock alarms (stops automatically). */
    void playAlarmSequence();

    /** Previews the specified sound once without triggering any alarm event. */
    void preview(const QString& soundId = {});

    /** Immediately stops any playing audio and cancels pending sequence repeats. */
    void stop();

    [[nodiscard]] bool isPlaying() const;

signals:
    void playbackStarted(const QString& soundId);
    void playbackStopped();

private slots:
    void onRepeatTick();

private:
    QSoundEffect* effectFor(const QString& soundId);
    void playSingleInternal(const QString& soundId);

    QHash<QString, QSoundEffect*> m_effects;
    QString m_selectedSound = QStringLiteral("soft_chime");
    QString m_activeSoundId;
    bool m_enabled = true;
    double m_volume = 0.25;

    QTimer m_repeatTimer;
    int m_remainingRepeats = 0;
    static constexpr int kAlarmRepeatCount = 3;
    static constexpr int kAlarmRepeatIntervalMs = 2400;
};
