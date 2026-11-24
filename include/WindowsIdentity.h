#pragma once

#include <QString>

/**
 * Configures Windows application identity (AppUserModelID and per-user Start Menu shortcut)
 * so Windows notifications display "Chiikawa" instead of the executable filename.
 * Operates strictly per-user without requiring administrator privileges.
 */
namespace WindowsIdentity {

inline constexpr wchar_t kAppUserModelId[] = L"JohnNguyen.ChiikawaStudyPet";
inline constexpr wchar_t kDisplayName[] = L"Chiikawa";

/**
 * Sets the current process AppUserModelID and ensures a matching per-user Start Menu
 * shortcut exists for the currently running executable.
 * Safe to call repeatedly across restarts; no-op on non-Windows platforms.
 */
void initialize();

/** Returns the AppUserModelID string for diagnostics and tests. */
[[nodiscard]] QString appUserModelId();

/** Returns the expected per-user Start Menu shortcut path on Windows (empty elsewhere). */
[[nodiscard]] QString startMenuShortcutPath();

} // namespace WindowsIdentity
