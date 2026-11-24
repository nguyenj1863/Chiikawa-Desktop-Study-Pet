#include "OnboardingPreview.h"
#include "PixelTheme.h"

#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

double smoothStep(double edge0, double edge1, double x)
{
    if (edge1 <= edge0) {
        return x >= edge1 ? 1.0 : 0.0;
    }
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

QPointF lerpPoint(const QPointF& a, const QPointF& b, double t)
{
    const double u = std::clamp(t, 0.0, 1.0);
    return QPointF(a.x() + (b.x() - a.x()) * u, a.y() + (b.y() - a.y()) * u);
}

} // namespace

OnboardingPreview::OnboardingPreview(Mode mode, QWidget* parent)
    : QWidget(parent)
    , m_mode(mode)
    , m_reducedMotion(systemPrefersReducedMotion())
{
    PixelTheme::ensureFontsLoaded();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(mode == Mode::Drag);

    m_animTimer.setInterval(33); // ~30 fps, active only while step is visible
    connect(&m_animTimer, &QTimer::timeout, this, &OnboardingPreview::onTick);
}

OnboardingPreview::~OnboardingPreview()
{
    setActive(false);
}

bool OnboardingPreview::systemPrefersReducedMotion()
{
#ifdef Q_OS_WIN
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0)) {
        return animationsEnabled == FALSE;
    }
#endif
    return false;
}

QSize OnboardingPreview::sizeHint() const
{
    if (m_mode == Mode::Welcome || m_mode == Mode::Complete) {
        return {440, 156};
    }
    return {440, 224};
}

QSize OnboardingPreview::minimumSizeHint() const
{
    if (m_mode == Mode::Welcome || m_mode == Mode::Complete) {
        return {280, 136};
    }
    return {280, 204};
}

void OnboardingPreview::ensureMoviesLoaded()
{
    if (m_moviesLoaded) {
        return;
    }
    m_moviesLoaded = true;

    const QString primaryPath = (m_mode == Mode::Complete)
        ? QStringLiteral(":/assets/chiikawa_twirl.gif")
        : QStringLiteral(":/assets/chiikawa_skip.gif");

    m_primaryMovie = new QMovie(primaryPath, QByteArray(), this);
    m_primaryMovie->setCacheMode(QMovie::CacheAll);
    connect(m_primaryMovie, &QMovie::frameChanged, this, [this](int) {
        if (m_active) {
            update();
        }
    });

    if (m_mode == Mode::Interact) {
        m_altMovie = new QMovie(QStringLiteral(":/assets/chiikawa_twirl.gif"), QByteArray(), this);
        m_altMovie->setCacheMode(QMovie::CacheAll);
        connect(m_altMovie, &QMovie::frameChanged, this, [this](int) {
            if (m_active) {
                update();
            }
        });
    }
}

void OnboardingPreview::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (m_active) {
        ensureMoviesLoaded();
        m_clock.start();
        m_elapsedMs = 0;
        m_userDragging = false;
        m_hasUserPos = false;
        if (!m_reducedMotion && m_mode != Mode::Welcome) {
            m_animTimer.start();
        }
        updateMoviePlayback();
        update();
    } else {
        m_animTimer.stop();
        m_userDragging = false;
        if (m_primaryMovie) {
            m_primaryMovie->stop();
        }
        if (m_altMovie) {
            m_altMovie->stop();
        }
    }
}

void OnboardingPreview::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    if (m_active) {
        if (m_reducedMotion) {
            m_animTimer.stop();
        } else if (m_mode != Mode::Welcome) {
            m_clock.start();
            m_animTimer.start();
        }
        updateMoviePlayback();
        update();
    }
}

void OnboardingPreview::updateMoviePlayback()
{
    auto syncMovie = [this](QMovie* movie) {
        if (!movie || !movie->isValid()) {
            return;
        }
        if (!m_active) {
            movie->stop();
            return;
        }
        if (m_reducedMotion) {
            movie->jumpToFrame(0);
            movie->stop();
        } else if (movie->state() != QMovie::Running) {
            movie->start();
        }
    };
    syncMovie(m_primaryMovie);
    syncMovie(m_altMovie);
}

void OnboardingPreview::hideEvent(QHideEvent* event)
{
    setActive(false);
    QWidget::hideEvent(event);
}

void OnboardingPreview::onTick()
{
    if (!m_active || m_reducedMotion) {
        return;
    }
    m_elapsedMs = m_clock.elapsed();
    if (m_hasUserPos && !m_userDragging && (m_elapsedMs - m_userDragReleaseMs) > 1800) {
        m_hasUserPos = false;
    }
    update();
}

double OnboardingPreview::cycleDurationMs() const
{
    switch (m_mode) {
    case Mode::Interact:   return 12000.0; // Slower 12s instructional loop with clear pauses
    case Mode::Drag:       return 7000.0;  // Clear 7s approach, grab, drag, settle loop
    case Mode::StudyTimer: return 9000.0;  // 9s real timer card + hover bubble sequence
    case Mode::Alarm:      return 10000.0; // 10s one-time alarm + notification snooze sequence
    case Mode::Complete:   return 3600.0;
    case Mode::Welcome:    return 3600.0;
    }
    return 8000.0;
}

double OnboardingPreview::cycleProgress() const
{
    if (m_reducedMotion) {
        switch (m_mode) {
        case Mode::Interact:   return 0.72;
        case Mode::Drag:       return 0.52;
        case Mode::StudyTimer: return 0.72;
        case Mode::Alarm:      return 0.74;
        case Mode::Complete:   return 0.50;
        case Mode::Welcome:    return 0.0;
        }
    }
    const double dur = cycleDurationMs();
    return std::fmod(static_cast<double>(m_elapsedMs), dur) / dur;
}

QRect OnboardingPreview::stageRect() const
{
    return rect().adjusted(2, 2, -2, -2);
}

QPointF OnboardingPreview::clampDragPos(const QPointF& pos) const
{
    const QRect s = stageRect();
    constexpr double padX = 52.0;
    constexpr double padY = 52.0;
    return QPointF(
        std::clamp(pos.x(), s.left() + padX, s.right() - padX),
        std::clamp(pos.y(), s.top() + padY, s.bottom() - padY));
}

void OnboardingPreview::mousePressEvent(QMouseEvent* event)
{
    if (m_mode == Mode::Drag && event->button() == Qt::LeftButton) {
        const QRect s = stageRect();
        if (s.contains(event->position().toPoint())) {
            m_userDragging = true;
            m_hasUserPos = true;
            m_userPetCenter = clampDragPos(event->position());
            m_dragOffset = QPointF(0, 0);
            setCursor(Qt::ClosedHandCursor);
            update();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void OnboardingPreview::mouseMoveEvent(QMouseEvent* event)
{
    if (m_mode == Mode::Drag) {
        if (m_userDragging) {
            m_userPetCenter = clampDragPos(event->position() - m_dragOffset);
            update();
            event->accept();
            return;
        }
        setCursor(stageRect().contains(event->position().toPoint())
            ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void OnboardingPreview::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_mode == Mode::Drag && m_userDragging && event->button() == Qt::LeftButton) {
        m_userDragging = false;
        m_userDragReleaseMs = m_clock.isValid() ? m_clock.elapsed() : m_elapsedMs;
        setCursor(Qt::OpenHandCursor);
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void OnboardingPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    ensureMoviesLoaded();

    QPainter p(this);
    const QRect stage = stageRect();
    paintStageBackground(p, stage);

    const double t = cycleProgress();
    switch (m_mode) {
    case Mode::Welcome:
        paintWelcome(p, stage);
        break;
    case Mode::Interact:
        paintInteract(p, stage, t);
        break;
    case Mode::Drag:
        paintDrag(p, stage, t);
        break;
    case Mode::StudyTimer:
        paintStudyTimer(p, stage, t);
        break;
    case Mode::Alarm:
        paintAlarm(p, stage, t);
        break;
    case Mode::Complete:
        paintComplete(p, stage, t);
        break;
    }
}

void OnboardingPreview::paintStageBackground(QPainter& p, const QRect& stage) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);

    // Warm pastel pixel-framed preview stage
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kCardBg);
    p.drawRoundedRect(stage, 6, 6);

    // Subtle pastel dot texture
    p.setPen(Qt::NoPen);
    for (int y = stage.top() + 16; y < stage.bottom() - 12; y += 20) {
        for (int x = stage.left() + 18; x < stage.right() - 14; x += 24) {
            p.fillRect(x, y, 2, 2, QColor(0xE8, 0xC7, 0xB7, 90));
        }
    }
    p.restore();
}

void OnboardingPreview::drawPetSprite(QPainter& p,
                                      const QPointF& center,
                                      int size,
                                      bool useAltMovie,
                                      qreal yBounce) const
{
    QMovie* movie = (useAltMovie && m_altMovie && m_altMovie->isValid())
        ? m_altMovie
        : m_primaryMovie;

    p.save();
    // Soft ground shadow beneath Chiikawa
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(101, 76, 67, 28));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawEllipse(QPointF(center.x(), center.y() + size * 0.42), size * 0.34, size * 0.10);

    const QRect target(qRound(center.x() - size * 0.5),
                       qRound(center.y() - size * 0.5 - yBounce),
                       size,
                       size);
    if (movie && movie->isValid()) {
        const QPixmap frame = movie->currentPixmap();
        if (!frame.isNull()) {
            const QPixmap scaled = frame.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPoint topLeft(target.center().x() - scaled.width() / 2,
                                 target.center().y() - scaled.height() / 2);
            p.drawPixmap(topLeft, scaled);
        }
    }
    p.restore();
}

void OnboardingPreview::drawPixelCursor(QPainter& p, const QPointF& tip, bool pressed) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale = pressed ? 1.18 : 1.32;
    p.translate(tip);
    p.scale(scale, scale);

    // Subtle drop shadow for crisp visibility against cream/peach surfaces
    QPolygonF arrow;
    arrow << QPointF(0, 0)
          << QPointF(0, 17)
          << QPointF(4.5, 13.5)
          << QPointF(7.5, 20)
          << QPointF(10.5, 18.5)
          << QPointF(7.5, 12)
          << QPointF(13, 12);

    p.save();
    p.translate(1.2, 1.5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(89, 62, 53, 55));
    p.drawPolygon(arrow);
    p.restore();

    p.setPen(QPen(QColor(0x59, 0x3E, 0x35), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(pressed ? PixelTheme::kHoverAccent : QColor(Qt::white));
    p.drawPolygon(arrow);
    p.restore();
}

void OnboardingPreview::drawClickRipple(QPainter& p,
                                        const QPointF& center,
                                        double progress,
                                        bool doubleRing) const
{
    if (progress <= 0.0 || progress >= 1.0) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const auto drawRing = [&](double u) {
        if (u <= 0.0 || u >= 1.0) {
            return;
        }
        const qreal radius = 6.0 + u * 28.0;
        QColor c = PixelTheme::kPrimaryAccent;
        c.setAlpha(qRound((1.0 - u) * 230.0));
        p.setPen(QPen(c, 2.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, radius, radius);
    };
    drawRing(progress);
    if (doubleRing) {
        drawRing(std::clamp((progress - 0.22) / 0.78, 0.0, 1.0));
    }
    p.restore();
}

void OnboardingPreview::drawPillBadge(QPainter& p,
                                      const QPointF& center,
                                      const QString& text,
                                      bool primary) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f(PixelTheme::bodyFontFamily());
    f.setPixelSize(14);
    f.setBold(true);
    p.setFont(f);

    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 22;
    const int h = 28;
    const QRectF r(center.x() - w * 0.5, center.y() - h * 0.5, w, h);

    p.setPen(QPen(primary ? QColor(0xDC, 0x89, 0x81) : PixelTheme::kBorder, 2.0));
    p.setBrush(primary ? PixelTheme::kPrimaryAccent : PixelTheme::kBackground);
    p.drawRoundedRect(r, 6, 6);

    p.setPen(primary ? QColor(Qt::white) : QColor(0x59, 0x3E, 0x35));
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
}

void OnboardingPreview::paintWelcome(QPainter& p, const QRect& stage) const
{
    const QPointF center(stage.center().x(), stage.center().y() + 2.0);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kBackground);
    p.drawEllipse(center, 58, 58);
    p.restore();

    drawPetSprite(p, center, 106, false);
}

void OnboardingPreview::paintInteract(QPainter& p, const QRect& stage, double t) const
{
    // 12-second calm instructional loop:
    // 0.00-0.09: Initial animation pause (~1.1s)
    // 0.09-0.17: Cursor approaches Chiikawa; "Single-click" label appears
    // 0.17-0.21: Brief pause before click
    // 0.21-0.25: Visible single click + ripple -> animation swaps to twirl + bounce
    // 0.25-0.44: New animation plays (~2.3s)
    // 0.44-0.50: Calm pause; cursor pulls back slightly
    // 0.50-0.58: "Double-click" label appears; cursor approaches + brief pause
    // 0.585-0.628: Realistic double-click (2 clicks ~160ms apart) + double ripple
    // 0.63-0.88: Real-styled circular gear + mini settings card stay visible (~3.0s)
    // 0.88-1.00: Smooth fade-out & reset

    const QPointF petCenter(stage.center().x() - 34.0, stage.center().y() + 18.0);
    const bool swappedAnim = (t >= 0.22 && t < 0.90);
    const double bouncePhase = std::clamp((t - 0.22) / 0.08, 0.0, 1.0);
    const qreal bounce = (t >= 0.22 && t < 0.30) ? std::sin(bouncePhase * 3.14159) * 10.0 : 0.0;

    drawPetSprite(p, petCenter, 96, swappedAnim, bounce);

    // Gear icon (matching PetWindow's actual circular gear) + mini settings card after double-click
    const double gearReveal = smoothStep(0.62, 0.67, t) * (1.0 - smoothStep(0.89, 0.95, t));
    if (gearReveal > 0.01) {
        p.save();
        p.setOpacity(gearReveal);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QPointF gearCenter(petCenter.x() + 40.0, petCenter.y() - 36.0);
        p.setPen(QPen(PixelTheme::kBorder, 2.0));
        p.setBrush(PixelTheme::kBackground);
        p.drawEllipse(gearCenter, 16, 16);

        QFont gf(QStringLiteral("Segoe UI Symbol"));
        gf.setPixelSize(16);
        p.setFont(gf);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(QRectF(gearCenter.x() - 16, gearCenter.y() - 16, 32, 32),
                   Qt::AlignCenter, QStringLiteral("⚙"));

        // Compact real-styled settings popup hint beside Chiikawa
        const QRectF miniPanel(petCenter.x() + 72.0, stage.center().y() - 34.0, 122.0, 102.0);
        p.setPen(QPen(PixelTheme::kBorder, 2));
        p.setBrush(PixelTheme::kBackground);
        p.drawRoundedRect(miniPanel, 6, 6);

        QFont sf(PixelTheme::bodyFontFamily());
        sf.setPixelSize(12);
        sf.setBold(true);
        p.setFont(sf);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(QRectF(miniPanel.left() + 10, miniPanel.top() + 8, 102, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Settings"));

        p.setPen(QPen(PixelTheme::kBorder, 1.5));
        p.setBrush(PixelTheme::kCardBg);
        p.drawRoundedRect(QRectF(miniPanel.left() + 10, miniPanel.top() + 28, 102, 28), 4, 4);

        QFont mono(QStringLiteral("Consolas"));
        mono.setPixelSize(14);
        mono.setBold(true);
        p.setFont(mono);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(QRectF(miniPanel.left() + 10, miniPanel.top() + 28, 102, 28),
                   Qt::AlignCenter, QStringLiteral("25:00"));

        p.setPen(Qt::NoPen);
        p.setBrush(PixelTheme::kPrimaryAccent);
        p.drawRoundedRect(QRectF(miniPanel.left() + 10, miniPanel.top() + 63, 46, 12), 3, 3);
        p.setBrush(PixelTheme::kSecondaryAccent);
        p.drawRoundedRect(QRectF(miniPanel.left() + 62, miniPanel.top() + 63, 50, 12), 3, 3);
        p.drawRoundedRect(QRectF(miniPanel.left() + 10, miniPanel.top() + 81, 102, 12), 3, 3);
        p.restore();
    }

    // Top demonstration phase badge (clear, well-spaced, 14px bold)
    const QPointF badgePos(stage.center().x(), stage.top() + 26.0);
    if (t >= 0.09 && t < 0.45) {
        const double alpha = smoothStep(0.09, 0.13, t) * (1.0 - smoothStep(0.41, 0.45, t));
        p.save();
        p.setOpacity(alpha);
        drawPillBadge(p, badgePos, QStringLiteral("Single-click"), false);
        p.restore();
    } else if (t >= 0.50 && t < 0.94) {
        const double alpha = smoothStep(0.50, 0.54, t) * (1.0 - smoothStep(0.89, 0.94, t));
        p.save();
        p.setOpacity(alpha);
        drawPillBadge(p, badgePos, QStringLiteral("Double-click"), true);
        p.restore();
    }

    // Cursor trajectory with deliberate pauses
    const QPointF restPos(petCenter.x() - 78.0, petCenter.y() + 36.0);
    const QPointF midPos(petCenter.x() - 36.0, petCenter.y() + 18.0);
    const QPointF clickPos(petCenter.x() + 6.0, petCenter.y() + 4.0);
    QPointF cursorPos = restPos;
    if (t < 0.09) {
        cursorPos = restPos;
    } else if (t < 0.17) {
        cursorPos = lerpPoint(restPos, clickPos, smoothStep(0.09, 0.17, t));
    } else if (t < 0.44) {
        cursorPos = clickPos;
    } else if (t < 0.50) {
        cursorPos = lerpPoint(clickPos, midPos, smoothStep(0.44, 0.50, t));
    } else if (t < 0.56) {
        cursorPos = lerpPoint(midPos, clickPos, smoothStep(0.50, 0.56, t));
    } else if (t < 0.90) {
        cursorPos = clickPos;
    } else {
        cursorPos = lerpPoint(clickPos, restPos, smoothStep(0.90, 0.98, t));
    }

    const bool singlePressed = (t >= 0.21 && t < 0.245);
    const bool doublePressed = (t >= 0.585 && t < 0.600) || (t >= 0.612 && t < 0.628);

    if (t >= 0.215 && t < 0.34) {
        drawClickRipple(p, clickPos, (t - 0.215) / 0.125, false);
    } else if (t >= 0.59 && t < 0.74) {
        drawClickRipple(p, clickPos, (t - 0.59) / 0.15, true);
    }

    drawPixelCursor(p, cursorPos, singlePressed || doublePressed);
}

void OnboardingPreview::paintDrag(QPainter& p, const QRect& stage, double t) const
{
    // Clean desktop stage with NO stray bottom bar
    const QPointF startPos(stage.left() + 96.0, stage.center().y() + 10.0);
    const QPointF endPos(stage.right() - 96.0, stage.center().y() - 8.0);

    // Subtle dotted arc path showing the drag movement
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(PixelTheme::kBorder, 2.2, Qt::DotLine));
    QPointF prev = startPos;
    for (int i = 1; i <= 16; ++i) {
        const double u = static_cast<double>(i) / 16.0;
        const QPointF pt(startPos.x() + (endPos.x() - startPos.x()) * u,
                         startPos.y() + (endPos.y() - startPos.y()) * u - std::sin(u * 3.14159) * 26.0);
        p.drawLine(prev, pt);
        prev = pt;
    }
    p.restore();

    if (m_hasUserPos) {
        if (m_userDragging) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(PixelTheme::kPrimaryAccent, 2.2, Qt::DashLine));
            p.setBrush(QColor(243, 166, 160, 26));
            p.drawEllipse(m_userPetCenter, 48, 48);
            p.restore();
        }
        drawPetSprite(p, m_userPetCenter, 92, false, m_userDragging ? 6.0 : 0.0);
        return;
    }

    // 7-second drag sequence:
    // 0.00-0.12: Chiikawa rests on left
    // 0.12-0.24: Cursor approaches Chiikawa
    // 0.24-0.30: Mouse-down grab ring appears
    // 0.30-0.68: Smooth drag across stage
    // 0.68-0.74: Release & settle
    // 0.74-0.90: Rests at new position
    // 0.90-1.00: Smooth fade/reset
    const double dragU = smoothStep(0.30, 0.68, t);
    const bool dragging = (t >= 0.24 && t <= 0.70);
    const double resetFade = (t > 0.92) ? (1.0 - smoothStep(0.92, 0.99, t)) : 1.0;

    const QPointF petPos(startPos.x() + (endPos.x() - startPos.x()) * dragU,
                         startPos.y() + (endPos.y() - startPos.y()) * dragU - std::sin(dragU * 3.14159) * 26.0);

    const QPointF cursorStart(startPos.x() - 48.0, startPos.y() + 34.0);
    QPointF cursorPos = petPos + QPointF(6.0, 4.0);
    if (t < 0.12) {
        cursorPos = cursorStart;
    } else if (t < 0.24) {
        cursorPos = lerpPoint(cursorStart, petPos + QPointF(6.0, 4.0), smoothStep(0.12, 0.24, t));
    } else if (t > 0.74) {
        cursorPos = lerpPoint(endPos + QPointF(6.0, 4.0), endPos + QPointF(34.0, 26.0), smoothStep(0.74, 0.86, t));
    }

    p.save();
    p.setOpacity(resetFade);
    if (dragging) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(PixelTheme::kPrimaryAccent, 2.2, Qt::DashLine));
        p.setBrush(QColor(243, 166, 160, 26));
        p.drawEllipse(petPos, 48, 48);
    }
    drawPetSprite(p, petPos, 92, false, dragging ? 6.0 : 0.0);
    drawPixelCursor(p, cursorPos, dragging);
    p.restore();
}

void OnboardingPreview::paintStudyTimer(QPainter& p, const QRect& stage, double t) const
{
    // Matches the real Study Timer card in SettingsPanel + real hover TimerBubble
    // 0.00-0.14: Approach 25m preset chip
    // 0.14-0.24: Click 25m chip -> highlights coral, countdown shows 25:00
    // 0.24-0.40: Move to Play icon button -> click Play -> switches to Pause, countdown ticks
    // 0.40-0.58: Move cursor over Chiikawa
    // 0.58-0.90: Hover countdown bubble ("24:58 remaining") stays clearly visible
    // 0.90-1.00: Smooth reset

    const QRectF card(stage.left() + 18.0, stage.top() + 16.0, 214.0, stage.height() - 32.0);
    const QPointF petCenter(stage.right() - 88.0, stage.center().y() + 26.0);

    const bool presetSelected = (t >= 0.16);
    const bool timerRunning = (t >= 0.39);

    QString countdownText = QStringLiteral("45:00");
    if (presetSelected && !timerRunning) {
        countdownText = QStringLiteral("25:00");
    } else if (timerRunning) {
        if (t < 0.50) {
            countdownText = QStringLiteral("24:59");
        } else {
            countdownText = QStringLiteral("24:58");
        }
    }

    // Real Study Timer card styling (#FFF0E6 fill on #FFF9F2 inset, 2px #DDBFB3 border)
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kBackground);
    p.drawRoundedRect(card, 6, 6);

    // Header row: "Study Timer" + real PixelIcon::Edit button
    QFont headerFont(PixelTheme::bodyFontFamily());
    headerFont.setPixelSize(13);
    headerFont.setBold(true);
    p.setFont(headerFont);
    p.setPen(QColor(0x59, 0x3E, 0x35));
    p.drawText(QRectF(card.left() + 12, card.top() + 9, card.width() - 44, 20),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Study Timer"));

    const QRectF editBtn(card.right() - 30, card.top() + 8, 22, 22);
    p.setPen(QPen(PixelTheme::kBorder, 1.5));
    p.setBrush(PixelTheme::kCardBg);
    p.drawRoundedRect(editBtn, 4, 4);
    const QPixmap editPm = PixelTheme::icon(PixelIcon::Edit, PixelTheme::kPrimaryText, 11).pixmap(11, 11);
    p.drawPixmap(QPointF(editBtn.center().x() - 5.5, editBtn.center().y() - 5.5), editPm);

    // Prominent central monospace countdown (matching QPushButton#countdownDisplay)
    QFont monoFont(QStringLiteral("Cascadia Mono"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPixelSize(28);
    monoFont.setBold(true);
    p.setFont(monoFont);
    p.setPen(QColor(0x59, 0x3E, 0x35));
    p.drawText(QRectF(card.left() + 8, card.top() + 34, card.width() - 16, 38),
               Qt::AlignCenter, countdownText);

    // Preset chips row (25m / 45m / 60m)
    const double chipY = card.top() + 80.0;
    const QRectF chip25(card.left() + 12, chipY, 58, 28);
    const QRectF chip45(chip25.right() + 8, chipY, 58, 28);
    const QRectF chip60(chip45.right() + 8, chipY, 58, 28);

    QFont chipFont(PixelTheme::bodyFontFamily());
    chipFont.setPixelSize(13);
    chipFont.setBold(true);
    p.setFont(chipFont);

    const auto drawChip = [&](const QRectF& r, const QString& label, bool active) {
        p.setPen(QPen(active ? QColor(0xDC, 0x89, 0x81) : PixelTheme::kBorder, 2));
        p.setBrush(active ? PixelTheme::kPrimaryAccent : PixelTheme::kCardBg);
        p.drawRoundedRect(r, 4, 4);
        p.setPen(active ? QColor(Qt::white) : QColor(0x59, 0x3E, 0x35));
        p.drawText(r, Qt::AlignCenter, label);
    };
    drawChip(chip25, QStringLiteral("25m"), presetSelected);
    drawChip(chip45, QStringLiteral("45m"), !presetSelected);
    drawChip(chip60, QStringLiteral("60m"), false);

    // Real icon controls row: Play/Pause (primary) + Reset (secondary)
    const double iconY = card.bottom() - 44.0;
    const QRectF playBtn(card.center().x() - 39, iconY, 34, 34);
    const QRectF resetBtn(card.center().x() + 5, iconY, 34, 34);

    p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 2));
    p.setBrush(PixelTheme::kPrimaryAccent);
    p.drawRoundedRect(playBtn, 4, 4);
    const QPixmap playPm = PixelTheme::icon(
        timerRunning ? PixelIcon::Pause : PixelIcon::Play, Qt::white, 15).pixmap(15, 15);
    p.drawPixmap(QPointF(playBtn.center().x() - 7.5, playBtn.center().y() - 7.5), playPm);

    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kCardBg);
    p.drawRoundedRect(resetBtn, 4, 4);
    const QPixmap resetPm = PixelTheme::icon(PixelIcon::Reset, PixelTheme::kPrimaryText, 15).pixmap(15, 15);
    p.drawPixmap(QPointF(resetBtn.center().x() - 7.5, resetBtn.center().y() - 7.5), resetPm);
    p.restore();

    // Right side: Chiikawa + real-styled hover countdown bubble
    drawPetSprite(p, petCenter, 88, false);

    const double bubbleAlpha = smoothStep(0.58, 0.64, t) * (1.0 - smoothStep(0.90, 0.96, t));
    if (bubbleAlpha > 0.01) {
        p.save();
        p.setOpacity(bubbleAlpha);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bubble(petCenter.x() - 74.0, stage.top() + 26.0, 148.0, 34.0);
        p.setPen(QPen(PixelTheme::kBorder, 2));
        p.setBrush(PixelTheme::kBackground);
        p.drawRoundedRect(bubble, 7, 7);

        QFont bf(PixelTheme::bodyFontFamily());
        bf.setPixelSize(13);
        bf.setBold(true);
        p.setFont(bf);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(bubble, Qt::AlignCenter, QStringLiteral("%1 remaining").arg(countdownText));
        p.restore();
    }

    // Cursor animation: chip25 -> Play icon button -> hover Chiikawa
    const QPointF pStart(card.left() + 12, card.bottom() - 12);
    const QPointF pChip = chip25.center();
    const QPointF pPlay = playBtn.center();
    const QPointF pHover = petCenter + QPointF(6.0, 4.0);

    QPointF cursorPos = pChip;
    if (t < 0.13) {
        cursorPos = lerpPoint(pStart, pChip, smoothStep(0.0, 0.13, t));
    } else if (t < 0.24) {
        cursorPos = pChip;
    } else if (t < 0.36) {
        cursorPos = lerpPoint(pChip, pPlay, smoothStep(0.24, 0.36, t));
    } else if (t < 0.45) {
        cursorPos = pPlay;
    } else if (t < 0.58) {
        cursorPos = lerpPoint(pPlay, pHover, smoothStep(0.45, 0.58, t));
    } else {
        cursorPos = pHover;
    }

    const bool clickingChip = (t >= 0.15 && t < 0.19);
    const bool clickingPlay = (t >= 0.38 && t < 0.42);
    if (t >= 0.16 && t < 0.26) {
        drawClickRipple(p, pChip, (t - 0.16) / 0.10, false);
    } else if (t >= 0.39 && t < 0.49) {
        drawClickRipple(p, pPlay, (t - 0.39) / 0.10, false);
    }

    drawPixelCursor(p, cursorPos, clickingChip || clickingPlay);
}

void OnboardingPreview::paintAlarm(QPainter& p, const QRect& stage, double t) const
{
    // 10-second sequence matching the real one-time Alarm settings + AlarmNotification:
    // Phase A (0.00 - 0.46): Real one-time Alarm settings card (12h/24h, separate Hour:Min AM/PM, Save Alarm)
    // Phase B (0.46 - 0.96): Real AlarmNotification popup (Bell + Time to go! + Snooze 5m / Dismiss -> Snoozed for 5 minutes.)

    const double phaseAAlpha = 1.0 - smoothStep(0.44, 0.50, t);
    const double phaseBAlpha = smoothStep(0.48, 0.54, t) * (1.0 - smoothStep(0.93, 0.98, t));

    if (phaseAAlpha > 0.01) {
        p.save();
        p.setOpacity(phaseAAlpha);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF card(stage.left() + 28.0, stage.top() + 20.0, stage.width() - 56.0, stage.height() - 40.0);
        p.setPen(QPen(PixelTheme::kBorder, 2));
        p.setBrush(PixelTheme::kBackground);
        p.drawRoundedRect(card, 6, 6);

        // Row 1: Bell + "Alarm Time" + [12h] [24h] format toggle
        const QPixmap bellPm = PixelTheme::icon(PixelIcon::Bell, PixelTheme::kPrimaryAccent, 15).pixmap(15, 15);
        p.drawPixmap(QPointF(card.left() + 14, card.top() + 15), bellPm);

        QFont secFont(PixelTheme::bodyFontFamily());
        secFont.setPixelSize(14);
        secFont.setBold(true);
        p.setFont(secFont);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(QRectF(card.left() + 34, card.top() + 12, 140, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Alarm Time"));

        const QRectF btn12(card.right() - 102, card.top() + 11, 42, 24);
        const QRectF btn24(card.right() - 54, card.top() + 11, 42, 24);
        QFont chipFont(PixelTheme::bodyFontFamily());
        chipFont.setPixelSize(12);
        chipFont.setBold(true);
        p.setFont(chipFont);

        p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 1.8));
        p.setBrush(PixelTheme::kPrimaryAccent);
        p.drawRoundedRect(btn12, 4, 4);
        p.setPen(Qt::white);
        p.drawText(btn12, Qt::AlignCenter, QStringLiteral("12h"));

        p.setPen(QPen(PixelTheme::kBorder, 1.8));
        p.setBrush(PixelTheme::kCardBg);
        p.drawRoundedRect(btn24, 4, 4);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(btn24, Qt::AlignCenter, QStringLiteral("24h"));

        // Row 2: Separate Hour : Minute [AM/PM] dropdowns
        const double rowY = card.top() + 50.0;
        const QRectF hourBox(card.left() + 14, rowY, 96, 36);
        const QRectF minBox(hourBox.right() + 20, rowY, 96, 36);
        const QRectF periodBox(minBox.right() + 10, rowY, 96, 36);

        const bool hourActive = (t >= 0.06 && t < 0.16);
        const bool minActive = (t >= 0.16 && t < 0.25);
        const bool periodActive = (t >= 0.25 && t < 0.33);
        const bool saved = (t >= 0.36);

        const auto drawCombo = [&](const QRectF& r, const QString& val, bool focused) {
            p.setPen(QPen(focused ? PixelTheme::kPrimaryAccent : PixelTheme::kBorder, 2));
            p.setBrush(focused ? QColor(Qt::white) : PixelTheme::kBackground);
            p.drawRoundedRect(r, 4, 4);

            // Drop-down button strip on the right matching QComboBox#timeSelect
            const QRectF dd(r.right() - 22, r.top() + 2, 20, r.height() - 4);
            p.setPen(Qt::NoPen);
            p.setBrush(focused ? PixelTheme::kHoverAccent : PixelTheme::kCardBg);
            p.drawRect(dd);
            p.setPen(QPen(PixelTheme::kSecondaryAccent, 1.5));
            p.drawLine(QPointF(dd.left(), r.top() + 2), QPointF(dd.left(), r.bottom() - 2));

            // Down arrow triangle
            QPolygonF tri;
            tri << QPointF(dd.center().x() - 4, dd.center().y() - 2)
                << QPointF(dd.center().x() + 4, dd.center().y() - 2)
                << QPointF(dd.center().x(), dd.center().y() + 3);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x59, 0x3E, 0x35));
            p.drawPolygon(tri);

            QFont cf(QStringLiteral("Consolas"));
            cf.setPixelSize(15);
            cf.setBold(true);
            p.setFont(cf);
            p.setPen(QColor(0x59, 0x3E, 0x35));
            p.drawText(QRectF(r.left() + 4, r.top(), r.width() - 26, r.height()),
                       Qt::AlignCenter, val);
        };

        drawCombo(hourBox, (t >= 0.11) ? QStringLiteral("05") : QStringLiteral("04"), hourActive);
        QFont colonFont(QStringLiteral("Consolas"));
        colonFont.setPixelSize(18);
        colonFont.setBold(true);
        p.setFont(colonFont);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(QRectF(hourBox.right(), rowY, 20, 36), Qt::AlignCenter, QStringLiteral(":"));

        drawCombo(minBox, (t >= 0.20) ? QStringLiteral("30") : QStringLiteral("00"), minActive);
        drawCombo(periodBox, (t >= 0.29) ? QStringLiteral("PM") : QStringLiteral("AM"), periodActive);

        // Row 3: One-time schedule status + Save Alarm button
        const double actY = card.bottom() - 48.0;
        const QRectF saveBtn(card.right() - 126, actY, 112, 34);

        QFont statusFont(PixelTheme::bodyFontFamily());
        statusFont.setPixelSize(13);
        statusFont.setBold(true);
        p.setFont(statusFont);
        p.setPen(saved ? QColor(0x59, 0x3E, 0x35) : PixelTheme::kSecondaryText);
        p.drawText(QRectF(card.left() + 14, actY, saveBtn.left() - card.left() - 22, 34),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   saved ? QStringLiteral("Today at 5:30 PM") : QStringLiteral("Not scheduled"));

        p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 2));
        p.setBrush(PixelTheme::kPrimaryAccent);
        p.drawRoundedRect(saveBtn, 5, 5);
        p.setPen(Qt::white);
        p.drawText(saveBtn, Qt::AlignCenter, QStringLiteral("Save Alarm"));

        // Phase A cursor: Hour -> Minute -> AM/PM -> Save Alarm
        QPointF cursorPos = hourBox.center();
        if (t < 0.08) {
            cursorPos = lerpPoint(QPointF(card.left() + 8, card.bottom() - 8), hourBox.center(), smoothStep(0.0, 0.08, t));
        } else if (t < 0.16) {
            cursorPos = hourBox.center();
        } else if (t < 0.20) {
            cursorPos = lerpPoint(hourBox.center(), minBox.center(), smoothStep(0.16, 0.20, t));
        } else if (t < 0.25) {
            cursorPos = minBox.center();
        } else if (t < 0.29) {
            cursorPos = lerpPoint(minBox.center(), periodBox.center(), smoothStep(0.25, 0.29, t));
        } else if (t < 0.35) {
            cursorPos = lerpPoint(periodBox.center(), saveBtn.center(), smoothStep(0.29, 0.35, t));
        } else {
            cursorPos = saveBtn.center();
        }

        const bool clickingSave = (t >= 0.355 && t < 0.385);
        if (t >= 0.36 && t < 0.44) {
            drawClickRipple(p, saveBtn.center(), (t - 0.36) / 0.08, false);
        }
        drawPixelCursor(p, cursorPos, clickingSave);
        p.restore();
    }

    if (phaseBAlpha > 0.01) {
        p.save();
        p.setOpacity(phaseBAlpha);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool snoozed = (t >= 0.72);
        const QRect notifRect(stage.left() + 44, stage.top() + 26, stage.width() - 88, 134);
        PixelTheme::paintPixelCard(p, notifRect, PixelTheme::kBackground, PixelTheme::kBorder, 2);

        // Header: Bell + "Time to go!" + pixel Close button
        const QPixmap bellPm = PixelTheme::icon(PixelIcon::Bell, PixelTheme::kPrimaryAccent, 18).pixmap(18, 18);
        p.drawPixmap(QPointF(notifRect.left() + 18, notifRect.top() + 16), bellPm);

        QFont titleFont(PixelTheme::bodyFontFamily());
        titleFont.setPixelSize(16);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(QRectF(notifRect.left() + 42, notifRect.top() + 13, notifRect.width() - 84, 24),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Time to go!"));

        const QRectF closeRect(notifRect.right() - 40, notifRect.top() + 13, 24, 24);
        p.setPen(QPen(PixelTheme::kBorder, 1.6));
        p.setBrush(PixelTheme::kCardBg);
        p.drawRoundedRect(closeRect, 4, 4);
        const QPixmap closePm = PixelTheme::icon(PixelIcon::Close, PixelTheme::kPrimaryText, 11).pixmap(11, 11);
        p.drawPixmap(QPointF(closeRect.center().x() - 5.5, closeRect.center().y() - 5.5), closePm);

        // Body message
        QFont bodyFont(PixelTheme::bodyFontFamily());
        bodyFont.setPixelSize(14);
        p.setFont(bodyFont);
        p.setPen(PixelTheme::kSecondaryText);
        p.drawText(QRectF(notifRect.left() + 18, notifRect.top() + 45, notifRect.width() - 36, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Your 5:30 PM alarm is ringing!"));

        // Action row: [ Snooze 5m ] [ Dismiss ]
        const double btnW = (notifRect.width() - 48) * 0.5;
        const QRectF snoozeBtn(notifRect.left() + 18, notifRect.bottom() - 50, btnW, 34);
        const QRectF dismissBtn(snoozeBtn.right() + 10, notifRect.bottom() - 50, btnW, 34);

        QFont btnFont(PixelTheme::bodyFontFamily());
        btnFont.setPixelSize(14);
        btnFont.setBold(true);
        p.setFont(btnFont);

        p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 2));
        p.setBrush(snoozed ? PixelTheme::kHoverAccent : PixelTheme::kPrimaryAccent);
        p.drawRoundedRect(snoozeBtn, 5, 5);
        const QPixmap snoozePm = PixelTheme::icon(
            PixelIcon::Snooze, snoozed ? PixelTheme::kPrimaryText : QColor(Qt::white), 13).pixmap(13, 13);
        p.drawPixmap(QPointF(snoozeBtn.left() + 16, snoozeBtn.center().y() - 6.5), snoozePm);
        p.setPen(snoozed ? QColor(0x59, 0x3E, 0x35) : QColor(Qt::white));
        p.drawText(snoozeBtn.adjusted(14, 0, 0, 0), Qt::AlignCenter, QStringLiteral("Snooze 5m"));

        p.setPen(QPen(PixelTheme::kBorder, 2));
        p.setBrush(PixelTheme::kCardBg);
        p.drawRoundedRect(dismissBtn, 5, 5);
        p.setPen(QColor(0x59, 0x3E, 0x35));
        p.drawText(dismissBtn, Qt::AlignCenter, QStringLiteral("Dismiss"));

        // Brief snooze confirmation pill below notification
        if (snoozed) {
            drawPillBadge(p, QPointF(stage.center().x(), notifRect.bottom() + 24.0),
                          QStringLiteral("Snoozed for 5 minutes."), false);
        }

        // Cursor glides to Snooze 5m and clicks
        const QPointF pStart(notifRect.right() - 20, notifRect.bottom() + 12);
        const QPointF pSnooze = snoozeBtn.center();
        QPointF cursorPos = pSnooze;
        if (t < 0.68) {
            cursorPos = lerpPoint(pStart, pSnooze, smoothStep(0.54, 0.68, t));
        }

        const bool clickingSnooze = (t >= 0.70 && t < 0.735);
        if (t >= 0.71 && t < 0.82) {
            drawClickRipple(p, pSnooze, (t - 0.71) / 0.11, false);
        }
        drawPixelCursor(p, cursorPos, clickingSnooze);
        p.restore();
    }
}

void OnboardingPreview::paintComplete(QPainter& p, const QRect& stage, double t) const
{
    const QPointF center(stage.center().x(), stage.center().y() + 2.0);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kBackground);
    p.drawEllipse(center, 58, 58);

    // Subtle pastel pixel sparkles around Chiikawa
    const auto drawSparkle = [&](double ox, double oy, double phase) {
        const double pulse = m_reducedMotion ? 0.85 : (0.45 + 0.55 * std::sin((t + phase) * 6.28318));
        QColor c = PixelTheme::kPrimaryAccent;
        c.setAlpha(qRound(pulse * 225.0));
        const int sx = qRound(center.x() + ox);
        const int sy = qRound(center.y() + oy);
        p.fillRect(sx - 1, sy - 5, 2, 10, c);
        p.fillRect(sx - 5, sy - 1, 10, 2, c);
    };
    drawSparkle(-74, -26, 0.0);
    drawSparkle(72, -30, 0.33);
    drawSparkle(-62, 30, 0.66);
    drawSparkle(66, 26, 0.18);
    p.restore();

    drawPetSprite(p, center, 106, false);
}
