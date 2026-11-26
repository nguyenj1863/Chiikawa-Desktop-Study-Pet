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
 * Reusable, draggable, overlap-aware dark translucent dialog for adding or
 * editing a quick-action shortcut.
 *
 * - Draggable by its header bar (and non-interactive areas), clamped to the
 *   active monitor's available geometry across multi-monitor setups.
 * - Locally darkens its translucent glass backdrop wherever it overlaps the
 *   anchor Settings window so controls remain crisp and readable while dragging.
 * - Includes an asynchronous, cached, searchable Installed Applications picker
 *   with a "Browse for application…" fallback.
 */
class QuickActionDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickActionDialog(const QuickActionItem* existing = nullptr,
                               QWidget* anchorWindow = nullptr,
                               QWidget* parent = nullptr);

    [[nodiscard]] QuickActionItem resultItem() const;

    /** Positions the dialog beside anchorWindow without unnecessarily covering it. */
    void placeNearAnchor(QWidget* anchorWindow);

    /** Returns the dialog-local rectangle currently overlapping the anchor window. */
    [[nodiscard]] QRect overlapRectLocal() const;

    /** Programmatic accessors for automated UI tests. */
    [[nodiscard]] QWidget* headerWidget() const { return m_headerWidget; }
    [[nodiscard]] QLineEdit* searchEdit() const { return m_searchEdit; }
    [[nodiscard]] QListWidget* detectedAppsList() const { return m_appsList; }
    [[nodiscard]] QLineEdit* nameEdit() const { return m_nameEdit; }
    [[nodiscard]] QLineEdit* pathEdit() const { return m_pathEdit; }
    void selectDetectedAppRow(int row);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

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

    QPointer<QWidget> m_overlapAnchor;
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
    void editShortcut(const QString& id);
    void changeShortcutIcon(const QString& id);
    void resetShortcutIcon(const QString& id);
    [[nodiscard]] int indexOfId(const QString& id) const;
    [[nodiscard]] int enabledCount() const;

    QVector<QuickActionItem> m_items;
    QLabel* m_countLabel = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;
    bool m_rebuilding = false;
};
