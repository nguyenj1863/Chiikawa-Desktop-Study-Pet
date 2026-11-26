#pragma once

#include <functional>

class QWidget;

namespace MacWindowHelper {

/**
 * Configures a top-level floating widget's native macOS NSWindow so it stays
 * above normal and full-screen windows across all Desktop Spaces and does not
 * hide when another application activates.
 *
 * No-op on non-macOS platforms and under non-Cocoa QPA backends (e.g. offscreen).
 */
void configureFloatingOverlay(QWidget* widget, bool disableShadow = false);

/**
 * Returns true when macOS "Reduce Motion" accessibility preference is enabled.
 * Returns false on non-macOS platforms.
 */
[[nodiscard]] bool prefersReducedMotion();

/**
 * Installs a read-only macOS global mouse-down observer (no Accessibility permission
 * required, never consumes or blocks clicks) to detect clicks on other applications
 * or the desktop while a transient popup/menu is open.
 * Returns an opaque token to pass to removeOutsideClickMonitor(), or nullptr if unavailable.
 */
[[nodiscard]] void* installOutsideClickMonitor(std::function<void()> callback);
void removeOutsideClickMonitor(void* token);

} // namespace MacWindowHelper
