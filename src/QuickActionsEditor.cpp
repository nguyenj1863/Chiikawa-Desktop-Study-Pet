#include "QuickActionsEditor.h"

#include "ExternalAppLauncher.h"
#include "PixelTheme.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>

// ============================================================================
// QuickActionDialog
// ============================================================================

QuickActionDialog::QuickActionDialog(const QuickActionItem* existing, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_isEdit(existing != nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(340);
    setStyleSheet(PixelTheme::styleSheet());

    if (existing) {
        m_item = *existing;
    } else {
        m_item.id = QuickActionItem::generateCustomId();
        m_item.type = QuickActionType::CustomApp;
        m_item.enabled = true;
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    auto* title = new QLabel(m_isEdit ? QStringLiteral("Edit Shortcut") : QStringLiteral("Add Application"), this);
    title->setObjectName(QStringLiteral("toastTitle"));
    root->addWidget(title);

    auto* nameLbl = new QLabel(QStringLiteral("Application Name"), this);
    nameLbl->setObjectName(QStringLiteral("muted"));
    root->addWidget(nameLbl);

    m_nameEdit = new QLineEdit(m_item.displayName, this);
    m_nameEdit->setPlaceholderText(QStringLiteral("e.g. Visual Studio Code"));
    root->addWidget(m_nameEdit);

    if (m_item.type == QuickActionType::CustomApp) {
        auto* pathLbl = new QLabel(QStringLiteral("Application"), this);
        pathLbl->setObjectName(QStringLiteral("muted"));
        root->addWidget(pathLbl);

        auto* pathRow = new QHBoxLayout();
        pathRow->setSpacing(6);
        m_pathEdit = new QLineEdit(m_item.applicationReference, this);
#ifdef Q_OS_MACOS
        m_pathEdit->setPlaceholderText(QStringLiteral("/Applications/App.app"));
#else
        m_pathEdit->setPlaceholderText(QStringLiteral("Select .exe or .lnk application"));
#endif
        m_browseBtn = new QPushButton(QStringLiteral("Browse"), this);
        m_browseBtn->setCursor(Qt::PointingHandCursor);
        connect(m_browseBtn, &QPushButton::clicked, this, &QuickActionDialog::onBrowseApplication);
        pathRow->addWidget(m_pathEdit, 1);
        pathRow->addWidget(m_browseBtn);
        root->addLayout(pathRow);
    }

    auto* iconLbl = new QLabel(QStringLiteral("Icon"), this);
    iconLbl->setObjectName(QStringLiteral("muted"));
    root->addWidget(iconLbl);

    auto* iconRow = new QHBoxLayout();
    iconRow->setSpacing(8);
    m_iconPreview = new QLabel(this);
    m_iconPreview->setFixedSize(36, 36);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    m_iconPreview->setStyleSheet(QStringLiteral(
        "background: rgba(255, 255, 255, 0.08); border-radius: 8px; border: 1px solid rgba(255, 255, 255, 0.12);"));

    m_changeIconBtn = new QPushButton(QStringLiteral("Change"), this);
    m_changeIconBtn->setCursor(Qt::PointingHandCursor);
    connect(m_changeIconBtn, &QPushButton::clicked, this, &QuickActionDialog::onChooseCustomIcon);

    m_resetIconBtn = new QPushButton(QStringLiteral("Reset"), this);
    m_resetIconBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetIconBtn, &QPushButton::clicked, this, &QuickActionDialog::onResetCustomIcon);

    iconRow->addWidget(m_iconPreview);
    iconRow->addWidget(m_changeIconBtn);
    iconRow->addWidget(m_resetIconBtn);
    iconRow->addStretch(1);
    root->addLayout(iconRow);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #FF8E8E; font-size: 11px;"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 4, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);

    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* saveBtn = new QPushButton(m_isEdit ? QStringLiteral("Save") : QStringLiteral("Add Shortcut"), this);
    saveBtn->setObjectName(QStringLiteral("primary"));
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &QuickActionDialog::onAcceptClicked);

    buttons->addWidget(cancelBtn);
    buttons->addWidget(saveBtn);
    root->addLayout(buttons);

    refreshIconPreview();
}

void QuickActionDialog::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintGlassSurface(p, rect(), PixelTheme::GlassLevel::Primary, PixelTheme::kRadiusCard, 6);
}

void QuickActionDialog::onBrowseApplication()
{
    QString selected;
#ifdef Q_OS_MACOS
    selected = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose Application"),
        QStringLiteral("/Applications"),
        QStringLiteral("Applications (*.app)"));
#else
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    selected = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose Application"),
        startDir.isEmpty() ? QStringLiteral("C:/Program Files") : startDir,
        QStringLiteral("Applications (*.exe *.lnk)"));
#endif
    if (selected.isEmpty()) {
        return;
    }
    if (m_pathEdit) {
        m_pathEdit->setText(selected);
    }
    m_item.applicationReference = selected;
    if (m_nameEdit && m_nameEdit->text().trimmed().isEmpty()) {
        m_nameEdit->setText(ExternalAppLauncher::detectAppDisplayName(selected));
    }
    m_errorLabel->hide();
    refreshIconPreview();
}

void QuickActionDialog::onChooseCustomIcon()
{
    const QString picDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose Custom Icon"),
        picDir,
        QStringLiteral("Images (*.png *.ico *.icns *.jpg *.jpeg *.webp *.bmp)"));
    if (file.isEmpty()) {
        return;
    }
    m_pendingIconSource = file;
    m_resetIconRequested = false;
    m_errorLabel->hide();
    refreshIconPreview();
}

void QuickActionDialog::onResetCustomIcon()
{
    m_pendingIconSource.clear();
    m_resetIconRequested = true;
    refreshIconPreview();
}

void QuickActionDialog::refreshIconPreview()
{
    QuickActionItem preview = m_item;
    if (m_pathEdit) {
        preview.applicationReference = m_pathEdit->text().trimmed();
    }
    if (m_resetIconRequested) {
        preview.customIconPath.clear();
    } else if (!m_pendingIconSource.isEmpty()) {
        preview.customIconPath = m_pendingIconSource;
    }

    const QIcon icon = QuickActionItem::resolveIcon(preview, 24);
    m_iconPreview->setPixmap(icon.pixmap(QSize(24, 24)));
    m_resetIconBtn->setEnabled(!preview.customIconPath.isEmpty());
}

void QuickActionDialog::onAcceptClicked()
{
    const QString name = m_nameEdit ? m_nameEdit->text().trimmed() : m_item.displayName;
    if (m_item.type == QuickActionType::CustomApp) {
        const QString path = m_pathEdit ? m_pathEdit->text().trimmed() : m_item.applicationReference;
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            m_errorLabel->setText(QStringLiteral("Please choose an existing application file."));
            m_errorLabel->show();
            adjustSize();
            return;
        }
        m_item.applicationReference = path;
        m_item.displayName = name.isEmpty() ? ExternalAppLauncher::detectAppDisplayName(path) : name;
    } else {
        m_item.displayName = name.isEmpty() ? m_item.displayName : name;
    }

    if (m_resetIconRequested) {
        m_item.customIconPath.clear();
    } else if (!m_pendingIconSource.isEmpty()) {
        QString err;
        const QString imported = QuickActionItem::importCustomIcon(m_pendingIconSource, m_item.id, &err);
        if (imported.isEmpty()) {
            m_errorLabel->setText(err);
            m_errorLabel->show();
            adjustSize();
            return;
        }
        m_item.customIconPath = imported;
    }

    accept();
}

QuickActionItem QuickActionDialog::resultItem() const
{
    return m_item;
}

// ============================================================================
// QuickActionsEditor
// ============================================================================

QuickActionsEditor::QuickActionsEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 4, 0, 0);
    root->setSpacing(4);

    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(6);
    auto* titleLbl = new QLabel(QStringLiteral("Quick Actions"), this);
    titleLbl->setStyleSheet(QStringLiteral("font-weight: 700;"));
    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("muted"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerRow->addWidget(titleLbl);
    headerRow->addStretch(1);
    headerRow->addWidget(m_countLabel);
    root->addLayout(headerRow);

    auto* descLbl = new QLabel(QStringLiteral("Choose which apps appear when you double-click Chiikawa."), this);
    descLbl->setObjectName(QStringLiteral("muted"));
    descLbl->setWordWrap(true);
    root->addWidget(descLbl);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("quickActionsList"));
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSpacing(2);
    m_list->setFixedHeight(176);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget#quickActionsList {"
        "  background: rgba(14, 16, 22, 0.36);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "  padding: 3px;"
        "  outline: none;"
        "}"
        "QListWidget#quickActionsList::item {"
        "  background: transparent;"
        "  border-radius: 6px;"
        "  padding: 0px;"
        "}"
        "QListWidget#quickActionsList::item:selected {"
        "  background: rgba(255, 255, 255, 0.08);"
        "}"));
    connect(m_list->model(), &QAbstractItemModel::rowsMoved,
            this, &QuickActionsEditor::onRowsMoved);
    root->addWidget(m_list);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    m_addBtn = new QPushButton(QStringLiteral("+ Add Application"), this);
    m_addBtn->setObjectName(QStringLiteral("addQuickActionBtn"));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_addBtn, &QPushButton::clicked, this, &QuickActionsEditor::onAddApplicationClicked);

    m_restoreBtn = new QPushButton(QStringLiteral("Restore Defaults"), this);
    m_restoreBtn->setObjectName(QStringLiteral("restoreQuickActionsBtn"));
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_restoreBtn, &QPushButton::clicked, this, &QuickActionsEditor::onRestoreDefaultsClicked);

    btnRow->addWidget(m_addBtn, 1);
    btnRow->addWidget(m_restoreBtn);
    root->addLayout(btnRow);
}

void QuickActionsEditor::setShortcuts(const QVector<QuickActionItem>& items)
{
    m_items = QuickActionItem::sanitizeList(items);
    rebuildList();
}

int QuickActionsEditor::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            return i;
        }
    }
    return -1;
}

int QuickActionsEditor::enabledCount() const
{
    int count = 0;
    for (const auto& item : m_items) {
        if (item.enabled) {
            ++count;
        }
    }
    return count;
}

void QuickActionsEditor::updateCountLabel()
{
    if (!m_countLabel) {
        return;
    }
    m_countLabel->setText(QStringLiteral("%1/%2 active")
                              .arg(enabledCount())
                              .arg(QuickActionItem::kMaxVisibleRadialShortcuts));
    if (m_addBtn) {
        m_addBtn->setEnabled(m_items.size() < QuickActionItem::kMaxConfiguredShortcuts);
    }
}

void QuickActionsEditor::rebuildList()
{
    m_rebuilding = true;
    m_list->clear();

    for (const QuickActionItem& item : m_items) {
        auto* listItem = new QListWidgetItem(m_list);
        listItem->setData(Qt::UserRole, item.id);
        listItem->setSizeHint(QSize(0, 28));

        auto* rowWidget = new QWidget(m_list);
        rowWidget->setAttribute(Qt::WA_TranslucentBackground);
        auto* row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(6, 2, 4, 2);
        row->setSpacing(6);

        auto* grip = new QLabel(QStringLiteral("≡"), rowWidget);
        grip->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.42); font-size: 13px;"));
        grip->setFixedWidth(12);
        grip->setToolTip(QStringLiteral("Drag to reorder"));

        auto* iconLbl = new QLabel(rowWidget);
        iconLbl->setFixedSize(18, 18);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setPixmap(QuickActionItem::resolveIcon(item, 16).pixmap(QSize(16, 16)));

        auto* nameLbl = new QLabel(item.displayName, rowWidget);
        nameLbl->setStyleSheet(item.enabled
            ? QStringLiteral("color: #F4F6FA;")
            : QStringLiteral("color: rgba(244, 246, 250, 0.45);"));

        auto* enableCheck = new QCheckBox(rowWidget);
        enableCheck->setChecked(item.enabled);
        enableCheck->setCursor(Qt::PointingHandCursor);
        enableCheck->setFocusPolicy(Qt::NoFocus);
        enableCheck->setToolTip(item.enabled ? QStringLiteral("Disable shortcut") : QStringLiteral("Enable shortcut"));
        const QString itemId = item.id;
        connect(enableCheck, &QCheckBox::toggled, this, [this, itemId](bool checked) {
            if (!m_rebuilding) {
                emit userInteracted();
                setShortcutEnabled(itemId, checked);
            }
        });

        auto* moreBtn = new QPushButton(QStringLiteral("⋯"), rowWidget);
        moreBtn->setFixedSize(22, 22);
        moreBtn->setCursor(Qt::PointingHandCursor);
        moreBtn->setFocusPolicy(Qt::NoFocus);
        moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; color: rgba(244,246,250,0.72); border-radius: 5px; padding: 0px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.12); color: #FFFFFF; }"));
        connect(moreBtn, &QPushButton::clicked, this, [this, itemId, moreBtn]() {
            emit userInteracted();
            showRowMenu(itemId, moreBtn);
        });

        row->addWidget(grip);
        row->addWidget(iconLbl);
        row->addWidget(nameLbl, 1);
        row->addWidget(enableCheck);
        row->addWidget(moreBtn);

        m_list->addItem(listItem);
        m_list->setItemWidget(listItem, rowWidget);
    }

    updateCountLabel();
    m_rebuilding = false;
}

void QuickActionsEditor::setShortcutEnabled(const QString& id, bool enabled)
{
    const int idx = indexOfId(id);
    if (idx < 0) {
        return;
    }
    if (enabled && !m_items[idx].enabled
        && enabledCount() >= QuickActionItem::kMaxVisibleRadialShortcuts) {
        rebuildList();
        return;
    }
    if (m_items[idx].enabled == enabled) {
        return;
    }
    m_items[idx].enabled = enabled;
    rebuildList();
    emit shortcutsChanged(m_items);
}

void QuickActionsEditor::moveShortcut(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_items.size()
        || toIndex < 0 || toIndex >= m_items.size()
        || fromIndex == toIndex) {
        return;
    }
    const QuickActionItem moved = m_items.takeAt(fromIndex);
    m_items.insert(toIndex, moved);
    rebuildList();
    emit shortcutsChanged(m_items);
}

void QuickActionsEditor::addShortcut(const QuickActionItem& item)
{
    if (m_items.size() >= QuickActionItem::kMaxConfiguredShortcuts) {
        return;
    }
    QuickActionItem next = item;
    if (next.enabled && enabledCount() >= QuickActionItem::kMaxVisibleRadialShortcuts) {
        next.enabled = false;
    }
    m_items.push_back(next);
    m_items = QuickActionItem::sanitizeList(m_items);
    rebuildList();
    emit shortcutsChanged(m_items);
}

void QuickActionsEditor::updateShortcut(const QuickActionItem& item)
{
    const int idx = indexOfId(item.id);
    if (idx < 0) {
        return;
    }
    m_items[idx] = item;
    m_items = QuickActionItem::sanitizeList(m_items);
    rebuildList();
    emit shortcutsChanged(m_items);
}

void QuickActionsEditor::removeShortcut(const QString& id)
{
    const int idx = indexOfId(id);
    if (idx < 0 || m_items[idx].isBuiltIn()) {
        return;
    }
    m_items.removeAt(idx);
    rebuildList();
    emit shortcutsChanged(m_items);
}

void QuickActionsEditor::onRowsMoved()
{
    if (m_rebuilding) {
        return;
    }
    QVector<QuickActionItem> reordered;
    reordered.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        const QString id = m_list->item(i)->data(Qt::UserRole).toString();
        const int oldIdx = indexOfId(id);
        if (oldIdx >= 0) {
            reordered.push_back(m_items[oldIdx]);
        }
    }
    if (reordered.size() == m_items.size() && reordered != m_items) {
        m_items = std::move(reordered);
        emit userInteracted();
        rebuildList();
        emit shortcutsChanged(m_items);
    }
}

void QuickActionsEditor::showRowMenu(const QString& id, QWidget* anchor)
{
    const int idx = indexOfId(id);
    if (idx < 0) {
        return;
    }
    const QuickActionItem& item = m_items[idx];

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: rgba(20, 22, 30, 0.94); border: 1px solid rgba(255,255,255,0.14); border-radius: 8px; padding: 4px; color: #F4F6FA; }"
        "QMenu::item { padding: 5px 14px; border-radius: 5px; }"
        "QMenu::item:selected { background: rgba(255,255,255,0.14); }"));

    QAction* moveUp = menu.addAction(QStringLiteral("Move Up"));
    moveUp->setEnabled(idx > 0);
    connect(moveUp, &QAction::triggered, this, [this, idx]() { moveShortcut(idx, idx - 1); });

    QAction* moveDown = menu.addAction(QStringLiteral("Move Down"));
    moveDown->setEnabled(idx + 1 < m_items.size());
    connect(moveDown, &QAction::triggered, this, [this, idx]() { moveShortcut(idx, idx + 1); });

    menu.addSeparator();

    QAction* editAct = menu.addAction(item.isBuiltIn() ? QStringLiteral("Customize...") : QStringLiteral("Edit..."));
    connect(editAct, &QAction::triggered, this, [this, id]() { editShortcut(id); });

    QAction* iconAct = menu.addAction(QStringLiteral("Change Icon..."));
    connect(iconAct, &QAction::triggered, this, [this, id]() { changeShortcutIcon(id); });

    if (!item.customIconPath.isEmpty()) {
        QAction* resetIconAct = menu.addAction(QStringLiteral("Reset Icon"));
        connect(resetIconAct, &QAction::triggered, this, [this, id]() { resetShortcutIcon(id); });
    }

    if (!item.isBuiltIn()) {
        menu.addSeparator();
        QAction* removeAct = menu.addAction(QStringLiteral("Remove"));
        connect(removeAct, &QAction::triggered, this, [this, id]() { removeShortcut(id); });
    }

    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

void QuickActionsEditor::editShortcut(const QString& id)
{
    const int idx = indexOfId(id);
    if (idx < 0) {
        return;
    }
    QuickActionDialog dlg(&m_items[idx], window());
    if (dlg.exec() == QDialog::Accepted) {
        updateShortcut(dlg.resultItem());
    }
}

void QuickActionsEditor::changeShortcutIcon(const QString& id)
{
    const int idx = indexOfId(id);
    if (idx < 0) {
        return;
    }
    const QString picDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString file = QFileDialog::getOpenFileName(
        window(),
        QStringLiteral("Choose Custom Icon"),
        picDir,
        QStringLiteral("Images (*.png *.ico *.icns *.jpg *.jpeg *.webp *.bmp)"));
    if (file.isEmpty()) {
        return;
    }
    QString err;
    const QString imported = QuickActionItem::importCustomIcon(file, id, &err);
    if (imported.isEmpty()) {
        QMessageBox::warning(window(), QStringLiteral("Invalid Icon"), err);
        return;
    }
    QuickActionItem updated = m_items[idx];
    updated.customIconPath = imported;
    updateShortcut(updated);
}

void QuickActionsEditor::resetShortcutIcon(const QString& id)
{
    const int idx = indexOfId(id);
    if (idx < 0) {
        return;
    }
    QuickActionItem updated = m_items[idx];
    updated.customIconPath.clear();
    updateShortcut(updated);
}

void QuickActionsEditor::onAddApplicationClicked()
{
    emit userInteracted();
    QuickActionDialog dlg(nullptr, window());
    if (dlg.exec() == QDialog::Accepted) {
        addShortcut(dlg.resultItem());
    }
}

void QuickActionsEditor::onRestoreDefaultsClicked()
{
    emit userInteracted();
    bool hasCustom = false;
    for (const auto& item : m_items) {
        if (!item.isBuiltIn() || !item.customIconPath.isEmpty()) {
            hasCustom = true;
            break;
        }
    }
    if (hasCustom) {
        const auto reply = QMessageBox::question(
            window(),
            QStringLiteral("Restore Default Shortcuts"),
            QStringLiteral("Reset quick actions to the 6 default shortcuts and remove custom shortcuts?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    m_items = QuickActionItem::defaultShortcuts();
    rebuildList();
    emit shortcutsChanged(m_items);
}
