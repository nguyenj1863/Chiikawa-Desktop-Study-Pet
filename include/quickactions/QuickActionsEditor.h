#pragma once

#include "quickactions/InstalledAppDiscovery.h"
#include "quickactions/QuickActionItem.h"

#include <QDialog>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMouseEvent;
class QPushButton;

/**
 * Reusable, draggable, non-modal dark translucent window for adding or
 * editing a quick-action shortcut.
 *
 * - Floats independently of Settings: dragging either window never moves the
 *   other, and Settings stays interactive while the dialog is open.
 * - Draggable by its header bar (and non-interactive areas), clamped to the
 *   active monitor's available geometry across multi-monitor setups.
 * - Paints a per-pixel translucent glass surface, so overlapping Settings
 *   simply composites into a darker intersection.
 * - Includes an asynchronous, cached, searchable Installed Applications picker
 *   with a "Browse for application…" fallback.
 */
class QuickActionDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickActionDialog(const QuickActionItem* existing = nullptr,
                               QWidget* parent = nullptr);

    [[nodiscard]] QuickActionItem resultItem() const;
    [[nodiscard]] bool isEditing() const { return m_isEdit; }

    /** Initial placement beside anchorGlobalRect (e.g. Settings), without anchoring to it. */
    void placeBeside(const QRect& anchorGlobalRect);

    /** Programmatic accessors for automated UI tests. */
    [[nodiscard]] QWidget* headerWidget() const { return m_headerWidget; }
    [[nodiscard]] QLineEdit* searchEdit() const { return m_searchEdit; }
    [[nodiscard]] QListWidget* detectedAppsList() const { return m_appsList; }
    [[nodiscard]] QLineEdit* nameEdit() const { return m_nameEdit; }
    [[nodiscard]] QLineEdit* pathEdit() const { return m_pathEdit; }
    void selectDetectedAppRow(int row);
    void submit() { onAcceptClicked(); }

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onRefreshAppsClicked();
    void onDetectedAppClicked(QListWidgetItem* item);
    void onBrowseApplication();
    void onChooseCustomIcon();
    void onResetCustomIcon();
    void onAcceptClicked();

private:
    void loadInstalledApps(bool forceRefresh);
    void populateAppsList(const QString& query);
    void refreshIconPreview();
    [[nodiscard]] bool isInteractiveChildAt(const QPoint& pos) const;

    QuickActionItem m_item;
    bool m_isEdit = false;
    QString m_pendingIconSource;
    bool m_resetIconRequested = false;

    QVector<InstalledAppDiscovery::DetectedApp> m_allDetectedApps;
    QVector<InstalledAppDiscovery::DetectedApp> m_filteredApps;

    QWidget* m_headerWidget = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_refreshAppsBtn = nullptr;
    QListWidget* m_appsList = nullptr;
    QLabel* m_appsStatusLabel = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QLabel* m_iconPreview = nullptr;
    QPushButton* m_changeIconBtn = nullptr;
    QPushButton* m_resetIconBtn = nullptr;
    QLabel* m_errorLabel = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};

/**
 * Compact drag-and-drop shortcut manager embedded inside Settings -> Quick Actions.
 * Allows enabling/disabling, drag reordering, Move Up/Down, adding custom apps,
 * editing/removing shortcuts, importing custom icons, and restoring defaults.
 */
class QuickActionsEditor : public QWidget {
    Q_OBJECT

public:
    explicit QuickActionsEditor(QWidget* parent = nullptr);

    void setShortcuts(const QVector<QuickActionItem>& items);
    [[nodiscard]] QVector<QuickActionItem> shortcuts() const { return m_items; }
    [[nodiscard]] int enabledShortcutCount() const { return enabledCount(); }

    /** Programmatic helpers (also used by automated tests). */
    void setShortcutEnabled(const QString& id, bool enabled);
    void moveShortcut(int fromIndex, int toIndex);
    void addShortcut(const QuickActionItem& item);
    void updateShortcut(const QuickActionItem& item);
    void removeShortcut(const QString& id);

    /** Opens the non-modal Add/Edit window, or raises the one already open. */
    void openAddDialog();
    void openEditDialog(const QString& id);
    [[nodiscard]] QuickActionDialog* activeDialog() const { return m_activeDialog; }

    /** Cancels any open Add/Edit window without applying its pending changes. */
    void closeActiveDialog();

signals:
    void shortcutsChanged(const QVector<QuickActionItem>& items);
    void userInteracted();

private slots:
    void onAddApplicationClicked();
    void onRestoreDefaultsClicked();
    void onRowsMoved();

private:
    void rebuildList();
    void updateCountLabel();
    void showRowMenu(const QString& id, QWidget* anchor);
    void openDialog(const QuickActionItem* existing);
    void changeShortcutIcon(const QString& id);
    void resetShortcutIcon(const QString& id);
    [[nodiscard]] int indexOfId(const QString& id) const;
    [[nodiscard]] int enabledCount() const;

    QVector<QuickActionItem> m_items;
    QLabel* m_countLabel = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;
    QPointer<QuickActionDialog> m_activeDialog;
    bool m_rebuilding = false;
};
