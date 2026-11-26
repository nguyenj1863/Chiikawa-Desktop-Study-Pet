#pragma once

#include "QuickActionItem.h"

#include <QDialog>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/**
 * Compact dark translucent dialog for adding or editing a quick-action shortcut.
 * Supports browsing native Windows (.exe/.lnk) or macOS (.app) applications,
 * automatic display-name and icon detection, and optional custom icon import.
 */
class QuickActionDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickActionDialog(const QuickActionItem* existing = nullptr,
                               QWidget* parent = nullptr);

    [[nodiscard]] QuickActionItem resultItem() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onBrowseApplication();
    void onChooseCustomIcon();
    void onResetCustomIcon();
    void onAcceptClicked();

private:
    void refreshIconPreview();

    QuickActionItem m_item;
    bool m_isEdit = false;
    QString m_pendingIconSource;
    bool m_resetIconRequested = false;

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QLabel* m_iconPreview = nullptr;
    QPushButton* m_changeIconBtn = nullptr;
    QPushButton* m_resetIconBtn = nullptr;
    QLabel* m_errorLabel = nullptr;
};

/**
 * Compact drag-and-drop shortcut manager embedded inside Settings -> Pet Preferences.
 * Allows enabling/disabling, drag reordering, Move Up/Down, adding custom apps,
 * editing/removing shortcuts, importing custom icons, and restoring defaults.
 */
class QuickActionsEditor : public QWidget {
    Q_OBJECT

public:
    explicit QuickActionsEditor(QWidget* parent = nullptr);

    void setShortcuts(const QVector<QuickActionItem>& items);
    [[nodiscard]] QVector<QuickActionItem> shortcuts() const { return m_items; }

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
