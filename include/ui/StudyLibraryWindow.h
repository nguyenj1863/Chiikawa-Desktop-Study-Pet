#pragma once

#include "ai/OllamaClient.h"
#include "study/StudyTypes.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

class AppSettings;
class PixelCheckBox;
class PixelIconButton;
class PixelOptionButton;
class ProgressTracker;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QuestionGenerator;
class StudyLibrary;
class StudySetList;
class UISoundManager;

/**
 * Compact smoked-glass window for managing study materials: studySets, imported
 * documents (with processing status and unsupported pages), question generation,
 * question-format preferences, and the local AI (Ollama) setup. Everything runs
 * locally; the only network actions are opening the Ollama download page in the
 * browser and asking a local Ollama to download a model, both only on request.
 */
class StudyLibraryWindow : public QWidget {
    Q_OBJECT

public:
    static constexpr int kWidth = 384;

    StudyLibraryWindow(AppSettings* settings,
                       StudyLibrary* library,
                       QuestionGenerator* generator,
                       OllamaClient* ollama,
                       ProgressTracker* progress,
                       UISoundManager* uiSound = nullptr,
                       QWidget* parent = nullptr);

    /** Shows the window beside the pet (clamped to its screen) and re-checks the AI setup. */
    void showNear(const QRect& petGlobalGeometry);

    [[nodiscard]] qint64 currentSet() const;
    [[nodiscard]] QString currentSetName() const;
    /** Opens a set's materials (same as clicking its row). */
    void openSet(qint64 setId);
    void selectSet(qint64 setId);
    /** Back to the list of sets. */
    void showSetList();
    [[nodiscard]] bool isShowingSet() const;
    /** Creates a study set and selects it; returns its ID or 0. */
    qint64 createSet(const QString& name);
    bool renameCurrentSet(const QString& name);
    bool renameSet(qint64 id, const QString& name);
    /** Asks for confirmation unless confirmed is true. */
    bool removeSet(qint64 id, bool confirmed = false);
    /** Asks for confirmation unless confirmed is true. */
    bool removeCurrentSet(bool confirmed = false);
    void importFiles(const QStringList& paths);
    /** Generates for checked documents. Regenerate asks for confirmation unless confirmed is true. */
    bool generate(bool regenerate, bool confirmed = false);
    bool removeDocument(qint64 documentId, bool confirmed = false);

    void refresh();

    // Accessors for tests
    [[nodiscard]] StudySetList* setList() const { return m_setList; }
    [[nodiscard]] QLabel* titleLabel() const { return m_titleLabel; }
    [[nodiscard]] QPushButton* backButton() const { return m_backBtn; }
    [[nodiscard]] QPushButton* newSetButton() const { return m_newSetBtn; }
    [[nodiscard]] QLabel* importTypesLabel() const { return m_importTypes; }
    [[nodiscard]] QPushButton* importPptxButton() const { return m_importPptxBtn; }
    [[nodiscard]] QPushButton* importPdfButton() const { return m_importPdfBtn; }
    [[nodiscard]] QPushButton* importTextButton() const { return m_importTextBtn; }
    [[nodiscard]] QPushButton* formatsToggle() const { return m_formatsToggle; }
    [[nodiscard]] bool formatsExpanded() const { return m_formatsBody && !m_formatsBody->isHidden(); }
    [[nodiscard]] QFrame* setupCard() const { return m_setupCard; }
    [[nodiscard]] QComboBox* modelCombo() const { return m_modelCombo; }
    [[nodiscard]] QLabel* setupLabel() const { return m_setupLabel; }
    [[nodiscard]] QLabel* statusLabel() const { return m_statusLabel; }
    [[nodiscard]] QPushButton* importButton() const { return m_importBtn; }
    [[nodiscard]] QPushButton* generateButton() const { return m_generateBtn; }
    [[nodiscard]] QPushButton* regenerateButton() const { return m_regenerateBtn; }
    [[nodiscard]] QPushButton* cancelButton() const { return m_cancelBtn; }
    [[nodiscard]] QPushButton* getOllamaButton() const { return m_getOllamaBtn; }
    [[nodiscard]] QPushButton* startOllamaButton() const { return m_startOllamaBtn; }
    [[nodiscard]] QPushButton* downloadModelButton() const { return m_downloadBtn; }
    [[nodiscard]] QPushButton* recheckButton() const { return m_recheckBtn; }
    [[nodiscard]] QProgressBar* progressBar() const { return m_progress; }
    [[nodiscard]] PixelCheckBox* formatCheck(int formatBit) const;
    [[nodiscard]] PixelOptionButton* mixedModeButton() const { return m_mixedBtn; }
    [[nodiscard]] PixelOptionButton* focusModeButton() const { return m_focusBtn; }
    [[nodiscard]] QComboBox* focusCombo() const { return m_focusCombo; }
    [[nodiscard]] QList<qint64> documentRows() const;
    [[nodiscard]] PixelCheckBox* documentCheck(qint64 documentId) const;
    [[nodiscard]] QLabel* documentMeta(qint64 documentId) const;

signals:
    void setChanged(qint64 setId);
    void preferencesChanged();
    void buttonActivated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    void reloadSets(qint64 selectId);
    void rebuildSetList();
    void applyView();
    void setFormatsExpanded(bool expanded);
    void updateFormatsSummary();
    [[nodiscard]] QString setName(qint64 id) const;
    void rebuildDocuments();
    void updateActions();
    void updateSetupBanner(OllamaClient::SetupState state);
    void syncFormatControls();
    void applyFormatControls();
    void onImportClicked(const QString& filter);
    void onDownloadClicked();
    void beginSetEdit(bool rename, qint64 id);
    void finishSetEdit(bool accept);
    void click();
    [[nodiscard]] QList<qint64> checkedDocuments() const;
    [[nodiscard]] bool confirm(const QString& title, const QString& body, const QString& confirmText);

    AppSettings* m_settings = nullptr;
    StudyLibrary* m_library = nullptr;
    QuestionGenerator* m_generator = nullptr;
    QPointer<OllamaClient> m_ollama;
    ProgressTracker* m_tracker = nullptr;
    UISoundManager* m_uiSound = nullptr;

    // Setup banner
    QFrame* m_setupCard = nullptr;
    QLabel* m_setupLabel = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QWidget* m_modelRow = nullptr;
    QPushButton* m_getOllamaBtn = nullptr;
    QPushButton* m_startOllamaBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_cancelDownloadBtn = nullptr;
    QPushButton* m_recheckBtn = nullptr;
    QProgressBar* m_downloadProgress = nullptr;

    // Header and navigation
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_backBtn = nullptr;
    QStackedWidget* m_stack = nullptr;
    qint64 m_currentSet = 0;
    bool m_showingSet = false;

    // Study set list
    StudySetList* m_setList = nullptr;
    QLabel* m_setsEmpty = nullptr;
    QPushButton* m_newSetBtn = nullptr;
    QWidget* m_setEditRow = nullptr;
    QLineEdit* m_setEdit = nullptr;
    bool m_renaming = false;
    qint64 m_editTarget = 0;

    // Importing
    QLabel* m_importTypes = nullptr;
    QPushButton* m_importPptxBtn = nullptr;
    QPushButton* m_importPdfBtn = nullptr;
    QPushButton* m_importTextBtn = nullptr;

    // Formats
    QPushButton* m_formatsToggle = nullptr;
    QWidget* m_formatsBody = nullptr;
    QHash<int, PixelCheckBox*> m_formatChecks;
    PixelOptionButton* m_mixedBtn = nullptr;
    PixelOptionButton* m_focusBtn = nullptr;
    QComboBox* m_focusCombo = nullptr;
    QLabel* m_formatHint = nullptr;

    // Documents
    QScrollArea* m_docScroll = nullptr;
    QWidget* m_docList = nullptr;
    QVBoxLayout* m_docLayout = nullptr;
    QHash<qint64, QFrame*> m_docRows;
    QHash<qint64, PixelCheckBox*> m_docChecks;
    QHash<qint64, QLabel*> m_docMeta;
    QSet<qint64> m_expandedDocs;
    QLabel* m_emptyLabel = nullptr;

    // Actions
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_generateBtn = nullptr;
    QPushButton* m_regenerateBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;

    bool m_syncing = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
