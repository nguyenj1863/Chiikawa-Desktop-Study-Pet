#pragma once

#include <QElapsedTimer>
#include <QMovie>
#include <QPointF>
#include <QTimer>
#include <QWidget>

/**
 * Lightweight animated demonstration stage for OnboardingWindow.
 * Renders warm pastel pixel-art visual demos using cached QMovies and Qt painting,
 * pausing all timers and GIF decoding whenever the step is inactive.
 */
class OnboardingPreview : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        Welcome,
        Interact,
        Drag,
        StudyTimer,
        Alarm,
        Complete
    };

    explicit OnboardingPreview(Mode mode, QWidget* parent = nullptr);
    ~OnboardingPreview() override;

    [[nodiscard]] Mode mode() const { return m_mode; }
    [[nodiscard]] bool isActive() const { return m_active; }
    [[nodiscard]] bool isReducedMotion() const { return m_reducedMotion; }
    [[nodiscard]] bool isAnimationTimerRunning() const { return m_animTimer.isActive(); }

    /** Activates or deactivates animations and GIF playback for this preview. */
    void setActive(bool active);

    /** Enables or disables reduced-motion static presentation. */
    void setReducedMotion(bool reduced);

    /** Queries the OS reduced-motion preference (false if animations are enabled). */
    [[nodiscard]] static bool systemPrefersReducedMotion();

    [[nodiscard]] double cycleDurationMs() const;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onTick();

private:
    void ensureMoviesLoaded();
    void updateMoviePlayback();
    [[nodiscard]] double cycleProgress() const;
    [[nodiscard]] QRect stageRect() const;
    [[nodiscard]] QPointF clampDragPos(const QPointF& pos) const;

    void paintStageBackground(QPainter& p, const QRect& stage) const;
    void paintWelcome(QPainter& p, const QRect& stage) const;
    void paintInteract(QPainter& p, const QRect& stage, double t) const;
    void paintDrag(QPainter& p, const QRect& stage, double t) const;
    void paintStudyTimer(QPainter& p, const QRect& stage, double t) const;
    void paintAlarm(QPainter& p, const QRect& stage, double t) const;
    void paintComplete(QPainter& p, const QRect& stage, double t) const;

    void drawPetSprite(QPainter& p, const QPointF& center, int size, bool useAltMovie, qreal yBounce = 0.0) const;
    void drawPixelCursor(QPainter& p, const QPointF& tip, bool pressed) const;
    void drawClickRipple(QPainter& p, const QPointF& center, double progress, bool doubleRing) const;
    void drawPillBadge(QPainter& p, const QPointF& center, const QString& text, bool primary = false) const;

    Mode m_mode;
    bool m_active = false;
    bool m_reducedMotion = false;
    bool m_moviesLoaded = false;

    QMovie* m_primaryMovie = nullptr;
    QMovie* m_altMovie = nullptr;

    QTimer m_animTimer;
    QElapsedTimer m_clock;
    qint64 m_elapsedMs = 0;

    // Interactive drag state for Mode::Drag (strictly confined to preview stage)
    bool m_userDragging = false;
    bool m_hasUserPos = false;
    QPointF m_userPetCenter;
    QPointF m_dragOffset;
    qint64 m_userDragReleaseMs = -10000;
};
