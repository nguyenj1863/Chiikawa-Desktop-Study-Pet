#pragma once

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

} // namespace MacWindowHelper
