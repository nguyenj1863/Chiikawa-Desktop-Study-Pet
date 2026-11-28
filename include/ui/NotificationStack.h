#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>

class QScreen;
class QWidget;

/**
 * One place for Chiikawa's notifications (timer done, alarm): bottom-right of the primary screen's
 * work area, above the taskbar, 20 px from the edges. When several are open they stack upward with
 * a 10 px gap, the newest nearest the corner, so they never overlap. The stack follows the primary
 * screen when it changes, is unplugged, or its work area moves (taskbar, DPI or resolution changes);
 * sizes are in device-independent pixels, so Qt's high-DPI scaling applies as everywhere else.
 * A notification leaves the stack when it hides.
 */
class NotificationStack : public QObject {
    Q_OBJECT

public:
    static constexpr int kScreenEdgeMarginPx = 20;
    static constexpr int kGapPx = 10;

    /** The stack every notification uses. */
    static NotificationStack& shared();

    explicit NotificationStack(QObject* parent = nullptr);

    /**
     * Adds (or moves to the front) a notification about to be shown, re-lays out the others, and
     * returns where it belongs. The caller shows it there (it may animate into place).
     */
    QPoint place(QWidget* notification);
    /** Takes a notification out and closes the gap it leaves. Hiding one does this too. */
    void remove(QWidget* notification);
    /** Moves every open notification to its place again (after a screen change). */
    void relayout();

    [[nodiscard]] QList<QWidget*> notifications() const;
    /** Work area the stack uses; tests can pin it to a fixed rectangle. */
    [[nodiscard]] QRect workArea() const;
    void setWorkAreaOverride(const QRect& area) { m_override = area; }

    /** Bottom-right corner position for one notification, kept inside the area. */
    [[nodiscard]] static QPoint bottomRightPosition(const QRect& available, const QSize& size,
                                                    int margin = kScreenEdgeMarginPx);
    /**
     * Positions for notifications of the given sizes, newest first: each sits above the previous
     * one, right-aligned, never above the top of the area.
     */
    [[nodiscard]] static QList<QPoint> stackPositions(const QRect& available, const QList<QSize>& sizes,
                                                      int margin = kScreenEdgeMarginPx, int gap = kGapPx);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] QScreen* targetScreen() const;
    void bindScreen(QScreen* screen);
    void prune();

    QList<QPointer<QWidget>> m_items;   // newest first
    QPointer<QScreen> m_screen;
    QRect m_override;
};
