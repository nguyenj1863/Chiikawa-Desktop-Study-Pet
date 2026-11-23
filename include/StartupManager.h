#pragma once

#include <QString>

/**
 * Per-user Windows startup via HKCU\\...\\Run.
 * No admin rights required. Paths with spaces are quoted.
 * Avoids writing a temporary/dev path as a permanent startup target
 * unless the caller explicitly enables it with a stable executable path.
 */
class StartupManager {
public:
    static constexpr auto kRunValueName = "ChiikawaStudyPet";

    [[nodiscard]] static bool isEnabled();
    [[nodiscard]] static bool enable(const QString& executablePath);
    [[nodiscard]] static bool disable();

    /** True when path looks like a durable install location (not build/ or Temp). */
    [[nodiscard]] static bool isStableExecutablePath(const QString& path);
};
