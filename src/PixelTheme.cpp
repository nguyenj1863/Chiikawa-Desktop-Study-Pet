#include "PixelTheme.h"

#include <QEnterEvent>
#include <QEvent>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace {

// 12x12 bitmap pixel-art definitions ('#' = filled pixel, '.' = transparent)
using PixelGrid = std::array<const char*, 12>;

PixelGrid gridFor(PixelIcon kind)
{
    switch (kind) {
    case PixelIcon::Play:
        return {{
            "............",
            "..##........",
            "..####......",
            "..######....",
            "..########..",
            "..#########.",
            "..#########.",
            "..########..",
            "..######....",
            "..####......",
            "..##........",
            "............",
        }};
    case PixelIcon::Pause:
        return {{
            "............",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "..###..###..",
            "............",
        }};
    case PixelIcon::Reset:
    case PixelIcon::RestoreDefaults:
        return {{
            "............",
            "....#####...",
            "..##.....##.",
            ".##.......#.",
            ".##.....####",
            ".##......###",
            ".##.........",
            ".##.......#.",
            "..##.....##.",
            "....#####...",
            "............",
            "............",
        }};
    case PixelIcon::Bell:
        return {{
            ".....##.....",
            "....####....",
            "...######...",
            "...######...",
            "..########..",
            "..########..",
            "..########..",
            ".##########.",
            ".##########.",
            "....####....",
            ".....##.....",
            "............",
        }};
    case PixelIcon::Volume:
        return {{
            "............",
            ".....##.....",
            "....###..#..",
            "..#####...#.",
            ".######.#.#.",
            ".######.#.#.",
            ".######.#.#.",
            "..#####...#.",
            "....###..#..",
            ".....##.....",
            "............",
            "............",
        }};
    case PixelIcon::Preview:
        return {{
            "............",
            ".##.........",
            ".####....#..",
            ".######...#.",
            ".#######.#.#",
            ".#######.#.#",
            ".######...#.",
            ".####....#..",
            ".##.........",
            "............",
            "............",
            "............",
        }};
    case PixelIcon::Snooze:
        return {{
            "............",
            "..######....",
            "......##....",
            ".....##.....",
            "....##......",
            "...##.......",
            "..######....",
            ".......####.",
            ".........##.",
            "........##..",
            ".......####.",
            "............",
        }};
    case PixelIcon::Close:
        return {{
            "............",
            "..##....##..",
            "..###..###..",
            "...######...",
            "....####....",
            "....####....",
            "...######...",
            "..###..###..",
            "..##....##..",
            "............",
            "............",
            "............",
        }};
    case PixelIcon::ChevronDown:
        return {{
            "............",
            "............",
            "............",
            ".##......##.",
            "..##....##..",
            "...##..##...",
            "....####....",
            ".....##.....",
            "............",
            "............",
            "............",
            "............",
        }};
    case PixelIcon::ChevronRight:
        return {{
            "............",
            "...##.......",
            "...###......",
            "....###.....",
            ".....###....",
            "......###...",
            ".....###....",
            "....###.....",
            "...###......",
            "...##.......",
            "............",
            "............",
        }};
    case PixelIcon::Exit:
        return {{
            "............",
            ".#####......",
            ".##.........",
            ".##....##...",
            ".##.....##..",
            ".##..######.",
            ".##..######.",
            ".##.....##..",
            ".##....##...",
            ".##.........",
            ".#####......",
            "............",
        }};
    case PixelIcon::Edit:
        return {{
            "........##..",
            ".......####.",
            "......####..",
            ".....####...",
            "....####....",
            "...####.....",
            "..####......",
            ".####.......",
            ".###........",
            ".##.........",
            "............",
            "............",
        }};
    }
    return {{
        "............", "............", "............", "............",
        "............", "............", "............", "............",
        "............", "............", "............", "............",
    }};
}

} // namespace

namespace PixelTheme {

QIcon icon(PixelIcon kind, const QColor& color, int logicalSize)
{
    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1_%2_%3")
        .arg(static_cast<int>(kind))
        .arg(color.rgba(), 8, 16, QChar(u'0'))
        .arg(logicalSize);
    if (auto it = cache.constFind(key); it != cache.constEnd()) {
        return it.value();
    }

    constexpr int dpr = 2;
    const int px = std::max(12, logicalSize) * dpr;
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(static_cast<qreal>(dpr));

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const auto grid = gridFor(kind);
    const qreal cell = static_cast<qreal>(logicalSize) / 12.0;
    for (int y = 0; y < 12; ++y) {
        const char* row = grid[static_cast<std::size_t>(y)];
        for (int x = 0; x < 12; ++x) {
            if (row[x] == '#') {
                const int rx = qRound(x * cell);
                const int ry = qRound(y * cell);
                const int rw = std::max(1, qRound((x + 1) * cell) - rx);
                const int rh = std::max(1, qRound((y + 1) * cell) - ry);
                p.fillRect(rx, ry, rw, rh, color);
            }
        }
    }

    QIcon result(pm);
    cache.insert(key, result);
    return result;
}

void paintPixelCard(QPainter& p,
                    const QRect& bounds,
                    const QColor& fill,
                    const QColor& border,
                    int margin)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect card = bounds.adjusted(margin, margin, -margin - 2, -margin - 2);
    const int x = card.x();
    const int y = card.y();
    const int w = card.width();
    const int h = card.height();
    constexpr int c = 4; // pixel corner step

    auto drawSteppedRect = [&](int ox, int oy, const QColor& col) {
        p.fillRect(x + ox + c, y + oy, w - 2 * c, h, col);
        p.fillRect(x + ox, y + oy + c, w, h - 2 * c, col);
        p.fillRect(x + ox + 2, y + oy + 2, w - 4, h - 4, col);
    };

    // Soft pixel drop shadow (2px down-right)
    drawSteppedRect(2, 2, QColor(101, 76, 67, 26));

    // Outer pixel border
    drawSteppedRect(0, 0, border);

    // Inner fill (2px inset)
    p.fillRect(x + c, y + 2, w - 2 * c, h - 4, fill);
    p.fillRect(x + 2, y + c, w - 4, h - 2 * c, fill);

    // Subtle warm top highlight strip inside the card
    p.fillRect(x + c + 1, y + 2, w - 2 * c - 2, 2, QColor(255, 255, 255, 160));

    p.restore();
}

QString styleSheet()
{
    return QStringLiteral(R"(
        QWidget#header, QWidget#scrollContent, QScrollArea#bodyScroll, QWidget#sectionBody {
            background: transparent;
        }
        QLabel {
            color: #654C43;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            background: transparent;
        }
        QLabel#title {
            color: #654C43;
            font-size: 15px;
            font-weight: 700;
            letter-spacing: 0.4px;
        }
        QLabel#sectionTitle {
            color: #654C43;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#sectionSummary {
            color: #967F73;
            font-size: 11px;
            font-weight: 600;
        }
        QLabel#muted {
            color: #967F73;
            font-size: 11px;
        }
        QLabel#valueBadge {
            color: #967F73;
            font-size: 11px;
            font-weight: 700;
        }
        QFrame#pixelDivider {
            background: #E8C7B7;
            max-height: 2px;
            min-height: 2px;
            border: none;
        }
        QFrame#timerCard {
            background: #FFF0E6;
            border: 2px solid #DDBFB3;
            border-radius: 6px;
        }
        QPushButton#countdownDisplay {
            background: transparent;
            color: #654C43;
            border: none;
            font-family: 'Cascadia Mono', 'Consolas', 'Segoe UI', monospace;
            font-size: 32px;
            font-weight: 700;
            padding: 2px 6px;
        }
        QPushButton#countdownDisplay:hover {
            color: #4D3730;
            background: rgba(247, 194, 181, 0.35);
            border-radius: 4px;
        }
        QPushButton#countdownDisplay:disabled {
            color: #654C43;
            background: transparent;
        }
        QSpinBox, QTimeEdit {
            background: #FFF9F2;
            border: 2px solid #DDBFB3;
            border-radius: 4px;
            padding: 3px 6px;
            color: #654C43;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            font-weight: 600;
            min-height: 22px;
            selection-background-color: #F7C2B5;
            selection-color: #654C43;
        }
        QSpinBox:hover, QTimeEdit:hover {
            border-color: #F3A6A0;
            background: #FFFFFF;
        }
        QSpinBox:focus, QTimeEdit:focus {
            border-color: #F3A6A0;
            background: #FFFFFF;
        }
        QSpinBox::up-button, QSpinBox::down-button,
        QTimeEdit::up-button, QTimeEdit::down-button {
            width: 16px;
            border: none;
            background: #FFF0E6;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover,
        QTimeEdit::up-button:hover, QTimeEdit::down-button:hover {
            background: #F7C2B5;
        }
        QComboBox {
            background: #FFF9F2;
            border: 2px solid #DDBFB3;
            border-radius: 4px;
            padding: 4px 24px 4px 8px;
            color: #654C43;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            font-weight: 600;
            min-height: 22px;
        }
        QComboBox:hover {
            border-color: #F3A6A0;
            background: #FFFFFF;
        }
        QComboBox:focus {
            border-color: #F3A6A0;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 22px;
            border-left: 2px solid #E8C7B7;
            background: #FFF0E6;
        }
        QComboBox::drop-down:hover {
            background: #F7C2B5;
        }
        QComboBox::down-arrow {
            width: 0px;
            height: 0px;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #654C43;
        }
        QComboBox QAbstractItemView {
            background: #FFF9F2;
            border: 2px solid #DDBFB3;
            color: #654C43;
            selection-background-color: #F7C2B5;
            selection-color: #654C43;
            outline: 0;
            padding: 2px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 12px;
            font-weight: 600;
        }
        QComboBox QAbstractItemView::item {
            min-height: 24px;
            padding: 3px 8px;
        }
        QComboBox QAbstractItemView::item:hover {
            background: #FFF0E6;
        }
        QSlider {
            background: transparent;
            min-height: 22px;
        }
        QSlider::groove:horizontal {
            height: 6px;
            background: #E8C7B7;
            border: 1px solid #DDBFB3;
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #F3A6A0;
            border: 1px solid #DF8F88;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #FFF9F2;
            border: 2px solid #F3A6A0;
            width: 12px;
            height: 12px;
            margin: -5px 0;
            border-radius: 3px;
        }
        QSlider::handle:horizontal:hover {
            background: #F7C2B5;
            border-color: #E08D86;
        }
        QSlider::handle:horizontal:pressed {
            background: #F3A6A0;
            border-color: #D27D76;
        }
        QPushButton {
            background: #FFF0E6;
            color: #654C43;
            border: 2px solid #DDBFB3;
            border-radius: 4px;
            padding: 5px 10px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
            font-weight: 700;
            min-height: 20px;
        }
        QPushButton:hover {
            background: #F7C2B5;
            border-color: #DF9B92;
        }
        QPushButton:pressed {
            background: #F3A6A0;
            color: #FFFFFF;
            border-color: #DC8981;
        }
        QPushButton:focus {
            border-color: #F3A6A0;
        }
        QPushButton:disabled {
            background: #E9DFDA;
            color: #A6958C;
            border-color: #DFD2CC;
        }
        QPushButton#chip {
            background: #FFF0E6;
            color: #654C43;
            border: 2px solid #DDBFB3;
            border-radius: 4px;
            padding: 4px 8px;
            min-height: 18px;
        }
        QPushButton#chip:hover {
            background: #F7C2B5;
            border-color: #DF9B92;
        }
        QPushButton#chip[active="true"] {
            background: #F3A6A0;
            color: #FFFFFF;
            border-color: #DC8981;
        }
        QPushButton#chip[active="true"]:hover {
            background: #EA9891;
        }
        QPushButton#primary {
            background: #F3A6A0;
            color: #FFFFFF;
            border: 2px solid #DC8981;
        }
        QPushButton#primary:hover {
            background: #EA9891;
            border-color: #D27C74;
        }
        QPushButton#primary:pressed {
            background: #DF8880;
        }
        QPushButton#iconBtn {
            background: #FFF0E6;
            border: 2px solid #DDBFB3;
            border-radius: 4px;
            padding: 0px;
        }
        QPushButton#iconBtn:hover {
            background: #F7C2B5;
            border-color: #DF9B92;
        }
        QPushButton#iconBtn:pressed {
            background: #F3A6A0;
        }
        QPushButton#iconBtnPrimary {
            background: #F3A6A0;
            border: 2px solid #DC8981;
            border-radius: 4px;
            padding: 0px;
        }
        QPushButton#iconBtnPrimary:hover {
            background: #EA9891;
            border-color: #D27C74;
        }
        QPushButton#iconBtnPrimary:pressed {
            background: #DF8880;
        }
        QPushButton#subtle {
            background: #FFF9F2;
            color: #967F73;
            border: 2px solid #E8C7B7;
        }
        QPushButton#subtle:hover {
            background: #FFF0E6;
            color: #654C43;
            border-color: #DDBFB3;
        }
        QPushButton#danger {
            background: #FCE4E1;
            color: #8F4340;
            border: 2px solid #EBC2BC;
        }
        QPushButton#danger:hover {
            background: #F7CFC9;
            border-color: #E0A9A0;
        }
        QPushButton#danger:pressed {
            background: #F0B8B0;
        }
        QToolTip {
            background: #FFF9F2;
            color: #654C43;
            border: 2px solid #DDBFB3;
            padding: 4px 6px;
            font-family: 'Segoe UI', 'Yu Gothic UI', sans-serif;
            font-size: 11px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 6px;
            margin: 2px 0 2px 2px;
        }
        QScrollBar::handle:vertical {
            background: #DDBFB3;
            min-height: 26px;
            border-radius: 2px;
        }
        QScrollBar::handle:vertical:hover {
            background: #F3A6A0;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
            border: none;
            background: none;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    )");
}

} // namespace PixelTheme

// --- PixelCheckBox ---

PixelCheckBox::PixelCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
}

QSize PixelCheckBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int w = 6 + kBoxSize + kSpacing + fm.horizontalAdvance(text()) + 10;
    const int h = std::max(26, fm.height() + 8);
    return {w, h};
}

QSize PixelCheckBox::minimumSizeHint() const
{
    return sizeHint();
}

void PixelCheckBox::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QCheckBox::enterEvent(event);
}

void PixelCheckBox::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QCheckBox::leaveEvent(event);
}

void PixelCheckBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();
    if (m_hovered && isEnabled()) {
        p.fillRect(r.adjusted(1, 2, -1, -2), PixelTheme::kCardBg);
    }

    const int boxY = (r.height() - kBoxSize) / 2;
    const QRect box(4, boxY, kBoxSize, kBoxSize);

    if (isChecked()) {
        const QColor fill = isEnabled()
            ? (isDown() ? QColor(0xDF, 0x88, 0x80)
                        : (m_hovered ? QColor(0xEA, 0x98, 0x91) : PixelTheme::kPrimaryAccent))
            : PixelTheme::kDisabled;
        const QColor border = isEnabled() ? QColor(0xDC, 0x89, 0x81) : PixelTheme::kBorder;
        p.fillRect(box, border);
        p.fillRect(box.adjusted(2, 2, -2, -2), fill);

        // Crisp pixel checkmark
        p.fillRect(box.x() + 3, box.y() + 8, 2, 2, Qt::white);
        p.fillRect(box.x() + 5, box.y() + 10, 2, 2, Qt::white);
        p.fillRect(box.x() + 7, box.y() + 8, 2, 2, Qt::white);
        p.fillRect(box.x() + 9, box.y() + 6, 2, 2, Qt::white);
        p.fillRect(box.x() + 11, box.y() + 4, 2, 2, Qt::white);
    } else {
        const QColor fill = m_hovered && isEnabled() ? QColor(0xFF, 0xFF, 0xFF) : PixelTheme::kBackground;
        const QColor border = m_hovered && isEnabled() ? PixelTheme::kPrimaryAccent : PixelTheme::kBorder;
        p.fillRect(box, border);
        p.fillRect(box.adjusted(2, 2, -2, -2), fill);
    }

    if (hasFocus()) {
        p.setPen(QPen(PixelTheme::kPrimaryAccent, 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(-2, -2, 1, 1));
    }

    const QRect textRect(box.right() + kSpacing, 0,
                         r.width() - box.right() - kSpacing - 4, r.height());
    p.setPen(isEnabled() ? PixelTheme::kPrimaryText : PixelTheme::kSecondaryText);
    p.setFont(font());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
}

// --- PixelIconButton ---

PixelIconButton::PixelIconButton(PixelIcon iconKind,
                                 int buttonSize,
                                 int iconSize,
                                 bool primary,
                                 QWidget* parent)
    : QPushButton(parent)
    , m_iconKind(iconKind)
    , m_iconSize(iconSize)
    , m_primary(primary)
{
    setFixedSize(buttonSize, buttonSize);
    setObjectName(primary ? QStringLiteral("iconBtnPrimary") : QStringLiteral("iconBtn"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setIconSize(QSize(iconSize, iconSize));
    refreshIcon();
}

void PixelIconButton::setPixelIcon(PixelIcon iconKind)
{
    m_iconKind = iconKind;
    refreshIcon();
}

void PixelIconButton::changeEvent(QEvent* event)
{
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        refreshIcon();
    }
}

void PixelIconButton::refreshIcon()
{
    const QColor color = !isEnabled()
        ? PixelTheme::kSecondaryText
        : (m_primary ? QColor(Qt::white) : PixelTheme::kPrimaryText);
    setIcon(PixelTheme::icon(m_iconKind, color, m_iconSize));
}

// --- PixelConfirmDialog ---

PixelConfirmDialog::PixelConfirmDialog(const QString& title,
                                       const QString& body,
                                       const QString& confirmText,
                                       const QString& cancelText,
                                       QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(268);
    setStyleSheet(PixelTheme::styleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(10);

    auto* titleLbl = new QLabel(title, this);
    titleLbl->setObjectName(QStringLiteral("title"));
    titleLbl->setWordWrap(true);
    root->addWidget(titleLbl);

    auto* bodyLbl = new QLabel(body, this);
    bodyLbl->setObjectName(QStringLiteral("muted"));
    bodyLbl->setWordWrap(true);
    root->addWidget(bodyLbl);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto* cancelBtn = new QPushButton(cancelText, this);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    auto* confirmBtn = new QPushButton(confirmText, this);
    confirmBtn->setObjectName(QStringLiteral("primary"));
    confirmBtn->setCursor(Qt::PointingHandCursor);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirmBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnRow->addWidget(cancelBtn, 1);
    btnRow->addWidget(confirmBtn, 1);
    root->addLayout(btnRow);

    adjustSize();
}

void PixelConfirmDialog::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintPixelCard(p, rect(), PixelTheme::kBackground, PixelTheme::kBorder, 4);
}
