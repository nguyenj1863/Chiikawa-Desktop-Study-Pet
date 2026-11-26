#pragma once

#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QIcon>
#include <QPushButton>
#include <QString>

class QPainter;
class QRect;

enum class PixelIcon {
    Play,
    Pause,
    Reset,
    Bell,
    Volume,
    Preview,
    Snooze,
    Close,
    ChevronDown,
    ChevronRight,
    RestoreDefaults,
    Exit,
    Edit,
    Help,
    Gear,
    Spotify
};

namespace PixelTheme {

// Smoked-black translucent glass palette tokens (Privacy Report widget reference).
// Every non-text UI surface is translucent; only text, icons, and Chiikawa artwork stay opaque.
inline const QColor kBgPrimary{0x05, 0x06, 0x08};                 // #050608 deepest base tint
inline const QColor kBackground{0x08, 0x0A, 0x0C};                // #080A0C primary smoked-glass tint
inline const QColor kGlassSurface{0x14, 0x18, 0x20};              // #141820 secondary glass tint
inline const QColor kGlassElevated{0x1C, 0x22, 0x2B};             // #1C222B elevated tint (always paired with alpha)
inline const QColor kCardBg{0x14, 0x18, 0x20, 64};                // nested card wash, ~25% local opacity
inline const QColor kControlBg{0xFF, 0xFF, 0xFF, 22};             // neutral translucent button/control fill (~9%)
inline const QColor kHoverAccent{0xFF, 0xFF, 0xFF, 34};           // hovered control wash (~13%)
inline const QColor kPressedAccent{0xFF, 0xFF, 0xFF, 48};         // pressed neutral control wash (~19%)
inline const QColor kInputBg{0x00, 0x00, 0x00, 72};               // dark translucent input surface (~28%)
inline const QColor kPrimaryAccent{0x6E, 0xA8, 0xFF};             // #6EA8FF foreground accent (text/icons)
inline const QColor kPrimaryAccentHover{0x8A, 0xB9, 0xFF};        // #8AB9FF foreground accent hover
inline const QColor kPrimaryAccentPressed{0x5A, 0x97, 0xF0};      // #5A97F0 foreground accent pressed
inline const QColor kPrimaryAccentGlass{0x55, 0x96, 0xF5, 132};   // translucent blue primary button fill (~52%)
inline const QColor kPrimaryAccentGlassHover{0x69, 0xA6, 0xFC, 165}; // translucent blue hover (~65%)
inline const QColor kPrimaryAccentGlassPressed{0x46, 0x84, 0xE1, 185}; // translucent blue pressed (~73%)
inline const QColor kSecondaryAccent{0x55, 0x96, 0xF5, 92};       // translucent blue selected chip/state (~36%)
inline const QColor kCloseHoverBg{0xFF, 0xFF, 0xFF, 20};          // minimal close-button hover (~8%)
inline const QColor kClosePressedBg{0xFF, 0xFF, 0xFF, 31};        // minimal close-button pressed (~12%)
inline const QColor kBorder{0xFF, 0xFF, 0xFF, 10};                // ultra-subtle translucent border (~4%)
inline const QColor kBorderSubtle{0xFF, 0xFF, 0xFF, 8};           // near-invisible outer hairline (~3%)
inline const QColor kBorderHighlight{0xFF, 0xFF, 0xFF, 14};       // subtle divider / soft highlight (~5.5%)
inline const QColor kFocusBorder{0x6E, 0xA8, 0xFF, 165};          // translucent blue keyboard focus ring (~65%)
inline const QColor kPrimaryText{0xF3, 0xF6, 0xFC};               // #F3F6FC primary text (opaque)
inline const QColor kSecondaryText{0xBC, 0xC7, 0xD9};             // #BCC7D9 secondary text (opaque)
inline const QColor kMutedText{0x8E, 0x9A, 0xAD};                 // #8E9AAD muted labels & footers (opaque)
inline const QColor kDisabled{0xFF, 0xFF, 0xFF, 12};              // disabled translucent surface (~5%)
inline const QColor kDangerBg{0xA0, 0x2A, 0x3E, 58};              // translucent dark crimson surface (~23%)
inline const QColor kDangerText{0xF4, 0x8C, 0x94};                // #F48C94 readable coral-red danger text (opaque)
inline const QColor kDangerBorder{0xF4, 0x8C, 0x94, 34};          // translucent danger hairline (~13%)

// Effective glass opacity per depth tier (alpha / 255):
// Primary ~60% (55-65%), Secondary ~25% local (15-35%), Compact ~66% (60-70%).
inline constexpr int kGlassPrimaryAlpha = 152;
inline constexpr int kGlassSecondaryAlpha = 64;
inline constexpr int kGlassCompactAlpha = 168;
inline const QColor kGlassPrimaryTint{0x08, 0x0A, 0x0C};          // #080A0C smoked-black window glass
inline const QColor kGlassCompactTint{0x08, 0x0A, 0x0C};          // #080A0C compact floating overlay glass

// Tighter, restrained corner radii matching the Privacy Report widget hierarchy
inline constexpr qreal kRadiusOnboarding = 16.0;
inline constexpr qreal kRadiusWindow = 14.0;
inline constexpr qreal kRadiusCard = 12.0;
inline constexpr qreal kRadiusSection = 10.0;
inline constexpr qreal kRadiusButton = 8.0;
inline constexpr qreal kRadiusInput = 8.0;
inline constexpr qreal kRadiusBubble = 10.0;
inline constexpr qreal kRadiusClose = 6.0;

/** Glass surface depth tier for floating windows, cards, and compact overlays. */
enum class GlassLevel {
    Primary,
    Secondary,
    Compact
};

/**
 * Renders and caches a crisp pixel-art icon on an integer pixel grid at 2x DPR.
 */
[[nodiscard]] QIcon icon(PixelIcon kind, const QColor& color = kPrimaryText, int logicalSize = 14);

/**
 * Draws the shared directional arrow icon (used by real Alarm dropdowns,
 * Study Timer steppers, and Onboarding previews) centered inside area.
 */
void drawDropdownArrow(QPainter& p, const QRectF& area, bool pointingUp = false, int size = 10);

/**
 * Loads bundled SIL OFL fonts (Pixelify Sans & Nunito) once into QFontDatabase.
 */
void ensureFontsLoaded();

/** Resolved pixel display font family (Pixelify Sans, falling back to Segoe UI). */
[[nodiscard]] QString displayFontFamily();

/** Resolved friendly body font family (Nunito, falling back to Segoe UI). */
[[nodiscard]] QString bodyFontFamily();

/**
 * Paints a smoked-black Liquid Glass floating surface with soft ambient shadow,
 * subtle vertical gradient, upper-edge glass highlight, and thin rim border.
 */
void paintGlassSurface(QPainter& p,
                       const QRect& bounds,
                       GlassLevel level = GlassLevel::Primary,
                       qreal radius = -1.0,
                       int margin = 5);

/**
 * Paints a rounded dark glass card frame (compat wrapper around paintGlassSurface).
 */
void paintPixelCard(QPainter& p,
                    const QRect& bounds,
                    const QColor& fill = kBackground,
                    const QColor& border = kBorder,
                    int margin = 4);

/**
 * Shared smoked-black Liquid Glass QSS for SettingsPanel, OnboardingWindow,
 * AlarmNotification, CompletionToast, and child controls.
 * Preserves all existing typography (font families, sizes, weights, and spacing).
 */
[[nodiscard]] QString styleSheet();

} // namespace PixelTheme

/**
 * Kawaii pixel-art checkbox with custom-painted stepped box, crisp checkmark,
 * hover row highlight, and keyboard focus indicator.
 */
class PixelCheckBox : public QCheckBox {
    Q_OBJECT

public:
    explicit PixelCheckBox(const QString& text, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kBoxSize = 14;
    static constexpr int kSpacing = 7;
    bool m_hovered = false;
};

/**
 * Crisp pixel-art icon button with hover/pressed/focus states and pointing-hand cursor.
 */
class PixelIconButton : public QPushButton {
    Q_OBJECT

public:
    PixelIconButton(PixelIcon iconKind,
                    int buttonSize,
                    int iconSize,
                    bool primary = false,
                    QWidget* parent = nullptr);

    void setPixelIcon(PixelIcon iconKind);

    [[nodiscard]] QSize sizeHint() const override { return QSize(m_buttonSize, m_buttonSize); }
    [[nodiscard]] QSize minimumSizeHint() const override { return QSize(m_buttonSize, m_buttonSize); }

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void refreshIcon();

    PixelIcon m_iconKind;
    int m_buttonSize = 28;
    int m_iconSize = 14;
    bool m_primary = false;
    bool m_hovered = false;
};

/**
 * Compact kawaii pixel-art confirmation dialog (replaces generic Windows QMessageBox).
 */
class PixelConfirmDialog : public QDialog {
    Q_OBJECT

public:
    PixelConfirmDialog(const QString& title,
                       const QString& body,
                       const QString& confirmText,
                       const QString& cancelText,
                       QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};
