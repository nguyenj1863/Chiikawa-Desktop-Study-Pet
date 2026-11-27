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

inline constexpr int kQuickActionIconScalePercent = 110;
inline constexpr int kMinQuickActionIconScalePercent = 80;
inline constexpr int kMaxQuickActionIconScalePercent = 150;
inline constexpr int kQuickActionIconScaleStep = 5;

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
inline constexpr bool kButtonSoundsEnabled = true;
inline constexpr bool kOnboardingCompleted = false;
inline constexpr double kAlarmVolume = 0.25;
inline constexpr double kMinAlarmVolume = 0.0;
inline constexpr double kMaxAlarmVolume = 1.0;

inline constexpr bool kAlarmNotificationEnabled = true;
inline constexpr int kSnoozeMinutes = 5;
inline constexpr bool kAlarmUse24Hour = false;

inline constexpr bool kCheckInsEnabled = false;
inline constexpr int kCheckInMinutes = 15;
inline constexpr int kMinCheckInMinutes = 5;
inline constexpr int kMaxCheckInMinutes = 60;
inline constexpr int kCheckInMinuteChoices[] = {5, 10, 15, 30, 60};

/** Snaps a check-in interval to the nearest offered choice. */
inline int clampCheckInMinutes(int minutes)
{
    int best = kCheckInMinuteChoices[0];
    for (const int choice : kCheckInMinuteChoices) {
        const int diff = choice > minutes ? choice - minutes : minutes - choice;
        const int bestDiff = best > minutes ? best - minutes : minutes - best;
        if (diff < bestDiff) {
            best = choice;
        }
    }
    return best;
}

// Enabled question formats as bits: 1 = multiple choice, 2 = multiple select, 4 = written answer.
inline constexpr int kStudyFormatSingleChoice = 0x1;
inline constexpr int kStudyFormatMultiSelect = 0x2;
inline constexpr int kStudyFormatWritten = 0x4;
inline constexpr int kStudyFormatsAll = 0x7;
inline constexpr int kStudyFormatsMask = kStudyFormatsAll;

inline constexpr int kStudyFormatModeMixed = 0;
inline constexpr int kStudyFormatModeFocus = 1;
inline constexpr int kStudyFormatMode = kStudyFormatModeMixed;
inline constexpr int kStudyFocusFormat = kStudyFormatSingleChoice;

/** Keeps only known format bits; an empty set falls back to every format (at least one must be on). */
inline int clampStudyFormats(int mask)
{
    const int known = mask & kStudyFormatsAll;
    return known == 0 ? kStudyFormatsAll : known;
}

/** A single known format bit for Focus mode. */
inline int clampStudyFocusFormat(int format)
{
    return (format == kStudyFormatMultiSelect || format == kStudyFormatWritten) ? format : kStudyFormatSingleChoice;
}

inline QString defaultStudyModel()
{
    return QStringLiteral("qwen3.5:4b-q4_K_M");
}

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
