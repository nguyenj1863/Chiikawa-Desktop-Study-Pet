#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <random>

class QLabel;
class QMovie;

enum class AnimationId {
    Skip = 0,
    Stomp,
    Twirl,
    Fire,
    Count
};

/**
 * Loads and plays Chiikawa GIF animations via QMovie.
 * Preserves frame timing and transparency; caches movies to avoid reloads.
 * Horizontal mirroring is applied per-frame without reloading assets.
 */
class AnimationManager : public QObject {
    Q_OBJECT

public:
    explicit AnimationManager(QLabel* displayLabel, QObject* parent = nullptr);
    ~AnimationManager() override;

    [[nodiscard]] bool loadAll();
    [[nodiscard]] bool isReady() const { return m_ready; }

    void play(AnimationId id, bool mirrored = false);
    void playRandom(bool avoidCurrent = true);
    void playCelebration();

    void setMirrored(bool mirrored);
    [[nodiscard]] bool isMirrored() const { return m_mirrored; }

    void setAutoSwitchEnabled(bool enabled);
    [[nodiscard]] bool autoSwitchEnabled() const { return m_autoSwitchEnabled; }

    void setTargetSize(int size);
    [[nodiscard]] int targetSize() const { return m_targetSize; }

    [[nodiscard]] AnimationId currentAnimation() const { return m_current; }

    [[nodiscard]] static QString resourcePath(AnimationId id);
    [[nodiscard]] static QString displayName(AnimationId id);

signals:
    void animationChanged(AnimationId id);
    void frameUpdated(const QPixmap& frame, AnimationId id);

private slots:
    void onAutoSwitchTimeout();
    void onFrameChanged();

private:
    void scheduleNextAutoSwitch();
    void updateDisplayedFrame();
    void clearFrameCacheIfNeeded(int physSize, qreal dpr);
    [[nodiscard]] AnimationId pickRandom(bool avoidCurrent);
    [[nodiscard]] QMovie* currentMovie() const;

    QPointer<QLabel> m_label;
    QHash<int, QMovie*> m_movies;
    QHash<quint64, QPixmap> m_frameCache;
    int m_cachedPhysSize = 0;
    qreal m_cachedDpr = 1.0;
    AnimationId m_current = AnimationId::Skip;
    bool m_mirrored = false;
    bool m_ready = false;
    bool m_autoSwitchEnabled = true;
    int m_targetSize = 120;
    QTimer m_autoSwitchTimer;
    std::mt19937 m_rng;
};
