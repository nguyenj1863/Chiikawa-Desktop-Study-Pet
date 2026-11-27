#include "ui/StudyLibraryWindow.h"

#ifdef Q_OS_MACOS
#  include "platform/MacWindowHelper.h"
#endif
#include "alarm/UISoundManager.h"
#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "study/DocumentExtractor.h"
#include "study/ProgressTracker.h"
#include "study/QuestionGenerator.h"
#include "study/QuestionSelector.h"
#include "study/StudyLibrary.h"
#include "ui/PixelTheme.h"
#include "ui/PixelSegmentedControl.h"
#include "ui/QuestionEditor.h"
#include "ui/StudyPracticeView.h"
#include "ui/StudyPreferencesPage.h"
#include "ui/StudySetList.h"
#include "ui/StudyUiHelpers.h"

#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using namespace StudyUi;

namespace {

QString typeTag(QuestionType type)
{
    switch (type) {
    case QuestionType::SingleChoice:
        return QStringLiteral("Choice");
    case QuestionType::MultiSelect:
        return QStringLiteral("Select all");
    case QuestionType::Written:
        return QStringLiteral("Written");
    }
    return QStringLiteral("Question");
}

bool supportedSuffix(const QString& suffix)
{
    const QString s = suffix.toLower();
    return s == QLatin1String("pptx") || s == QLatin1String("txt") || s == QLatin1String("md")
        || s == QLatin1String("markdown") || (s == QLatin1String("pdf") && DocumentExtractor::pdfSupported());
}

QString skippedMessage(const QStringList& rejected, qsizetype skipped)
{
    if (rejected.isEmpty()) {
        return QStringLiteral("%1 file%2 skipped.").arg(skipped).arg(skipped == 1 ? QString() : QStringLiteral("s"));
    }
    QStringList shown = rejected.mid(0, 3);
    QString names = shown.join(QStringLiteral(", "));
    if (rejected.size() > 3) {
        names += QStringLiteral(" and %1 more").arg(rejected.size() - 3);
    }
    QString message = QStringLiteral("Can't import %1. Use PowerPoint (.pptx), PDF, text (.txt), or Markdown (.md) files.").arg(names);
    for (const QString& name : rejected) {
        const QString ext = QFileInfo(name).suffix().toLower();
        if (ext == QLatin1String("ppt") || ext == QLatin1String("doc") || ext == QLatin1String("docx")
            || ext == QLatin1String("odp") || ext == QLatin1String("pages") || ext == QLatin1String("key")) {
            message += QStringLiteral(" Save older Office or Keynote files as .pptx or PDF first.");
            break;
        }
    }
    return message;
}

QString unitName(DocumentKind kind, int count)
{
    if (kind == DocumentKind::Pptx) {
        return count == 1 ? QStringLiteral("slide") : QStringLiteral("slides");
    }
    if (kind == DocumentKind::Text) {
        return count == 1 ? QStringLiteral("section") : QStringLiteral("sections");
    }
    return count == 1 ? QStringLiteral("page") : QStringLiteral("pages");
}

} // namespace

StudyLibraryWindow::StudyLibraryWindow(AppSettings* settings, StudyLibrary* library, QuestionGenerator* generator,
                                       OllamaClient* ollama, ProgressTracker* progress, UISoundManager* uiSound,
                                       QWidget* parent)
#ifdef Q_OS_MACOS
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
#else
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
#endif
    , m_settings(settings)
    , m_library(library)
    , m_generator(generator)
    , m_ollama(ollama)
    , m_tracker(progress)
    , m_uiSound(uiSound)
{
    PixelTheme::ensureFontsLoaded();
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kWidth);
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("Study Library"));
    setStyleSheet(PixelTheme::styleSheet()
                  + QStringLiteral("QScrollArea#docScroll, QWidget#docList, QWidget#setList { background: transparent; border: none; }"));
    buildUi();

    if (m_library) {
        connect(m_library, &StudyLibrary::documentsChanged, this, &StudyLibraryWindow::refresh);
        connect(m_library, &StudyLibrary::documentProgress, this, [this](qint64 id, const QString& stage) {
            const auto doc = m_library->database()->document(id);
            m_statusLabel->setText(QStringLiteral("%1: %2…").arg(doc ? doc->title : QStringLiteral("Document"), stage));
        });
        connect(m_library, &StudyLibrary::documentProcessed, this, [this](qint64 id, DocumentStatus status, const QString& note) {
            const auto doc = m_library->database()->document(id);
            const QString title = doc ? doc->title : QStringLiteral("Document");
            if (status == DocumentStatus::Extracted) {
                m_statusLabel->setText(note.isEmpty() ? QStringLiteral("%1 is ready for questions.").arg(title)
                                                      : QStringLiteral("%1: %2").arg(title, note));
            } else {
                m_statusLabel->setText(QStringLiteral("%1: %2").arg(title, note));
            }
            refresh();
        });
        connect(m_library, &StudyLibrary::busyChanged, this, [this](bool) { updateActions(); });
    }
    if (m_generator) {
        connect(m_generator, &QuestionGenerator::progress, this, [this](int done, int total, const QString& status) {
            m_progress->setRange(0, std::max(1, total));
            m_progress->setValue(done);
            m_progress->show();
            m_statusLabel->setText(status);
            updateActions();
        });
        connect(m_generator, &QuestionGenerator::documentFinished, this, [this](qint64, int, int) { refresh(); });
        connect(m_generator, &QuestionGenerator::finished, this, [this](int accepted, int rejected, const QString& message) {
            m_progress->hide();
            if (!message.isEmpty()) {
                m_statusLabel->setText(message);
            } else if (accepted == 0 && rejected > 0) {
                m_statusLabel->setText(QStringLiteral("No questions passed the source check this time. Try Generate again."));
            } else {
                m_statusLabel->setText(QStringLiteral("Added %1 question%2.%3")
                                           .arg(accepted)
                                           .arg(accepted == 1 ? QString() : QStringLiteral("s"))
                                           .arg(rejected > 0 ? QStringLiteral(" %1 were not grounded well enough and were skipped.").arg(rejected)
                                                             : QString()));
            }
            refresh();
        });
    }
    if (m_ollama) {
        connect(m_ollama, &OllamaClient::setupStateChanged, this, &StudyLibraryWindow::updateSetupBanner);
        connect(m_ollama, &OllamaClient::pullProgress, this, [this](qint64 completed, qint64 total, const QString& status) {
            if (total > 0) {
                m_downloadProgress->setRange(0, 1000);
                m_downloadProgress->setValue(static_cast<int>(completed * 1000 / total));
                m_setupLabel->setText(QStringLiteral("Downloading %1… %2%")
                                          .arg(m_ollama->model())
                                          .arg(completed * 100 / total));
            } else {
                m_downloadProgress->setRange(0, 0);
                m_setupLabel->setText(QStringLiteral("Downloading %1… (%2)").arg(m_ollama->model(), status));
            }
        });
        connect(m_ollama, &OllamaClient::pullFinished, this, [this](bool ok, const QString& message) {
            m_downloadProgress->hide();
            m_cancelDownloadBtn->hide();
            m_statusLabel->setText(message);
            if (!ok) {
                updateSetupBanner(m_ollama->setupState());
            }
        });
    }
    reloadSets(m_settings ? m_settings->studySetId() : 0);
    m_prefs->syncFromSettings();
    updateFormatsHint();
    updateSetupBanner(m_ollama ? m_ollama->setupState() : OllamaClient::SetupState::NotInstalled);
    hide();
}

void StudyLibraryWindow::buildUi()
{
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(15, 13, 15, 17);
    shell->setSpacing(7);

    // Header
    auto* header = new QHBoxLayout();
    header->setContentsMargins(2, 0, 0, 0);
    m_backBtn = makeButton(QStringLiteral("‹ Sets"), this, "chip");
    m_backBtn->setToolTip(QStringLiteral("Back to all study sets"));
    m_backBtn->hide();
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        click();
        if (m_practicing) {
            m_practice->abandon();
            m_practicing = false;
            applyView();
            refresh();
        } else {
            showSetList();
        }
    });
    auto* title = new QLabel(QStringLiteral("Study Library"), this);
    title->setObjectName(QStringLiteral("title"));
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_titleLabel = title;
    auto* closeBtn = new PixelIconButton(PixelIcon::Close, 30, 11, false, this);
    closeBtn->setToolTip(QStringLiteral("Close Study Library"));
    closeBtn->setAccessibleName(QStringLiteral("Close Study Library"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        click();
        hide();
    });
    header->addWidget(m_backBtn);
    header->addWidget(title, 1);
    header->addWidget(closeBtn);
    shell->addLayout(header);

    m_tabs = new PixelSegmentedControl(this);
    m_tabs->addTab(QStringLiteral("Library"));
    m_tabs->addTab(QStringLiteral("Preferences"));
    shell->addWidget(m_tabs);
    connect(m_tabs, &PixelSegmentedControl::tabClicked, this, [this](int) { click(); });

    // Local AI setup banner
    m_setupCard = makeCard(this);
    auto* setup = new QVBoxLayout(m_setupCard);
    setup->setContentsMargins(10, 8, 10, 9);
    setup->setSpacing(6);
    m_setupLabel = makeLabel(QString(), "sectionSummary", m_setupCard);
    setup->addWidget(m_setupLabel);
    m_downloadProgress = new QProgressBar(m_setupCard);
    m_downloadProgress->setTextVisible(false);
    m_downloadProgress->hide();
    setup->addWidget(m_downloadProgress);
    auto* setupButtons = new QHBoxLayout();
    setupButtons->setSpacing(6);
    m_getOllamaBtn = makeButton(QStringLiteral("Get Ollama"), m_setupCard, "primary");
    m_getOllamaBtn->setToolTip(QStringLiteral("Opens ollama.com/download in your browser"));
    m_startOllamaBtn = makeButton(QStringLiteral("Start Ollama"), m_setupCard, "primary");
    m_downloadBtn = makeButton(QStringLiteral("Download Model"), m_setupCard, "primary");
    m_cancelDownloadBtn = makeButton(QStringLiteral("Cancel"), m_setupCard);
    m_recheckBtn = makeButton(QStringLiteral("Recheck"), m_setupCard);
    setupButtons->addStretch(1);
    for (QPushButton* b : {m_recheckBtn, m_cancelDownloadBtn, m_getOllamaBtn, m_startOllamaBtn, m_downloadBtn}) {
        setupButtons->addWidget(b);
    }
    m_cancelDownloadBtn->hide();
    setup->addLayout(setupButtons);
    shell->addWidget(m_setupCard);
    connect(m_getOllamaBtn, &QPushButton::clicked, this, [this]() {
        click();
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(OllamaClient::kDownloadPage)));
        m_statusLabel->setText(QStringLiteral("After installing Ollama, open it and press Recheck."));
    });
    connect(m_startOllamaBtn, &QPushButton::clicked, this, [this]() {
        click();
        if (m_ollama && m_ollama->startOllama()) {
            m_setupLabel->setText(QStringLiteral("Starting Ollama…"));
        } else {
            m_statusLabel->setText(QStringLiteral("Ollama could not be started. Open it from your apps, then press Recheck."));
        }
    });
    connect(m_downloadBtn, &QPushButton::clicked, this, &StudyLibraryWindow::onDownloadClicked);
    connect(m_cancelDownloadBtn, &QPushButton::clicked, this, [this]() {
        click();
        if (m_ollama) {
            m_ollama->cancelPull();
        }
    });
    connect(m_recheckBtn, &QPushButton::clicked, this, [this]() {
        click();
        if (m_ollama) {
            m_ollama->checkSetup();
        }
    });

    // Page 0: the list of study sets
    m_stack = new QStackedWidget(this);
    auto* listPage = new QWidget(m_stack);
    auto* listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);

    auto* listHeader = new QHBoxLayout();
    listHeader->setSpacing(6);
    listHeader->addWidget(makeLabel(QStringLiteral("Your study sets"), "sectionTitle", listPage), 1);
    m_newSetBtn = makeButton(QStringLiteral("New set"), listPage, "primary");
    listHeader->addWidget(m_newSetBtn);
    listLayout->addLayout(listHeader);
    connect(m_newSetBtn, &QPushButton::clicked, this, [this]() {
        click();
        beginSetEdit(false, 0);
    });

    m_setEditRow = new QWidget(listPage);
    auto* editLayout = new QHBoxLayout(m_setEditRow);
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->setSpacing(6);
    m_setEdit = new QLineEdit(m_setEditRow);
    m_setEdit->setObjectName(QStringLiteral("setNameEdit"));
    m_setEdit->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_setEdit->setMaxLength(60);
    m_setEdit->setPlaceholderText(QStringLiteral("Set name, e.g. CS 340 Databases"));
    auto* saveSet = makeButton(QStringLiteral("Save"), m_setEditRow, "primary");
    auto* cancelSet = makeButton(QStringLiteral("Cancel"), m_setEditRow);
    editLayout->addWidget(m_setEdit, 1);
    editLayout->addWidget(cancelSet);
    editLayout->addWidget(saveSet);
    m_setEditRow->hide();
    listLayout->addWidget(m_setEditRow);
    connect(saveSet, &QPushButton::clicked, this, [this]() {
        click();
        finishSetEdit(true);
    });
    connect(cancelSet, &QPushButton::clicked, this, [this]() {
        click();
        finishSetEdit(false);
    });
    connect(m_setEdit, &QLineEdit::returnPressed, this, [this]() { finishSetEdit(true); });

    m_setsEmpty = makeLabel(QStringLiteral("No study sets yet. Create one to import your notes and slides, then generate practice questions."),
                            "muted", listPage);
    listLayout->addWidget(m_setsEmpty);
    auto* setScroll = new QScrollArea(listPage);
    setScroll->setObjectName(QStringLiteral("docScroll"));
    setScroll->setWidgetResizable(true);
    setScroll->setFrameShape(QFrame::NoFrame);
    setScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setScroll->viewport()->setAutoFillBackground(false);
    m_setList = new StudySetList(setScroll);
    setScroll->setWidget(m_setList);
    listLayout->addWidget(setScroll, 1);
    connect(m_setList, &StudySetList::openRequested, this, [this](qint64 id) {
        click();
        openSet(id);
    });
    connect(m_setList, &StudySetList::renameRequested, this, [this](qint64 id) {
        click();
        beginSetEdit(true, id);
    });
    connect(m_setList, &StudySetList::deleteRequested, this, [this](qint64 id) {
        click();
        removeSet(id, false);
    });
    m_stack->addWidget(listPage);

    // Page 1: one set's materials
    auto* detailPage = new QWidget(m_stack);
    auto* detail = new QVBoxLayout(detailPage);
    detail->setContentsMargins(0, 0, 0, 0);
    detail->setSpacing(7);

    // Importing: say which files work
    auto* importCard = makeCard(detailPage);
    auto* importLayout = new QVBoxLayout(importCard);
    importLayout->setContentsMargins(10, 8, 10, 9);
    importLayout->setSpacing(5);
    importLayout->addWidget(makeLabel(QStringLiteral("Add study materials"), "sectionTitle", importCard));
    const bool pdf = DocumentExtractor::pdfSupported();
    m_importTypes = makeLabel(QStringLiteral("PowerPoint (.pptx) · %1 · Text (.txt) · Markdown (.md)")
                                  .arg(pdf ? QStringLiteral("PDF (.pdf)") : QStringLiteral("PDF (not available in this build)")),
                              "muted", importCard);
    importLayout->addWidget(m_importTypes);
    auto* importRow = new QHBoxLayout();
    importRow->setSpacing(5);
    m_importPptxBtn = makeButton(QStringLiteral("PowerPoint"), importCard, "chip");
    m_importPdfBtn = makeButton(QStringLiteral("PDF"), importCard, "chip");
    m_importTextBtn = makeButton(QStringLiteral("Text / Markdown"), importCard, "chip");
    m_importPdfBtn->setEnabled(pdf);
    m_importPptxBtn->setToolTip(QStringLiteral("Choose .pptx slide decks"));
    m_importPdfBtn->setToolTip(pdf ? QStringLiteral("Choose .pdf files") : QStringLiteral("PDF import is not available in this build"));
    m_importTextBtn->setToolTip(QStringLiteral("Choose .txt, .md, or .markdown notes"));
    m_importBtn = makeButton(QStringLiteral("Choose files…"), importCard, "primary");
    m_importBtn->setToolTip(QStringLiteral("Pick any supported files, or drop them on this window"));
    importRow->addWidget(m_importPptxBtn);
    importRow->addWidget(m_importPdfBtn);
    importRow->addWidget(m_importTextBtn);
    importRow->addStretch(1);
    importRow->addWidget(m_importBtn);
    importLayout->addLayout(importRow);
    importLayout->addWidget(makeLabel(QStringLiteral("Or drop files anywhere on this window."), "muted", importCard));
    importLayout->addWidget(makeLabel(QStringLiteral("Prefer your own words? Write questions yourself and they are practised with the rest."),
                                      "muted", importCard));
    m_writeBtn = makeButton(QStringLiteral("Write a question"), importCard, "chip");
    m_writeBtn->setToolTip(QStringLiteral("Add a multiple choice, multiple select, or written question of your own"));
    importLayout->addWidget(m_writeBtn, 0, Qt::AlignLeft);
    m_importCard = importCard;
    detail->addWidget(importCard);

    m_editorPage = new QWidget(detailPage);
    auto* editorLayout = new QVBoxLayout(m_editorPage);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    m_editor = new QuestionEditor(m_editorPage);
    editorLayout->addWidget(m_editor);
    editorLayout->addStretch(1);   // the form stays compact at the top of the page
    m_editorPage->hide();
    detail->addWidget(m_editorPage, 1);
    connect(m_writeBtn, &QPushButton::clicked, this, [this]() {
        click();
        m_editor->startNew();
        setEditing(true);
    });
    connect(m_editor, &QuestionEditor::cancelled, this, [this]() { setEditing(false); });
    connect(m_editor, &QuestionEditor::buttonActivated, this, [this]() { click(); });
    connect(m_editor, &QuestionEditor::saveRequested, this, [this](const Question& q, bool addAnother, bool isEdit) {
        if (saveOwnQuestion(q, isEdit) && !addAnother) {
            setEditing(false);
        }
    });
    connect(m_importBtn, &QPushButton::clicked, this, [this]() { onImportClicked(QString()); });
    connect(m_importPptxBtn, &QPushButton::clicked, this, [this]() { onImportClicked(QStringLiteral("PowerPoint (*.pptx)")); });
    connect(m_importPdfBtn, &QPushButton::clicked, this, [this]() { onImportClicked(QStringLiteral("PDF (*.pdf)")); });
    connect(m_importTextBtn, &QPushButton::clicked, this,
            [this]() { onImportClicked(QStringLiteral("Text and Markdown (*.txt *.md *.markdown)")); });

    // Which question formats practice uses is a preference; show it here so it is never a surprise
    m_practiceHint = makeLabel(QString(), "muted", detailPage);
    detail->addWidget(m_practiceHint);

    // Documents
    m_docScroll = new QScrollArea(detailPage);
    m_docScroll->setObjectName(QStringLiteral("docScroll"));
    m_docScroll->setWidgetResizable(true);
    m_docScroll->setFrameShape(QFrame::NoFrame);
    m_docScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_docScroll->viewport()->setAutoFillBackground(false);
    m_docList = new QWidget(m_docScroll);
    m_docList->setObjectName(QStringLiteral("docList"));
    m_docLayout = new QVBoxLayout(m_docList);
    m_docLayout->setContentsMargins(0, 0, 4, 0);
    m_docLayout->setSpacing(5);
    m_emptyLabel = makeLabel(QStringLiteral("No documents yet. Add PowerPoint, PDF, text, or Markdown files above."),
                             "muted", m_docList);
    m_docLayout->addWidget(m_emptyLabel);
    m_docLayout->addStretch(1);
    m_docScroll->setWidget(m_docList);
    m_docScroll->setMinimumHeight(120);
    detail->addWidget(m_docScroll, 1);

    m_actionBar = new QWidget(detailPage);
    auto* actions = new QHBoxLayout(m_actionBar);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);
    m_cancelBtn = makeButton(QStringLiteral("Cancel"), detailPage);
    m_regenerateBtn = makeButton(QStringLiteral("Regenerate"), detailPage);
    m_generateBtn = makeButton(QStringLiteral("Generate"), detailPage, "primary");
    m_generateBtn->setToolTip(QStringLiteral("Write new questions from the checked documents"));
    m_regenerateBtn->setToolTip(QStringLiteral("Replace the questions of the checked documents"));
    m_practiceBtn = makeButton(QStringLiteral("Practice"), detailPage, "primary");
    actions->addWidget(m_practiceBtn);
    actions->addStretch(1);
    actions->addWidget(m_cancelBtn);
    actions->addWidget(m_regenerateBtn);
    actions->addWidget(m_generateBtn);
    detail->addWidget(m_actionBar);
    m_stack->addWidget(detailPage);

    // Page 2: practising the open set
    m_practice = new StudyPracticeView(m_settings, m_library, m_tracker, m_ollama, m_stack);
    m_stack->addWidget(m_practice);
    connect(m_practice, &StudyPracticeView::finished, this, [this]() {
        m_practicing = false;
        applyView();
        refresh();
    });
    connect(m_practice, &StudyPracticeView::progressChanged, this, [this]() {
        m_prefs->refreshProgress();
        rebuildSetList();
    });
    connect(m_practice, &StudyPracticeView::buttonActivated, this, [this]() { click(); });
    connect(m_practiceBtn, &QPushButton::clicked, this, [this]() {
        click();
        startPractice();
    });
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(m_stack);
    m_prefs = new StudyPreferencesPage(m_settings, m_library, m_generator, m_ollama, m_tracker, this);
    auto* prefsScroll = new QScrollArea(m_pages);
    prefsScroll->setObjectName(QStringLiteral("docScroll"));
    prefsScroll->setWidgetResizable(true);
    prefsScroll->setFrameShape(QFrame::NoFrame);
    prefsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    prefsScroll->viewport()->setAutoFillBackground(false);
    prefsScroll->setWidget(m_prefs);
    m_pages->addWidget(prefsScroll);
    shell->addWidget(m_pages, 1);
    connect(m_tabs, &PixelSegmentedControl::currentChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_prefs, &StudyPreferencesPage::preferencesChanged, this, [this]() {
        updateFormatsHint();
        updateActions();
        emit preferencesChanged();
    });
    connect(m_prefs, &StudyPreferencesPage::askQuestionRequested, this, &StudyLibraryWindow::askQuestionRequested);
    connect(m_prefs, &StudyPreferencesPage::resetRequested, this, &StudyLibraryWindow::progressResetRequested);
    connect(m_prefs, &StudyPreferencesPage::buttonActivated, this, [this]() { click(); });

    // Progress and status are shared by both pages
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->hide();
    shell->addWidget(m_progress);
    m_statusLabel = makeLabel(QString(), "muted", this);
    shell->addWidget(m_statusLabel);

    connect(m_generateBtn, &QPushButton::clicked, this, [this]() {
        click();
        generate(false);
    });
    connect(m_regenerateBtn, &QPushButton::clicked, this, [this]() {
        click();
        generate(true);
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        click();
        if (m_generator && m_generator->isRunning()) {
            m_generator->cancel();
        }
        if (m_library && m_library->isBusy()) {
            m_library->cancelAll();
        }
        updateActions();
    });
}

void StudyLibraryWindow::click()
{
    emit buttonActivated();
    if (m_uiSound) {
        m_uiSound->playClick();
    }
}

bool StudyLibraryWindow::confirm(const QString& title, const QString& body, const QString& confirmText)
{
    PixelConfirmDialog dialog(title, body, confirmText, QStringLiteral("Cancel"), this);
    return dialog.exec() == QDialog::Accepted;
}

qint64 StudyLibraryWindow::currentSet() const
{
    return m_currentSet;
}

QString StudyLibraryWindow::currentSetName() const
{
    return setName(m_currentSet);
}

QString StudyLibraryWindow::setName(qint64 id) const
{
    if (m_library && m_library->isOpen()) {
        for (const StudySet& s : m_library->database()->studySets()) {
            if (s.id == id) {
                return s.name;
            }
        }
    }
    return QString();
}

void StudyLibraryWindow::selectSet(qint64 setId)
{
    openSet(setId);
}

bool StudyLibraryWindow::isShowingSet() const
{
    return m_stack && m_stack->currentIndex() == 1;
}

void StudyLibraryWindow::applyView()
{
    const bool showSet = m_showingSet && m_currentSet > 0;
    const bool practicing = showSet && m_practicing;
    m_stack->setCurrentIndex(practicing ? 2 : (showSet ? 1 : 0));
    m_backBtn->setVisible(showSet);
    m_backBtn->setText(practicing ? QStringLiteral("‹ Set") : QStringLiteral("‹ Sets"));
    m_backBtn->setToolTip(practicing ? QStringLiteral("End the session and go back to this set")
                                     : QStringLiteral("Back to all study sets"));
    const QString text = practicing ? QStringLiteral("Practice · %1").arg(setName(m_currentSet))
                                    : (showSet ? setName(m_currentSet) : QStringLiteral("Study Library"));
    m_titleLabel->setToolTip(showSet ? text : QString());
    m_titleLabel->setText(m_titleLabel->fontMetrics().elidedText(text, Qt::ElideRight, kWidth - 150));
}

void StudyLibraryWindow::showSetList()
{
    if (m_editor && m_editor->isVisible()) {
        m_editor->cancel();
    }
    if (m_practicing) {
        m_practice->abandon();
    }
    m_practicing = false;
    m_showingSet = false;
    applyView();
}

bool StudyLibraryWindow::startPractice()
{
    if (!m_practice || m_currentSet <= 0) {
        return false;
    }
    if (!m_practice->begin(m_currentSet)) {
        m_statusLabel->setText(QStringLiteral("There are no questions to practise in this set yet."));
        return false;
    }
    m_practicing = true;
    m_showingSet = true;
    m_statusLabel->clear();
    applyView();
    return true;
}

void StudyLibraryWindow::openSet(qint64 setId)
{
    if (setName(setId).isEmpty()) {
        return;
    }
    const bool changed = setId != m_currentSet;
    if (changed && m_practicing) {
        m_practice->abandon();
        m_practicing = false;
    }
    if (changed && m_editor && m_editor->isVisible()) {
        m_editor->cancel();
    }
    m_currentSet = setId;
    m_showingSet = true;
    if (changed) {
        if (m_settings) {
            m_settings->setStudySetId(setId);
            m_settings->save();
        }
        m_expandedDocs.clear();
    }
    applyView();
    rebuildDocuments();
    updateActions();
    if (changed) {
        emit setChanged(setId);
    }
}

void StudyLibraryWindow::rebuildSetList()
{
    QList<StudySetList::Entry> entries;
    if (m_library && m_library->isOpen()) {
        for (const StudySet& s : m_library->database()->studySets()) {
            int questions = 0;
            const QVector<DocumentRecord> docs = m_library->database()->documents(s.id);
            for (const DocumentRecord& d : docs) {
                questions += d.questionCount;
            }
            entries.append({s.id, s.name, static_cast<int>(docs.size()), questions});
        }
    }
    m_setList->setEntries(entries);
    m_setsEmpty->setVisible(entries.isEmpty());
}

void StudyLibraryWindow::reloadSets(qint64 selectId)
{
    qint64 id = 0;
    if (m_library && m_library->isOpen()) {
        const QVector<StudySet> sets = m_library->database()->studySets();
        for (const StudySet& s : sets) {
            if (s.id == selectId) {
                id = selectId;
            }
        }
        if (id == 0 && !sets.isEmpty()) {
            id = sets.first().id;   // the saved set (used for check-ins) falls back to the first one
        }
    }
    m_currentSet = id;
    if (m_settings && m_settings->studySetId() != id) {
        m_settings->setStudySetId(id);
        m_settings->save();
        emit setChanged(id);
    }
    rebuildSetList();
    applyView();
    rebuildDocuments();
    updateActions();
    if (m_prefs) {
        m_prefs->reloadSets();
    }
}

qint64 StudyLibraryWindow::createSet(const QString& name)
{
    if (!m_library || !m_library->isOpen()) {
        return 0;
    }
    const auto id = m_library->database()->addStudySet(name);
    if (!id) {
        m_statusLabel->setText(name.trimmed().isEmpty() ? QStringLiteral("Enter a name for the study set.")
                                                         : QStringLiteral("A study set with that name already exists."));
        return 0;
    }
    reloadSets(*id);
    emit setChanged(*id);
    openSet(*id);   // straight to its materials so the first files can be added
    return *id;
}

bool StudyLibraryWindow::renameSet(qint64 id, const QString& name)
{
    if (!m_library || id <= 0 || !m_library->database()->renameStudySet(id, name)) {
        m_statusLabel->setText(QStringLiteral("That name is empty or already used."));
        return false;
    }
    reloadSets(m_currentSet);
    emit setChanged(m_currentSet);
    return true;
}

bool StudyLibraryWindow::renameCurrentSet(const QString& name)
{
    return renameSet(m_currentSet, name);
}

bool StudyLibraryWindow::removeSet(qint64 id, bool confirmed)
{
    if (!m_library || id <= 0) {
        return false;
    }
    if (!confirmed
        && !confirm(QStringLiteral("Delete “%1”?").arg(setName(id)),
                    QStringLiteral("Its documents, questions, and study progress will be deleted from this computer."),
                    QStringLiteral("Delete"))) {
        return false;
    }
    if (m_generator && m_generator->isRunning()) {
        m_generator->cancel();
    }
    const bool ok = m_library->removeStudySet(id);
    m_showingSet = false;
    reloadSets(id == m_currentSet ? 0 : m_currentSet);
    emit setChanged(m_currentSet);
    return ok;
}

bool StudyLibraryWindow::removeCurrentSet(bool confirmed)
{
    return removeSet(m_currentSet, confirmed);
}

void StudyLibraryWindow::beginSetEdit(bool rename, qint64 id)
{
    m_renaming = rename;
    m_editTarget = rename ? id : 0;
    m_setEdit->setText(rename ? setName(id) : QString());
    m_setEditRow->show();
    activateWindow();
    m_setEdit->setFocus();
    m_setEdit->selectAll();
}

void StudyLibraryWindow::finishSetEdit(bool accept)
{
    if (accept) {
        const QString name = m_setEdit->text();
        const bool ok = m_renaming ? renameSet(m_editTarget, name) : createSet(name) != 0;
        if (!ok) {
            return;
        }
    }
    m_setEditRow->hide();
}

void StudyLibraryWindow::importFiles(const QStringList& paths)
{
    if (!m_library || currentSet() <= 0) {
        m_statusLabel->setText(QStringLiteral("Create a study set first."));
        return;
    }
    QStringList rejected;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!supportedSuffix(info.suffix())) {
            rejected.append(info.fileName());
        }
    }
    const QList<qint64> ids = m_library->importFiles(currentSet(), paths);
    const qsizetype skipped = paths.size() - ids.size();
    QString message;
    if (!ids.isEmpty()) {
        message = QStringLiteral("Importing %1 file(s)…").arg(ids.size());
    }
    if (skipped > 0) {
        message += (message.isEmpty() ? QString() : QStringLiteral(" ")) + skippedMessage(rejected, skipped);
    }
    m_statusLabel->setText(message);
    refresh();
}

void StudyLibraryWindow::onImportClicked(const QString& filter)
{
    click();
    if (currentSet() <= 0) {
        showSetList();
        beginSetEdit(false, 0);
        m_statusLabel->setText(QStringLiteral("Create a study set first, then import its materials."));
        return;
    }
    const bool pdf = DocumentExtractor::pdfSupported();
    QString filters = filter;
    if (filters.isEmpty()) {
        filters = QStringLiteral("All supported (%1);;PowerPoint (*.pptx);;%2Text and Markdown (*.txt *.md *.markdown)")
                      .arg(pdf ? QStringLiteral("*.pdf *.pptx *.txt *.md *.markdown") : QStringLiteral("*.pptx *.txt *.md *.markdown"),
                           pdf ? QStringLiteral("PDF (*.pdf);;") : QString());
    }
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add study materials"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), filters);
    if (!paths.isEmpty()) {
        importFiles(paths);
    }
}

void StudyLibraryWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (isShowingSet() && currentTab() == 0 && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void StudyLibraryWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (isShowingSet() && currentTab() == 0 && !paths.isEmpty()) {
        importFiles(paths);
        event->acceptProposedAction();
        return;
    }
    QWidget::dropEvent(event);
}

QList<qint64> StudyLibraryWindow::checkedDocuments() const
{
    QList<qint64> ids;
    for (auto it = m_docChecks.cbegin(); it != m_docChecks.cend(); ++it) {
        if (it.value()->isChecked() && !m_manualDocs.contains(it.key())) {   // nothing to generate from
            ids.append(it.key());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool StudyLibraryWindow::generate(bool regenerate, bool confirmed)
{
    if (!m_generator || !m_settings || m_generator->isRunning()) {
        return false;
    }
    const QList<qint64> docs = checkedDocuments();
    if (docs.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Check at least one document to generate questions from."));
        return false;
    }
    if (regenerate && !confirmed
        && !confirm(QStringLiteral("Replace questions?"),
                    QStringLiteral("The checked documents get new questions. Answers to their old questions are removed from your progress."),
                    QStringLiteral("Regenerate"))) {
        return false;
    }
    QList<QuestionType> formats;
    const int mask = m_settings->studyFormats();
    if (mask & SettingsDefaults::kStudyFormatSingleChoice) {
        formats.append(QuestionType::SingleChoice);
    }
    if (mask & SettingsDefaults::kStudyFormatMultiSelect) {
        formats.append(QuestionType::MultiSelect);
    }
    if (mask & SettingsDefaults::kStudyFormatWritten) {
        formats.append(QuestionType::Written);
    }
    const bool started = m_generator->start(docs, formats, regenerate);
    if (!started) {
        m_statusLabel->setText(QStringLiteral("Nothing new to generate: every passage of the checked documents already has a question. Use Regenerate to replace them."));
    }
    refresh();
    return started;
}

bool StudyLibraryWindow::removeDocument(qint64 documentId, bool confirmed)
{
    if (!m_library) {
        return false;
    }
    const auto doc = m_library->database()->document(documentId);
    if (!doc || doc->kind == DocumentKind::Manual) {
        return false;   // questions you wrote are deleted one by one
    }
    if (!confirmed
        && !confirm(QStringLiteral("Remove “%1”?").arg(doc->title),
                    QStringLiteral("Its copy, questions, and study progress will be deleted from this computer."),
                    QStringLiteral("Remove"))) {
        return false;
    }
    if (m_generator && m_generator->isRunning()) {
        m_generator->cancel();
    }
    m_expandedDocs.remove(documentId);
    return m_library->removeDocument(documentId);
}

void StudyLibraryWindow::refresh()
{
    rebuildSetList();
    rebuildDocuments();
    updateActions();
}

void StudyLibraryWindow::rebuildDocuments()
{
    // Keep check states across rebuilds (default: checked = selected for study).
    QHash<qint64, bool> checked;
    for (auto it = m_docChecks.cbegin(); it != m_docChecks.cend(); ++it) {
        checked.insert(it.key(), it.value()->isChecked());
    }
    for (QFrame* row : std::as_const(m_docRows)) {
        m_docLayout->removeWidget(row);
        row->deleteLater();
    }
    m_docRows.clear();
    m_docChecks.clear();
    m_docMeta.clear();
    m_manualDocs.clear();

    const qint64 studySet = currentSet();
    const QVector<DocumentRecord> docs = (m_library && studySet > 0) ? m_library->database()->documents(studySet)
                                                                   : QVector<DocumentRecord>();
    m_emptyLabel->setVisible(docs.isEmpty());
    m_emptyLabel->setText(studySet > 0 ? QStringLiteral("No documents yet. Add PowerPoint, PDF, text, or Markdown files above.")
                                     : QStringLiteral("Create a study set to start your Study Library."));
    int insertAt = m_docLayout->indexOf(m_emptyLabel) + 1;
    for (const DocumentRecord& d : docs) {
        QFrame* row = makeCard(m_docList);
        auto* layout = new QVBoxLayout(row);
        layout->setContentsMargins(8, 6, 8, 7);
        layout->setSpacing(2);
        auto* top = new QHBoxLayout();
        top->setSpacing(4);
        auto* check = new PixelCheckBox(QString(), row);
        check->setFixedWidth(26);
        check->setChecked(checked.value(d.id, d.selected));
        check->setToolTip(QStringLiteral("Use this document for study questions"));
        connect(check, &QCheckBox::toggled, this, [this, id = d.id](bool on) {
            if (m_library) {
                m_library->database()->setDocumentSelected(id, on);
            }
            updateActions();
            emit preferencesChanged();
        });
        auto* title = makeLabel(d.title, "sectionTitle", row);
        title->setWordWrap(false);
        title->setText(title->fontMetrics().elidedText(d.title, Qt::ElideMiddle, kWidth - 160));
        title->setToolTip(d.originalPath);
        auto* viewBtn = makeButton(m_expandedDocs.contains(d.id) ? QStringLiteral("Hide") : QStringLiteral("Questions"), row, "chip");
        viewBtn->setEnabled(d.questionCount > 0);
        const bool manual = d.kind == DocumentKind::Manual;
        if (manual) {
            m_manualDocs.insert(d.id);
        }
        auto* removeBtn = new PixelIconButton(PixelIcon::Close, 22, 9, false, row);
        removeBtn->setToolTip(QStringLiteral("Remove document"));
        removeBtn->setAccessibleName(QStringLiteral("Remove %1").arg(d.title));
        removeBtn->setVisible(!manual);
        top->addWidget(check);
        top->addWidget(title, 1);
        top->addWidget(viewBtn);
        if (!manual) {
            top->addWidget(removeBtn);
        }
        layout->addLayout(top);

        QString meta = StudyText::kindLabel(d.kind);
        if (manual) {
            meta = QStringLiteral("Written by you");
        }
        if (d.unitCount > 0) {
            meta += QStringLiteral(" · %1 %2").arg(d.unitCount).arg(unitName(d.kind, d.unitCount));
        }
        meta += QStringLiteral(" · %1 question%2").arg(d.questionCount).arg(d.questionCount == 1 ? QString() : QStringLiteral("s"));
        if (!manual) {
            meta += QStringLiteral(" · %1").arg(StudyText::statusLabel(d.status));
        }
        auto* metaLabel = makeLabel(meta, "muted", row);
        layout->addWidget(metaLabel);
        QString note = d.error;
        if (note.isEmpty() && !d.unsupportedUnits.isEmpty()) {
            note = QStringLiteral("No readable text on %1 %2 (scans or images; OCR is not supported yet).")
                       .arg(unitName(d.kind, 2), StudyText::compactRanges(d.unsupportedUnits));
        }
        if (!note.isEmpty()) {
            auto* noteLabel = makeLabel(note, "muted", row);
            if (d.status == DocumentStatus::Failed || d.status == DocumentStatus::Unsupported) {
                noteLabel->setStyleSheet(QStringLiteral("color: #F48C94;"));
            }
            layout->addWidget(noteLabel);
        }
        if (m_expandedDocs.contains(d.id)) {
            for (const Question& q : m_library->database()->documentQuestions(d.id)) {
                const QString location = q.sources.isEmpty() ? QString() : q.sources.first().location;
                auto* qLabel = makeLabel(location.isEmpty() ? QStringLiteral("[%1] %2").arg(typeTag(q.type), q.prompt)
                                                            : QStringLiteral("[%1] %2  (%3)").arg(typeTag(q.type), q.prompt, location),
                                         "muted", row);
                qLabel->setToolTip(q.sources.isEmpty() ? QString() : q.sources.first().excerpt);
                if (!manual) {
                    layout->addWidget(qLabel);
                    continue;
                }
                auto* line = new QHBoxLayout();
                line->setSpacing(4);
                line->addWidget(qLabel, 1);
                auto* editBtn = makeButton(QStringLiteral("Edit"), row, "chip");
                auto* deleteBtn = makeButton(QStringLiteral("Delete"), row, "chip");
                line->addWidget(editBtn, 0, Qt::AlignTop);
                line->addWidget(deleteBtn, 0, Qt::AlignTop);
                layout->addLayout(line);
                connect(editBtn, &QPushButton::clicked, this, [this, id = q.id]() {
                    click();
                    editOwnQuestion(id);
                });
                connect(deleteBtn, &QPushButton::clicked, this, [this, id = q.id]() {
                    click();
                    removeOwnQuestion(id, false);
                });
            }
        }
        connect(viewBtn, &QPushButton::clicked, this, [this, id = d.id]() {
            click();
            if (m_expandedDocs.contains(id)) {
                m_expandedDocs.remove(id);
            } else {
                m_expandedDocs.insert(id);
            }
            rebuildDocuments();
        });
        connect(removeBtn, &QPushButton::clicked, this, [this, id = d.id]() {
            click();
            removeDocument(id, false);
        });
        m_docLayout->insertWidget(insertAt++, row);
        m_docRows.insert(d.id, row);
        m_docChecks.insert(d.id, check);
        m_docMeta.insert(d.id, metaLabel);
    }
}

void StudyLibraryWindow::updateActions()
{
    const bool hasSet = currentSet() > 0;
    const bool generating = m_generator && m_generator->isRunning();
    const bool importing = m_library && m_library->isBusy();
    const bool modelReady = m_ollama && m_ollama->setupState() == OllamaClient::SetupState::Ready;
    bool anyText = false;
    bool anyQuestions = false;
    for (const qint64 id : checkedDocuments()) {
        std::optional<DocumentRecord> doc;
        if (m_library) {
            doc = m_library->database()->document(id);
        }
        if (doc && doc->chunkCount > 0) {
            anyText = true;
            anyQuestions = anyQuestions || doc->questionCount > 0;
        }
    }
    if (m_practiceBtn) {
        int total = 0;
        int inFormats = 0;
        if (hasSet && m_library && m_library->isOpen()) {
            total = static_cast<int>(m_library->database()->setQuestions(m_currentSet, {}, true).size());
            inFormats = static_cast<int>(m_library->database()->setQuestions(m_currentSet, StudyPracticeView::enabledTypes(m_settings), true).size());
        }
        m_practiceBtn->setText(inFormats > 0 ? QStringLiteral("Practice (%1)").arg(inFormats) : QStringLiteral("Practice"));
        m_practiceBtn->setEnabled(hasSet && inFormats > 0 && !generating);
        m_practiceBtn->setToolTip(inFormats > 0 ? QStringLiteral("Answer this set's questions one at a time")
                                  : total > 0 ? QStringLiteral("No questions in the chosen formats. Change formats in Preferences.")
                                              : QStringLiteral("No questions yet. Generate some from your documents first."));
    }
    m_importBtn->setEnabled(hasSet);
    m_writeBtn->setEnabled(hasSet);
    m_importPptxBtn->setEnabled(hasSet);
    m_importPdfBtn->setEnabled(hasSet && DocumentExtractor::pdfSupported());
    m_importTextBtn->setEnabled(hasSet);
    m_generateBtn->setEnabled(hasSet && modelReady && anyText && !generating);
    m_regenerateBtn->setEnabled(hasSet && modelReady && anyQuestions && !generating);
    m_cancelBtn->setVisible(generating || importing);
    m_generateBtn->setToolTip(modelReady ? QStringLiteral("Write new questions from the checked documents")
                                         : QStringLiteral("Set up the local AI model above to generate questions"));
}

void StudyLibraryWindow::updateSetupBanner(OllamaClient::SetupState state)
{
    const QString model = m_ollama ? m_ollama->model() : (m_settings ? m_settings->studyModel() : QString());
    m_setupLabel->setText(OllamaClient::setupMessage(state, model));
    const bool pulling = m_ollama && m_ollama->isPulling();
    m_getOllamaBtn->setVisible(state == OllamaClient::SetupState::NotInstalled);
    m_startOllamaBtn->setVisible(state == OllamaClient::SetupState::NotRunning);
    m_downloadBtn->setVisible(state == OllamaClient::SetupState::ModelMissing && !pulling);
    m_cancelDownloadBtn->setVisible(pulling);
    m_downloadProgress->setVisible(pulling);
    m_recheckBtn->setVisible(state != OllamaClient::SetupState::Ready && state != OllamaClient::SetupState::Checking);

    m_prefs->updateModels(state);
    // A working setup needs no banner; the card returns only when something must be done.
    const bool ready = state == OllamaClient::SetupState::Ready;
    const bool checking = state == OllamaClient::SetupState::Checking;
    m_setupLabel->setVisible(!ready);
    m_setupCard->setVisible((!ready && !checking) || pulling);
    updateActions();
}

void StudyLibraryWindow::onDownloadClicked()
{
    click();
    if (!m_ollama) {
        return;
    }
    const QString model = m_ollama->model();
    if (!confirm(QStringLiteral("Download %1?").arg(model),
                 QStringLiteral("Ollama will download about %1 from the internet. You only need internet this once; "
                                "after that, questions are written and checked offline on this computer.")
                     .arg(OllamaClient::approximateDownloadSize(model).remove(QStringLiteral("about "))),
                 QStringLiteral("Download"))) {
        return;
    }
    m_downloadProgress->setRange(0, 0);
    m_downloadProgress->show();
    m_downloadBtn->hide();
    m_cancelDownloadBtn->show();
    m_setupLabel->setText(QStringLiteral("Starting download of %1…").arg(model));
    m_ollama->pullModel(model);
}

PixelCheckBox* StudyLibraryWindow::formatCheck(int formatBit) const
{
    return m_prefs->formatCheck(formatBit);
}

PixelOptionButton* StudyLibraryWindow::mixedModeButton() const
{
    return m_prefs->mixedModeButton();
}

PixelOptionButton* StudyLibraryWindow::focusModeButton() const
{
    return m_prefs->focusModeButton();
}

QComboBox* StudyLibraryWindow::focusCombo() const
{
    return m_prefs->focusCombo();
}

QComboBox* StudyLibraryWindow::modelCombo() const
{
    return m_prefs->modelCombo();
}

CollapsibleSection* StudyLibraryWindow::formatsSection() const
{
    return m_prefs->formatsSection();
}

bool StudyLibraryWindow::formatsExpanded() const
{
    return m_prefs->formatsExpanded();
}

void StudyLibraryWindow::setEditing(bool editing)
{
    // The editor takes the whole page so the form never has to scroll or push the window taller.
    m_importCard->setVisible(!editing);
    m_practiceHint->setVisible(!editing);
    m_docScroll->setVisible(!editing);
    m_actionBar->setVisible(!editing);
    m_editorPage->setVisible(editing);
    m_editor->setVisible(editing);
}

bool StudyLibraryWindow::saveOwnQuestion(const Question& question, bool isEdit)
{
    if (!m_library || !m_library->isOpen() || m_currentSet <= 0) {
        return false;
    }
    StudyDatabase* db = m_library->database();
    Question q = question;
    bool ok = false;
    qint64 documentId = 0;
    if (isEdit) {
        const auto existing = db->question(q.id);
        if (!existing) {
            m_statusLabel->setText(QStringLiteral("That question no longer exists."));
            return false;
        }
        q.setId = existing->setId;
        q.documentId = existing->documentId;
        documentId = q.documentId;
        ok = db->updateQuestion(q);
    } else {
        const auto doc = db->manualDocument(m_currentSet);
        if (!doc) {
            m_statusLabel->setText(QStringLiteral("Could not save the question."));
            return false;
        }
        documentId = *doc;
        q.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        q.setId = m_currentSet;
        q.documentId = documentId;
        ok = db->saveQuestion(q, 0, QStringLiteral("manual"));
    }
    if (!ok) {
        m_statusLabel->setText(QStringLiteral("Could not save the question."));
        return false;
    }
    m_expandedDocs.insert(documentId);
    m_statusLabel->setText(isEdit ? QStringLiteral("Question updated.") : QStringLiteral("Question added to My questions."));
    refresh();
    emit preferencesChanged();   // the pet's question pool changed
    return true;
}

bool StudyLibraryWindow::editOwnQuestion(const QString& questionId)
{
    if (!m_library) {
        return false;
    }
    const auto q = m_library->database()->question(questionId);
    if (!q) {
        return false;
    }
    m_editor->startEdit(*q);
    setEditing(true);
    return true;
}

bool StudyLibraryWindow::removeOwnQuestion(const QString& questionId, bool confirmed)
{
    if (!m_library) {
        return false;
    }
    const auto q = m_library->database()->question(questionId);
    if (!q) {
        return false;
    }
    if (!confirmed
        && !confirm(QStringLiteral("Delete this question?"),
                    QStringLiteral("The question and your answers to it are removed from this computer."),
                    QStringLiteral("Delete"))) {
        return false;
    }
    const bool ok = m_library->database()->removeQuestion(questionId);
    refresh();
    emit preferencesChanged();
    return ok;
}

void StudyLibraryWindow::updateFormatsHint()
{
    m_practiceHint->setText(QStringLiteral("Practice and questions use: %1. Change this in Preferences.").arg(m_prefs->formatsSummary().toLower()));
}

void StudyLibraryWindow::refreshProgress()
{
    m_prefs->refreshProgress();
}

void StudyLibraryWindow::showTab(int index)
{
    m_tabs->setCurrentIndex(index);
}

int StudyLibraryWindow::currentTab() const
{
    return m_tabs->currentIndex();
}

QList<qint64> StudyLibraryWindow::documentRows() const
{
    QList<qint64> ids = m_docRows.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

PixelCheckBox* StudyLibraryWindow::documentCheck(qint64 documentId) const
{
    return m_docChecks.value(documentId, nullptr);
}

QLabel* StudyLibraryWindow::documentMeta(qint64 documentId) const
{
    return m_docMeta.value(documentId, nullptr);
}

void StudyLibraryWindow::showNear(const QRect& petGlobalGeometry)
{
    QScreen* screen = QGuiApplication::screenAt(petGlobalGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    const int height = std::min(600, avail.height() * 85 / 100);
    resize(kWidth, height);
    int x = petGlobalGeometry.right() + 12;
    if (x + kWidth > avail.right()) {
        x = petGlobalGeometry.left() - 12 - kWidth;
    }
    x = std::clamp(x, avail.left(), std::max(avail.left(), avail.right() - kWidth + 1));
    const int y = std::clamp(petGlobalGeometry.center().y() - height / 2, avail.top(),
                             std::max(avail.top(), avail.bottom() - height + 1));
    move(x, y);
    reloadSets(m_settings ? m_settings->studySetId() : currentSet());
    showSetList();
    m_prefs->syncFromSettings();
    updateFormatsHint();
    show();
#ifdef Q_OS_MACOS
    MacWindowHelper::configureFloatingOverlay(this, false);
#endif
    raise();
    activateWindow();
    if (m_ollama && !m_ollama->isPulling()) {
        m_ollama->checkSetup();
    }
}

void StudyLibraryWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    PixelTheme::paintGlassSurface(p, rect(), PixelTheme::GlassLevel::Primary, PixelTheme::kRadiusWindow, 5);
}

void StudyLibraryWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QWidget* hit = childAt(event->position().toPoint());
        bool interactive = false;
        for (QWidget* w = hit; w && w != this; w = w->parentWidget()) {
            if (qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QLineEdit*>(w)
                || qobject_cast<QScrollBar*>(w) || qobject_cast<QAbstractSlider*>(w)) {
                interactive = true;
                break;
            }
        }
        if (!interactive) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void StudyLibraryWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint target = event->globalPosition().toPoint() - m_dragOffset;
        if (QScreen* screen = QGuiApplication::screenAt(event->globalPosition().toPoint())) {
            const QRect avail = screen->availableGeometry();
            target.setX(std::clamp(target.x(), avail.left(), std::max(avail.left(), avail.right() - width() + 1)));
            target.setY(std::clamp(target.y(), avail.top(), std::max(avail.top(), avail.bottom() - height() + 1)));
        }
        move(target);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void StudyLibraryWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void StudyLibraryWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    hide();
}

void StudyLibraryWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_setEditRow->isVisible()) {
            finishSetEdit(false);
        } else if (m_practicing) {
            m_practice->abandon();
            m_practicing = false;
            applyView();
            refresh();
        } else if (isShowingSet()) {
            showSetList();
        } else {
            hide();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
