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

inline const QColor kBackground{0xFF, 0xF9, 0xF2};      // #FFF9F2
inline const QColor kCardBg{0xFF, 0xF0, 0xE6};          // #FFF0E6
inline const QColor kPrimaryAccent{0xF3, 0xA6, 0xA0};   // #F3A6A0
inline const QColor kHoverAccent{0xF7, 0xC2, 0xB5};     // #F7C2B5
inline const QColor kSecondaryAccent{0xE8, 0xC7, 0xB7}; // #E8C7B7
inline const QColor kBorder{0xDD, 0xBF, 0xB3};          // #DDBFB3
inline const QColor kPrimaryText{0x65, 0x4C, 0x43};     // #654C43
inline const QColor kSecondaryText{0x96, 0x7F, 0x73};   // #967F73
inline const QColor kDisabled{0xE9, 0xDF, 0xDA};        // #E9DFDA
inline const QColor kDangerBg{0xFC, 0xE4, 0xE1};        // #FCE4E1
inline const QColor kDangerText{0x8F, 0x43, 0x40};      // #8F4340

/**
 * Renders and caches a crisp pixel-art icon on an integer pixel grid at 2x DPR.
 */
[[nodiscard]] QIcon icon(PixelIcon kind, const QColor& color = kPrimaryText, int logicalSize = 14);

/**
 * Loads bundled SIL OFL fonts (Pixelify Sans & Nunito) once into QFontDatabase.
 */
void ensureFontsLoaded();

/** Resolved pixel display font family (Pixelify Sans, falling back to Segoe UI). */
[[nodiscard]] QString displayFontFamily();

/** Resolved friendly body font family (Nunito, falling back to Segoe UI). */
[[nodiscard]] QString bodyFontFamily();

/**
 * Paints a kawaii pixel-stepped card frame with subtle pixel shadow and inner highlight.
 */
void paintPixelCard(QPainter& p,
                    const QRect& bounds,
                    const QColor& fill = kBackground,
                    const QColor& border = kBorder,
                    int margin = 4);

/**
 * Shared pixel-art QSS for SettingsPanel, AlarmNotification, and child controls
 * (buttons, combo boxes + popups, spinboxes, time edits, sliders, scrollbars, tooltips).
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
    static constexpr int kBoxSize = 16;
    static constexpr int kSpacing = 9;
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

protected:
    void changeEvent(QEvent* event) override;

private:
    void refreshIcon();

    PixelIcon m_iconKind;
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
