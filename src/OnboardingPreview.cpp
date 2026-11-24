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
        return {380, 136};
    }
    return {380, 192};
}

QSize OnboardingPreview::minimumSizeHint() const
{
    if (m_mode == Mode::Welcome || m_mode == Mode::Complete) {
        return {260, 120};
    }
    return {260, 168};
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
    if (m_hasUserPos && !m_userDragging && (m_elapsedMs - m_userDragReleaseMs) > 1600) {
        m_hasUserPos = false;
    }
    update();
}

double OnboardingPreview::cycleDurationMs() const
{
    switch (m_mode) {
    case Mode::Interact:   return 4800.0;
    case Mode::Drag:       return 4200.0;
    case Mode::StudyTimer: return 5000.0;
    case Mode::Alarm:      return 5000.0;
    case Mode::Complete:   return 3000.0;
    case Mode::Welcome:    return 3000.0;
    }
    return 4000.0;
}

double OnboardingPreview::cycleProgress() const
{
    if (m_reducedMotion) {
        switch (m_mode) {
        case Mode::Interact:   return 0.68;
        case Mode::Drag:       return 0.55;
        case Mode::StudyTimer: return 0.78;
        case Mode::Alarm:      return 0.68;
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
    constexpr double padX = 44.0;
    constexpr double padTop = 44.0;
    constexpr double padBottom = 52.0;
    return QPointF(
        std::clamp(pos.x(), s.left() + padX, s.right() - padX),
        std::clamp(pos.y(), s.top() + padTop, s.bottom() - padBottom));
}

void OnboardingPreview::mousePressEvent(QMouseEvent* event)
{
    if (m_mode == Mode::Drag && event->button() == Qt::LeftButton) {
        const QRect s = stageRect();
        const QPointF currentCenter = m_hasUserPos
            ? m_userPetCenter
            : QPointF(s.center().x(), s.center().y() - 6.0);
        const QRectF hit(currentCenter.x() - 42.0, currentCenter.y() - 42.0, 84.0, 84.0);
        if (hit.contains(event->position()) || s.contains(event->position().toPoint())) {
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

    // Warm pastel recessed pixel card
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kCardBg);
    p.drawRoundedRect(stage, 6, 6);

    // Subtle pastel dot pattern inside stage
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xE8, 0xC7, 0xB7, 90));
    for (int y = stage.top() + 14; y < stage.bottom() - 10; y += 18) {
        for (int x = stage.left() + 16; x < stage.right() - 12; x += 22) {
            p.fillRect(x, y, 2, 2, QColor(0xE8, 0xC7, 0xB7, 95));
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
    p.drawEllipse(QPointF(center.x(), center.y() + size * 0.42), size * 0.32, size * 0.10);

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
    const qreal scale = pressed ? 0.90 : 1.0;
    p.translate(tip);
    p.scale(scale, scale);

    QPolygonF arrow;
    arrow << QPointF(0, 0)
          << QPointF(0, 17)
          << QPointF(4.5, 13.5)
          << QPointF(7.5, 20)
          << QPointF(10.5, 18.5)
          << QPointF(7.5, 12)
          << QPointF(13, 12);

    p.setPen(QPen(PixelTheme::kPrimaryText, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
        const qreal radius = 5.0 + u * 22.0;
        QColor c = PixelTheme::kPrimaryAccent;
        c.setAlpha(qRound((1.0 - u) * 210.0));
        p.setPen(QPen(c, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, radius, radius);
    };
    drawRing(progress);
    if (doubleRing) {
        drawRing(std::clamp((progress - 0.24) / 0.76, 0.0, 1.0));
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
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);

    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 16;
    const int h = 22;
    const QRectF r(center.x() - w * 0.5, center.y() - h * 0.5, w, h);

    p.setPen(QPen(primary ? QColor(0xDC, 0x89, 0x81) : PixelTheme::kBorder, 1.8));
    p.setBrush(primary ? PixelTheme::kPrimaryAccent : PixelTheme::kBackground);
    p.drawRoundedRect(r, 5, 5);

    p.setPen(primary ? QColor(Qt::white) : PixelTheme::kPrimaryText);
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
}

void OnboardingPreview::paintWelcome(QPainter& p, const QRect& stage) const
{
    const QPointF center(stage.center().x(), stage.center().y() + 2.0);
    // Soft circular spotlight behind Chiikawa
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(PixelTheme::kBackground);
    p.drawEllipse(center, 48, 48);
    p.restore();

    drawPetSprite(p, center, 84, false);
}

void OnboardingPreview::paintInteract(QPainter& p, const QRect& stage, double t) const
{
    const QPointF petCenter(stage.center().x() - 18.0, stage.center().y() + 6.0);

    // Phase 1 (0.00 - 0.46): Move to pet, single click -> animation swap + bounce
    // Phase 2 (0.46 - 0.96): Double-click -> gear icon + mini settings hint appear
    const bool swappedAnim = (t >= 0.20 && t < 0.92);
    const double bouncePhase = std::clamp((t - 0.20) / 0.14, 0.0, 1.0);
    const qreal bounce = (t >= 0.20 && t < 0.34) ? std::sin(bouncePhase * 3.14159) * 8.0 : 0.0;

    drawPetSprite(p, petCenter, 78, swappedAnim, bounce);

    // Gear icon + mini settings card appear after double-click
    const double gearReveal = smoothStep(0.60, 0.70, t) * (1.0 - smoothStep(0.92, 0.98, t));
    if (gearReveal > 0.01) {
        p.save();
        p.setOpacity(gearReveal);
        const QPointF gearCenter(petCenter.x() + 30.0, petCenter.y() - 32.0);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(PixelTheme::kBorder, 1.8));
        p.setBrush(PixelTheme::kBackground);
        p.drawEllipse(gearCenter, 13, 13);

        QFont gf(QStringLiteral("Segoe UI Symbol"));
        gf.setPixelSize(13);
        p.setFont(gf);
        p.setPen(PixelTheme::kPrimaryText);
        p.drawText(QRectF(gearCenter.x() - 13, gearCenter.y() - 13, 26, 26),
                   Qt::AlignCenter, QStringLiteral("⚙"));

        // Compact mini settings preview card on the right
        const QRectF miniPanel(petCenter.x() + 56.0, stage.center().y() - 44.0, 96.0, 88.0);
        p.setPen(QPen(PixelTheme::kBorder, 2));
        p.setBrush(PixelTheme::kBackground);
        p.drawRoundedRect(miniPanel, 5, 5);

        QFont sf(QStringLiteral("Segoe UI"));
        sf.setPixelSize(9);
        sf.setBold(true);
        p.setFont(sf);
        p.setPen(PixelTheme::kPrimaryText);
        p.drawText(QRectF(miniPanel.left() + 8, miniPanel.top() + 7, 80, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Settings"));

        // Mini timer display bar
        p.setPen(QPen(PixelTheme::kBorder, 1.2));
        p.setBrush(PixelTheme::kCardBg);
        p.drawRoundedRect(QRectF(miniPanel.left() + 8, miniPanel.top() + 24, 80, 22), 3, 3);
        p.setPen(PixelTheme::kPrimaryText);
        p.drawText(QRectF(miniPanel.left() + 8, miniPanel.top() + 24, 80, 22),
                   Qt::AlignCenter, QStringLiteral("25:00"));

        // Two mini preset bars
        p.setPen(Qt::NoPen);
        p.setBrush(PixelTheme::kPrimaryAccent);
        p.drawRoundedRect(QRectF(miniPanel.left() + 8, miniPanel.top() + 52, 36, 10), 2, 2);
        p.setBrush(PixelTheme::kSecondaryAccent);
        p.drawRoundedRect(QRectF(miniPanel.left() + 50, miniPanel.top() + 52, 38, 10), 2, 2);
        p.drawRoundedRect(QRectF(miniPanel.left() + 8, miniPanel.top() + 68, 80, 10), 2, 2);
        p.restore();
    }

    // Cursor position
    const QPointF restPos(petCenter.x() - 62.0, petCenter.y() + 34.0);
    const QPointF clickPos(petCenter.x() + 4.0, petCenter.y() + 2.0);
    QPointF cursorPos = clickPos;
    if (t < 0.16) {
        cursorPos = lerpPoint(restPos, clickPos, smoothStep(0.0, 0.16, t));
    } else if (t > 0.92) {
        cursorPos = lerpPoint(clickPos, restPos, smoothStep(0.92, 1.0, t));
    }

    const bool singlePressed = (t >= 0.18 && t < 0.24);
    const bool doublePressed = (t >= 0.54 && t < 0.58) || (t >= 0.60 && t < 0.65);

    if (t >= 0.19 && t < 0.42) {
        drawClickRipple(p, clickPos, (t - 0.19) / 0.23, false);
        drawPillBadge(p, QPointF(petCenter.x(), stage.top() + 22.0), QStringLiteral("1x Click"), false);
    } else if (t >= 0.55 && t < 0.90) {
        if (t < 0.80) {
            drawClickRipple(p, clickPos, (t - 0.55) / 0.25, true);
        }
        drawPillBadge(p, QPointF(petCenter.x(), stage.top() + 22.0), QStringLiteral("2x Double-Click"), true);
    }

    drawPixelCursor(p, cursorPos, singlePressed || doublePressed);
}

void OnboardingPreview::paintDrag(QPainter& p, const QRect& stage, double t) const
{
    // Subtle mini desktop taskbar along bottom of stage
    const QRect taskbar(stage.left() + 10, stage.bottom() - 20, stage.width() - 20, 12);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xDD, 0xBF, 0xB3, 120));
    p.drawRoundedRect(taskbar, 3, 3);
    p.setBrush(PixelTheme::kBackground);
    p.drawRoundedRect(QRect(taskbar.left() + 6, taskbar.top() + 3, 14, 6), 2, 2);
    p.drawRoundedRect(QRect(taskbar.left() + 26, taskbar.top() + 3, 14, 6), 2, 2);
    p.restore();

    const QPointF startPos(stage.left() + 88.0, stage.center().y() + 4.0);
    const QPointF endPos(stage.right() - 88.0, stage.center().y() - 10.0);

    // Faint dotted arc path showing drag trajectory
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen trailPen(PixelTheme::kBorder, 2.0, Qt::DotLine);
    p.setPen(trailPen);
    QPointF prev = startPos;
    for (int i = 1; i <= 12; ++i) {
        const double u = static_cast<double>(i) / 12.0;
        const QPointF pt(startPos.x() + (endPos.x() - startPos.x()) * u,
                         startPos.y() + (endPos.y() - startPos.y()) * u - std::sin(u * 3.14159) * 20.0);
        p.drawLine(prev, pt);
        prev = pt;
    }
    p.restore();

    if (m_hasUserPos) {
        drawPetSprite(p, m_userPetCenter, 72, false, m_userDragging ? 4.0 : 0.0);
        drawPillBadge(p, QPointF(stage.center().x(), stage.top() + 22.0),
                      m_userDragging ? QStringLiteral("Moving!") : QStringLiteral("Try dragging me!"),
                      m_userDragging);
        return;
    }

    // Auto drag cycle: approach -> grab -> drag across -> release -> return
    const double dragU = smoothStep(0.22, 0.72, t);
    const bool dragging = (t >= 0.20 && t <= 0.74);
    const QPointF petPos(startPos.x() + (endPos.x() - startPos.x()) * dragU,
                         startPos.y() + (endPos.y() - startPos.y()) * dragU - std::sin(dragU * 3.14159) * 20.0);

    const QPointF cursorStart(startPos.x() - 36.0, startPos.y() + 28.0);
    QPointF cursorPos = petPos + QPointF(4.0, 2.0);
    if (t < 0.18) {
        cursorPos = lerpPoint(cursorStart, petPos + QPointF(4.0, 2.0), smoothStep(0.0, 0.18, t));
    } else if (t > 0.78) {
        cursorPos = lerpPoint(endPos + QPointF(4.0, 2.0), endPos + QPointF(26.0, 20.0), smoothStep(0.78, 0.92, t));
    }

    drawPetSprite(p, petPos, 72, false, dragging ? 5.0 : 0.0);
    drawPillBadge(p, QPointF(stage.center().x(), stage.top() + 22.0),
                  dragging ? QStringLiteral("Drag anywhere") : QStringLiteral("Click & drag"),
                  dragging);
    drawPixelCursor(p, cursorPos, dragging);
}

void OnboardingPreview::paintStudyTimer(QPainter& p, const QRect& stage, double t) const
{
    // Left: compact mini timer card; Right: Chiikawa + hover countdown bubble
    const QRectF card(stage.left() + 18.0, stage.top() + 24.0, 166.0, stage.height() - 48.0);
    const QPointF petCenter(stage.right() - 78.0, stage.center().y() + 18.0);

    const bool presetSelected = (t >= 0.16);
    const bool timerRunning = (t >= 0.44);
    const bool hoveringPet = (t >= 0.68 && t < 0.95);

    QString countdownText = QStringLiteral("00:00");
    if (presetSelected && !timerRunning) {
        countdownText = QStringLiteral("25:00");
    } else if (timerRunning) {
        if (t < 0.62) {
            countdownText = QStringLiteral("24:59");
        } else {
            countdownText = QStringLiteral("24:58");
        }
    }

    // Draw mini settings timer card
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kBackground);
    p.drawRoundedRect(card, 6, 6);

    QFont titleFont(QStringLiteral("Segoe UI"));
    titleFont.setPixelSize(10);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(PixelTheme::kSecondaryText);
    p.drawText(QRectF(card.left() + 10, card.top() + 8, card.width() - 20, 14),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("STUDY TIMER"));

    // Countdown readout
    QFont monoFont(QStringLiteral("Consolas"));
    monoFont.setPixelSize(22);
    monoFont.setBold(true);
    p.setFont(monoFont);
    p.setPen(PixelTheme::kPrimaryText);
    p.drawText(QRectF(card.left() + 8, card.top() + 24, card.width() - 16, 30),
               Qt::AlignCenter, countdownText);

    // Preset chips row (25m / 45m / 60m)
    const QRectF chip25(card.left() + 12, card.top() + 60, 42, 22);
    const QRectF chip45(card.left() + 62, card.top() + 60, 42, 22);
    const QRectF chip60(card.left() + 112, card.top() + 60, 42, 22);
    QFont chipFont(QStringLiteral("Segoe UI"));
    chipFont.setPixelSize(10);
    chipFont.setBold(true);
    p.setFont(chipFont);

    const auto drawChip = [&](const QRectF& r, const QString& label, bool active) {
        p.setPen(QPen(active ? QColor(0xDC, 0x89, 0x81) : PixelTheme::kBorder, 1.5));
        p.setBrush(active ? PixelTheme::kPrimaryAccent : PixelTheme::kCardBg);
        p.drawRoundedRect(r, 4, 4);
        p.setPen(active ? QColor(Qt::white) : PixelTheme::kPrimaryText);
        p.drawText(r, Qt::AlignCenter, label);
    };
    drawChip(chip25, QStringLiteral("25m"), presetSelected);
    drawChip(chip45, QStringLiteral("45m"), false);
    drawChip(chip60, QStringLiteral("60m"), false);

    // Play / Pause button
    const QRectF playBtn(card.center().x() - 26, card.bottom() - 36, 52, 24);
    p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 1.6));
    p.setBrush(PixelTheme::kPrimaryAccent);
    p.drawRoundedRect(playBtn, 4, 4);
    p.setPen(Qt::white);
    p.drawText(playBtn, Qt::AlignCenter, timerRunning ? QStringLiteral("Pause") : QStringLiteral("Play"));
    p.restore();

    // Draw Chiikawa on the right
    drawPetSprite(p, petCenter, 72, false);

    // Hover countdown bubble above Chiikawa
    const double bubbleAlpha = smoothStep(0.68, 0.75, t) * (1.0 - smoothStep(0.93, 0.98, t));
    if (bubbleAlpha > 0.01) {
        p.save();
        p.setOpacity(bubbleAlpha);
        const QRectF bubble(petCenter.x() - 56.0, petCenter.y() - 68.0, 112.0, 26.0);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(PixelTheme::kBorder, 1.8));
        p.setBrush(PixelTheme::kBackground);
        p.drawRoundedRect(bubble, 6, 6);

        QFont bf(QStringLiteral("Segoe UI"));
        bf.setPixelSize(10);
        bf.setBold(true);
        p.setFont(bf);
        p.setPen(PixelTheme::kPrimaryText);
        p.drawText(bubble, Qt::AlignCenter, QStringLiteral("%1 left").arg(countdownText));
        p.restore();
    }

    // Cursor animation: chip25 -> playBtn -> hover pet
    const QPointF p1 = chip25.center();
    const QPointF p2 = playBtn.center();
    const QPointF p3 = petCenter + QPointF(6.0, 2.0);
    QPointF cursorPos = p1;
    if (t < 0.14) {
        cursorPos = lerpPoint(QPointF(card.left() + 4, card.bottom() - 4), p1, smoothStep(0.0, 0.14, t));
    } else if (t < 0.26) {
        cursorPos = p1;
    } else if (t < 0.42) {
        cursorPos = lerpPoint(p1, p2, smoothStep(0.26, 0.42, t));
    } else if (t < 0.52) {
        cursorPos = p2;
    } else if (t < 0.68) {
        cursorPos = lerpPoint(p2, p3, smoothStep(0.52, 0.68, t));
    } else {
        cursorPos = p3;
    }

    const bool clickingChip = (t >= 0.15 && t < 0.21);
    const bool clickingPlay = (t >= 0.43 && t < 0.49);
    if (t >= 0.16 && t < 0.30) {
        drawClickRipple(p, p1, (t - 0.16) / 0.14, false);
    } else if (t >= 0.44 && t < 0.58) {
        drawClickRipple(p, p2, (t - 0.44) / 0.14, false);
    }
    Q_UNUSED(hoveringPet);
    drawPixelCursor(p, cursorPos, clickingChip || clickingPlay);
}

void OnboardingPreview::paintAlarm(QPainter& p, const QRect& stage, double t) const
{
    // Top: mini alarm selector strip
    const QRectF strip(stage.left() + 20.0, stage.top() + 18.0, stage.width() - 40.0, 50.0);
    const bool alarmSaved = (t >= 0.24);
    const double notifReveal = smoothStep(0.42, 0.52, t) * (1.0 - smoothStep(0.92, 0.98, t));
    const bool snoozed = (t >= 0.74);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(PixelTheme::kBorder, 2));
    p.setBrush(PixelTheme::kBackground);
    p.drawRoundedRect(strip, 6, 6);

    QFont mono(QStringLiteral("Consolas"));
    mono.setPixelSize(12);
    mono.setBold(true);
    p.setFont(mono);

    // Simulated Hour : Minute AM/PM pills
    const QRectF timeBox(strip.left() + 12, strip.top() + 12, 108, 26);
    p.setPen(QPen(PixelTheme::kBorder, 1.5));
    p.setBrush(PixelTheme::kCardBg);
    p.drawRoundedRect(timeBox, 4, 4);
    p.setPen(PixelTheme::kPrimaryText);
    p.drawText(timeBox, Qt::AlignCenter, QStringLiteral("05 : 30 PM"));

    // Save Alarm button
    const QRectF saveBtn(strip.right() - 94, strip.top() + 12, 82, 26);
    p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 1.5));
    p.setBrush(PixelTheme::kPrimaryAccent);
    p.drawRoundedRect(saveBtn, 4, 4);

    QFont btnFont(QStringLiteral("Segoe UI"));
    btnFont.setPixelSize(10);
    btnFont.setBold(true);
    p.setFont(btnFont);
    p.setPen(Qt::white);
    p.drawText(saveBtn, Qt::AlignCenter, alarmSaved ? QStringLiteral("Saved") : QStringLiteral("Save Alarm"));

    if (alarmSaved) {
        QFont smallFont(QStringLiteral("Segoe UI"));
        smallFont.setPixelSize(10);
        p.setFont(smallFont);
        p.setPen(PixelTheme::kSecondaryText);
        p.drawText(QRectF(timeBox.right() + 8, strip.top() + 12, saveBtn.left() - timeBox.right() - 14, 26),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Today 5:30 PM"));
    }
    p.restore();

    // Bottom: simulated alarm notification popup with Snooze & Dismiss
    const QRectF notif(stage.left() + 42.0, strip.bottom() + 16.0, stage.width() - 84.0, 88.0);
    const QRectF snoozeBtn(notif.left() + 12, notif.bottom() - 34, (notif.width() - 32) * 0.5, 24);
    const QRectF dismissBtn(snoozeBtn.right() + 8, notif.bottom() - 34, (notif.width() - 32) * 0.5, 24);

    if (notifReveal > 0.01) {
        p.save();
        p.setOpacity(notifReveal);
        p.setRenderHint(QPainter::Antialiasing, true);

        p.setPen(QPen(PixelTheme::kBorder, 2));
        p.setBrush(PixelTheme::kBackground);
        p.drawRoundedRect(notif, 6, 6);

        // Bell icon + heading
        const QPixmap bell = PixelTheme::icon(PixelIcon::Bell, PixelTheme::kPrimaryAccent, 14).pixmap(14, 14);
        p.drawPixmap(QPointF(notif.left() + 12, notif.top() + 11), bell);

        QFont headFont(QStringLiteral("Segoe UI"));
        headFont.setPixelSize(12);
        headFont.setBold(true);
        p.setFont(headFont);
        p.setPen(PixelTheme::kPrimaryText);
        p.drawText(QRectF(notif.left() + 30, notif.top() + 9, notif.width() - 42, 18),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Time to go!"));

        QFont subFont(QStringLiteral("Segoe UI"));
        subFont.setPixelSize(10);
        p.setFont(subFont);
        p.setPen(PixelTheme::kSecondaryText);
        p.drawText(QRectF(notif.left() + 12, notif.top() + 29, notif.width() - 24, 16),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   snoozed ? QStringLiteral("Snoozed for 5 minutes...")
                           : QStringLiteral("Your 5:30 PM alarm is ringing!"));

        // Snooze & Dismiss buttons
        QFont bf(QStringLiteral("Segoe UI"));
        bf.setPixelSize(10);
        bf.setBold(true);
        p.setFont(bf);

        p.setPen(QPen(QColor(0xDC, 0x89, 0x81), 1.5));
        p.setBrush(snoozed ? PixelTheme::kHoverAccent : PixelTheme::kPrimaryAccent);
        p.drawRoundedRect(snoozeBtn, 4, 4);
        p.setPen(snoozed ? PixelTheme::kPrimaryText : QColor(Qt::white));
        p.drawText(snoozeBtn, Qt::AlignCenter, QStringLiteral("Snooze 5m"));

        p.setPen(QPen(PixelTheme::kBorder, 1.5));
        p.setBrush(PixelTheme::kCardBg);
        p.drawRoundedRect(dismissBtn, 4, 4);
        p.setPen(PixelTheme::kPrimaryText);
        p.drawText(dismissBtn, Qt::AlignCenter, QStringLiteral("Dismiss"));
        p.restore();
    }

    // Cursor: Save Alarm -> Snooze 5m
    const QPointF pSave = saveBtn.center();
    const QPointF pSnooze = snoozeBtn.center();
    QPointF cursorPos = pSave;
    if (t < 0.20) {
        cursorPos = lerpPoint(timeBox.center(), pSave, smoothStep(0.0, 0.20, t));
    } else if (t < 0.50) {
        cursorPos = pSave;
    } else if (t < 0.70) {
        cursorPos = lerpPoint(pSave, pSnooze, smoothStep(0.50, 0.70, t));
    } else {
        cursorPos = pSnooze;
    }

    const bool clickingSave = (t >= 0.22 && t < 0.28);
    const bool clickingSnooze = (t >= 0.72 && t < 0.78);
    if (t >= 0.23 && t < 0.36) {
        drawClickRipple(p, pSave, (t - 0.23) / 0.13, false);
    } else if (t >= 0.73 && t < 0.88) {
        drawClickRipple(p, pSnooze, (t - 0.73) / 0.15, false);
    }
    drawPixelCursor(p, cursorPos, clickingSave || clickingSnooze);
}

void OnboardingPreview::paintComplete(QPainter& p, const QRect& stage, double t) const
{
    const QPointF center(stage.center().x(), stage.center().y() + 2.0);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(PixelTheme::kBackground);
    p.drawEllipse(center, 48, 48);

    // Subtle pastel pixel sparkles around Chiikawa
    const auto drawSparkle = [&](double ox, double oy, double phase) {
        const double pulse = m_reducedMotion ? 0.85 : (0.45 + 0.55 * std::sin((t + phase) * 6.28318));
        QColor c = PixelTheme::kPrimaryAccent;
        c.setAlpha(qRound(pulse * 220.0));
        const int sx = qRound(center.x() + ox);
        const int sy = qRound(center.y() + oy);
        p.fillRect(sx - 1, sy - 4, 2, 8, c);
        p.fillRect(sx - 4, sy - 1, 8, 2, c);
    };
    drawSparkle(-58, -22, 0.0);
    drawSparkle(56, -26, 0.33);
    drawSparkle(-48, 26, 0.66);
    drawSparkle(52, 22, 0.18);
    p.restore();

    drawPetSprite(p, center, 84, false);
}
