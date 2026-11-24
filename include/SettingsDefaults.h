#pragma once

#include <QString>

/**
 * Centralized default values and valid ranges for all persistent preferences.
 * Single source of truth used by AppSettings, Restore Defaults, and UI controls.
 */
namespace SettingsDefaults {

inline constexpr int kPetSize = 120;
inline constexpr int kMinPetSize = 64;
inline constexpr int kMaxPetSize = 256;

inline constexpr double kMoveSpeed = 1.0;
inline constexpr double kMinMoveSpeed = 0.25;
inline constexpr double kMaxMoveSpeed = 3.0;

inline constexpr bool kMovementEnabled = true;
inline constexpr bool kAutoAnimationEnabled = true;
inline constexpr bool kDesktopOnlyMode = false;
inline constexpr bool kLaunchOnStartup = false;

inline constexpr int kTimerSeconds = 25 * 60;
inline constexpr int kMinTimerSeconds = 1;
inline constexpr int kMaxTimerSeconds = 24 * 60 * 60;

inline constexpr bool kSoundEnabled = true;
inline constexpr double kAlarmVolume = 0.25;
inline constexpr double kMinAlarmVolume = 0.0;
inline constexpr double kMaxAlarmVolume = 1.0;

inline constexpr bool kAlarmNotificationEnabled = true;
inline constexpr int kSnoozeMinutes = 5;
inline constexpr bool kAlarmUse24Hour = false;

inline QString defaultAlarmSoundId()
{
    return QStringLiteral("soft_chime");
}

inline int clampSnoozeMinutes(int minutes)
{
    if (minutes <= 7) {
        return 5;
    }
    if (minutes <= 12) {
        return 10;
    }
    return 15;
}

} // namespace SettingsDefaults
