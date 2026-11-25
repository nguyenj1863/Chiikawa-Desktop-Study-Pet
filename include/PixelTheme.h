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
    Gear
};

namespace PixelTheme {

// Smoked-black Liquid Glass palette tokens (Windows Terminal acrylic reference).
// Surfaces are translucent black; text, icons, and GIFs stay fully opaque.
inline const QColor kBgPrimary{0x05, 0x06, 0x08};             // #050608 deepest base
inline const QColor kBackground{0x0A, 0x0C, 0x10};            // #0A0C10 primary glass tint (popups, tooltips)
inline const QColor kGlassSurface{0x14, 0x18, 0x20};          // #141820 secondary glass tint
inline const QColor kGlassElevated{0x1C, 0x22, 0x2B};         // #1C222B elevated tint (always used with alpha)
inline const QColor kCardBg{0x1C, 0x22, 0x2B, 46};            // nested card wash, ~18% (near-transparent)
inline const QColor kHoverAccent{0xFF, 0xFF, 0xFF, 34};       // hovered control wash, ~13% white
inline const QColor kPrimaryAccent{0x6E, 0xA8, 0xFF};         // #6EA8FF primary action
inline const QColor kPrimaryAccentHover{0x8A, 0xB9, 0xFF};    // #8AB9FF hovered primary action
inline const QColor kPrimaryAccentPressed{0x5A, 0x97, 0xF0};  // #5A97F0 pressed primary action
inline const QColor kSecondaryAccent{0x29, 0x4B, 0x78};       // #294B78 selected / subtle accent
inline const QColor kBorder{0xFF, 0xFF, 0xFF, 40};            // white low-alpha glass hairline
inline const QColor kBorderHighlight{0xFF, 0xFF, 0xFF, 56};   // upper-edge specular hairline
inline const QColor kPrimaryText{0xF3, 0xF6, 0xFC};           // #F3F6FC primary text (opaque)
inline const QColor kSecondaryText{0xBC, 0xC7, 0xD9};         // #BCC7D9 descriptions
inline const QColor kMutedText{0x8E, 0x9A, 0xAD};             // #8E9AAD labels & footers
inline const QColor kDisabled{0x1C, 0x22, 0x2B, 120};         // disabled control fill
inline const QColor kDangerBg{0x2B, 0x18, 0x22, 204};         // restrained dark crimson surface
inline const QColor kDangerText{0xF4, 0x8C, 0x94};            // #F48C94 readable coral-red danger text
inline const QColor kDangerBorder{0x5E, 0x2C, 0x3B};          // #5E2C3B subtle danger border

// Effective glass opacity per depth tier (alpha / 255). Primary ~78%, Compact ~85%, Secondary ~58%.
inline constexpr int kGlassPrimaryAlpha = 199;
inline constexpr int kGlassSecondaryAlpha = 148;
inline constexpr int kGlassCompactAlpha = 217;
inline const QColor kGlassPrimaryTint{0x08, 0x0A, 0x0D};      // #080A0D near-black smoked glass
inline const QColor kGlassCompactTint{0x04, 0x05, 0x07};      // compact overlays: deeper black for contrast

// Shared corner radii
inline constexpr qreal kRadiusWindow = 20.0;
inline constexpr qreal kRadiusCard = 14.0;
inline constexpr qreal kRadiusButton = 10.0;
inline constexpr qreal kRadiusInput = 10.0;
inline constexpr qreal kRadiusBubble = 15.0;

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
    void changeEvent(QEvent* event) override;

private:
    void refreshIcon();

    PixelIcon m_iconKind;
    int m_buttonSize = 28;
    int m_iconSize = 14;
    bool m_primary = false;
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
