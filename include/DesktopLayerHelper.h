#pragma once

#include <QByteArray>
#include <QtGlobal>

class QWidget;

/**
 * Manages Windows Desktop-Only Mode vs floating desktop-pet mode.
 *
 * When Desktop-Only Mode is enabled:
 * - Keeps the pet window pinned to the bottom Z-order (HWND_BOTTOM) behind all
 *   ordinary and fullscreen application windows via SetWindowPos + WM_WINDOWPOSCHANGING.
 * - Attaches defensively to the desktop shell owner (Progman / WorkerW check) when
 *   available while keeping mouse clicks, dragging, and GIF rendering fully functional
 *   (avoiding SetParent behind SHELLDLL_DefView which blocks pointer input on Win11).
 * - Re-applies desktop layering automatically if Windows Explorer restarts (TaskbarCreated).
 *
 * When disabled:
 * - Restores the normal floating desktop-pet window (HWND_TOPMOST / WindowStaysOnTopHint).
 */
class DesktopLayerHelper {
public:
    DesktopLayerHelper();

    void applyMode(QWidget* petWindow, bool desktopOnly);
    [[nodiscard]] bool isDesktopOnly() const { return m_desktopOnly; }

    /**
     * Call from PetWindow::nativeEvent().
     * Enforces HWND_BOTTOM in Desktop-Only Mode and handles Explorer restarts.
     */
    bool handleNativeEvent(QWidget* petWindow,
                           const QByteArray& eventType,
                           void* message,
                           qintptr* result);

private:
    void enforceBottomZOrder(QWidget* petWindow);

    bool m_desktopOnly = false;
    unsigned int m_taskbarCreatedMsg = 0;
};
