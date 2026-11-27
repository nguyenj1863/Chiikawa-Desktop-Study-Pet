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

#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QPushButton* makeButton(const QString& text, QWidget* parent, const char* objectName = nullptr)
{
    auto* btn = new QPushButton(text, parent);
    if (objectName) {
        btn->setObjectName(QString::fromLatin1(objectName));
    }
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

QLabel* makeLabel(const QString& text, const char* objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setWordWrap(true);
    return label;
}

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("studyCard"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    return card;
}

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
    setWindowTitle(QStringLiteral("Study Library"));
    setStyleSheet(PixelTheme::styleSheet()
                  + QStringLiteral("QScrollArea#docScroll, QWidget#docList { background: transparent; border: none; }"));
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
    reloadCourses(m_settings ? m_settings->studyCourseId() : 0);
    syncFormatControls();
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
    auto* title = new QLabel(QStringLiteral("Study Library"), this);
    title->setObjectName(QStringLiteral("title"));
    auto* closeBtn = new PixelIconButton(PixelIcon::Close, 30, 11, false, this);
    closeBtn->setToolTip(QStringLiteral("Close Study Library"));
    closeBtn->setAccessibleName(QStringLiteral("Close Study Library"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        click();
        hide();
    });
    header->addWidget(title, 1);
    header->addWidget(closeBtn);
    shell->addLayout(header);

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
    m_modelRow = new QWidget(m_setupCard);
    auto* modelLayout = new QHBoxLayout(m_modelRow);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->setSpacing(6);
    modelLayout->addWidget(new QLabel(QStringLiteral("Model"), m_modelRow));
    m_modelCombo = new QComboBox(m_modelRow);
    m_modelCombo->setObjectName(QStringLiteral("studyModelCombo"));
    m_modelCombo->setCursor(Qt::PointingHandCursor);
    m_modelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    modelLayout->addWidget(m_modelCombo, 1);
    setup->addWidget(m_modelRow);
    connect(m_modelCombo, &QComboBox::activated, this, [this](int index) {
        click();
        const QString model = m_modelCombo->itemData(index).toString();
        if (m_settings) {
            m_settings->setStudyModel(model);
            m_settings->save();
        }
        if (m_ollama) {
            m_ollama->setModel(model);
        }
        if (m_generator) {
            m_generator->setModelName(model);
        }
        emit preferencesChanged();
    });
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

    // Course row
    auto* courseRow = new QHBoxLayout();
    courseRow->setSpacing(6);
    courseRow->addWidget(makeLabel(QStringLiteral("Course"), "sectionTitle", this));
    m_courseCombo = new QComboBox(this);
    m_courseCombo->setObjectName(QStringLiteral("studyCourseCombo"));
    m_courseCombo->setCursor(Qt::PointingHandCursor);
    m_courseCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    courseRow->addWidget(m_courseCombo, 1);
    m_newCourseBtn = makeButton(QStringLiteral("New"), this, "chip");
    m_renameCourseBtn = makeButton(QStringLiteral("Rename"), this, "chip");
    m_deleteCourseBtn = makeButton(QStringLiteral("Delete"), this, "chip");
    m_deleteCourseBtn->setToolTip(QStringLiteral("Delete this course and all of its documents and progress"));
    for (QPushButton* b : {m_newCourseBtn, m_renameCourseBtn, m_deleteCourseBtn}) {
        courseRow->addWidget(b);
    }
    shell->addLayout(courseRow);
    connect(m_courseCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_syncing) {
            return;
        }
        const qint64 id = currentCourse();
        if (m_settings) {
            m_settings->setStudyCourseId(id);
            m_settings->save();
        }
        m_expandedDocs.clear();
        rebuildDocuments();
        updateActions();
        emit courseChanged(id);
    });
    connect(m_courseCombo, &QComboBox::activated, this, [this](int) { click(); });
    connect(m_newCourseBtn, &QPushButton::clicked, this, [this]() {
        click();
        beginCourseEdit(false);
    });
    connect(m_renameCourseBtn, &QPushButton::clicked, this, [this]() {
        click();
        beginCourseEdit(true);
    });
    connect(m_deleteCourseBtn, &QPushButton::clicked, this, [this]() {
        click();
        removeCurrentCourse(false);
    });

    m_courseEditRow = new QWidget(this);
    auto* editLayout = new QHBoxLayout(m_courseEditRow);
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->setSpacing(6);
    m_courseEdit = new QLineEdit(m_courseEditRow);
    m_courseEdit->setObjectName(QStringLiteral("courseNameEdit"));
    m_courseEdit->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_courseEdit->setMaxLength(60);
    m_courseEdit->setPlaceholderText(QStringLiteral("Course name, e.g. CS 340 Databases"));
    auto* saveCourse = makeButton(QStringLiteral("Save"), m_courseEditRow, "primary");
    auto* cancelCourse = makeButton(QStringLiteral("Cancel"), m_courseEditRow);
    editLayout->addWidget(m_courseEdit, 1);
    editLayout->addWidget(cancelCourse);
    editLayout->addWidget(saveCourse);
    m_courseEditRow->hide();
    shell->addWidget(m_courseEditRow);
    connect(saveCourse, &QPushButton::clicked, this, [this]() {
        click();
        finishCourseEdit(true);
    });
    connect(cancelCourse, &QPushButton::clicked, this, [this]() {
        click();
        finishCourseEdit(false);
    });
    connect(m_courseEdit, &QLineEdit::returnPressed, this, [this]() { finishCourseEdit(true); });

    // Question formats
    auto* formatCard = makeCard(this);
    auto* formats = new QVBoxLayout(formatCard);
    formats->setContentsMargins(10, 8, 10, 8);
    formats->setSpacing(3);
    formats->addWidget(makeLabel(QStringLiteral("Question formats"), "sectionTitle", formatCard));
    const QList<QPair<int, QString>> formatNames = {
        {SettingsDefaults::kStudyFormatSingleChoice, QStringLiteral("Multiple choice")},
        {SettingsDefaults::kStudyFormatMultiSelect, QStringLiteral("Multiple select")},
        {SettingsDefaults::kStudyFormatWritten, QStringLiteral("Written answer")},
    };
    auto* checks = new QHBoxLayout();
    checks->setSpacing(4);
    for (const auto& f : formatNames) {
        auto* check = new PixelCheckBox(f.second, formatCard);
        check->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_formatChecks.insert(f.first, check);
        checks->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &StudyLibraryWindow::applyFormatControls);
        connect(check, &QCheckBox::clicked, this, [this](bool) { click(); });
    }
    checks->addStretch(1);
    formats->addLayout(checks);
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(4);
    m_mixedBtn = new PixelOptionButton(QStringLiteral("Mixed"), PixelOptionButton::Indicator::Radio, formatCard);
    m_focusBtn = new PixelOptionButton(QStringLiteral("Only"), PixelOptionButton::Indicator::Radio, formatCard);
    for (PixelOptionButton* b : {m_mixedBtn, m_focusBtn}) {
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        b->setFixedWidth(b == m_mixedBtn ? 82 : 66);
        b->setFixedHeight(b->heightForWidth(b->width()));
        connect(b, &QAbstractButton::toggled, this, &StudyLibraryWindow::applyFormatControls);
        connect(b, &QAbstractButton::clicked, this, [this](bool) { click(); });
    }
    m_focusCombo = new QComboBox(formatCard);
    m_focusCombo->setCursor(Qt::PointingHandCursor);
    for (const auto& f : formatNames) {
        m_focusCombo->addItem(f.second, f.first);
    }
    connect(m_focusCombo, &QComboBox::currentIndexChanged, this, &StudyLibraryWindow::applyFormatControls);
    modeRow->addWidget(m_mixedBtn);
    modeRow->addWidget(m_focusBtn);
    modeRow->addWidget(m_focusCombo, 1);
    formats->addLayout(modeRow);
    m_formatHint = makeLabel(QString(), "muted", formatCard);
    m_formatHint->hide();
    formats->addWidget(m_formatHint);
    shell->addWidget(formatCard);

    // Documents
    m_docScroll = new QScrollArea(this);
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
    m_emptyLabel = makeLabel(QStringLiteral("No documents yet. Import lecture slides (PPTX), PDFs, or notes (TXT, MD)."),
                             "muted", m_docList);
    m_docLayout->addWidget(m_emptyLabel);
    m_docLayout->addStretch(1);
    m_docScroll->setWidget(m_docList);
    m_docScroll->setMinimumHeight(150);
    shell->addWidget(m_docScroll, 1);

    // Actions and status
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->hide();
    shell->addWidget(m_progress);
    m_statusLabel = makeLabel(QString(), "muted", this);
    shell->addWidget(m_statusLabel);
    auto* actions = new QHBoxLayout();
    actions->setSpacing(6);
    m_importBtn = makeButton(QStringLiteral("Import…"), this);
    m_cancelBtn = makeButton(QStringLiteral("Cancel"), this);
    m_regenerateBtn = makeButton(QStringLiteral("Regenerate"), this);
    m_generateBtn = makeButton(QStringLiteral("Generate"), this, "primary");
    m_generateBtn->setToolTip(QStringLiteral("Write new questions from the checked documents"));
    m_regenerateBtn->setToolTip(QStringLiteral("Replace the questions of the checked documents"));
    actions->addWidget(m_importBtn);
    actions->addStretch(1);
    actions->addWidget(m_cancelBtn);
    actions->addWidget(m_regenerateBtn);
    actions->addWidget(m_generateBtn);
    shell->addLayout(actions);
    connect(m_importBtn, &QPushButton::clicked, this, &StudyLibraryWindow::onImportClicked);
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

qint64 StudyLibraryWindow::currentCourse() const
{
    return m_courseCombo->currentIndex() >= 0 ? m_courseCombo->currentData().toLongLong() : 0;
}

void StudyLibraryWindow::selectCourse(qint64 courseId)
{
    const int index = m_courseCombo->findData(courseId);
    if (index >= 0) {
        m_courseCombo->setCurrentIndex(index);
    }
}

void StudyLibraryWindow::reloadCourses(qint64 selectId)
{
    m_syncing = true;
    m_courseCombo->clear();
    if (m_library && m_library->isOpen()) {
        for (const Course& c : m_library->database()->courses()) {
            m_courseCombo->addItem(c.name, c.id);
        }
    }
    int index = m_courseCombo->findData(selectId);
    if (index < 0 && m_courseCombo->count() > 0) {
        index = 0;
    }
    m_courseCombo->setCurrentIndex(index);
    m_syncing = false;
    const qint64 id = currentCourse();
    if (m_settings && m_settings->studyCourseId() != id) {
        m_settings->setStudyCourseId(id);
        m_settings->save();
        emit courseChanged(id);
    }
    rebuildDocuments();
    updateActions();
}

qint64 StudyLibraryWindow::createCourse(const QString& name)
{
    if (!m_library || !m_library->isOpen()) {
        return 0;
    }
    const auto id = m_library->database()->addCourse(name);
    if (!id) {
        m_statusLabel->setText(name.trimmed().isEmpty() ? QStringLiteral("Enter a course name.")
                                                         : QStringLiteral("A course with that name already exists."));
        return 0;
    }
    reloadCourses(*id);
    emit courseChanged(*id);
    return *id;
}

bool StudyLibraryWindow::renameCurrentCourse(const QString& name)
{
    const qint64 id = currentCourse();
    if (!m_library || id <= 0 || !m_library->database()->renameCourse(id, name)) {
        m_statusLabel->setText(QStringLiteral("That name is empty or already used."));
        return false;
    }
    reloadCourses(id);
    emit courseChanged(id);
    return true;
}

bool StudyLibraryWindow::removeCurrentCourse(bool confirmed)
{
    const qint64 id = currentCourse();
    if (!m_library || id <= 0) {
        return false;
    }
    if (!confirmed
        && !confirm(QStringLiteral("Delete “%1”?").arg(m_courseCombo->currentText()),
                    QStringLiteral("Its documents, questions, and study progress will be deleted from this computer."),
                    QStringLiteral("Delete"))) {
        return false;
    }
    if (m_generator && m_generator->isRunning()) {
        m_generator->cancel();
    }
    const bool ok = m_library->removeCourse(id);
    reloadCourses(0);
    emit courseChanged(currentCourse());
    return ok;
}

void StudyLibraryWindow::beginCourseEdit(bool rename)
{
    m_renaming = rename;
    m_courseEdit->setText(rename ? m_courseCombo->currentText() : QString());
    m_courseEditRow->show();
    activateWindow();
    m_courseEdit->setFocus();
    m_courseEdit->selectAll();
}

void StudyLibraryWindow::finishCourseEdit(bool accept)
{
    if (accept) {
        const QString name = m_courseEdit->text();
        const bool ok = m_renaming ? renameCurrentCourse(name) : createCourse(name) != 0;
        if (!ok) {
            return;
        }
    }
    m_courseEditRow->hide();
}

void StudyLibraryWindow::importFiles(const QStringList& paths)
{
    if (!m_library || currentCourse() <= 0) {
        m_statusLabel->setText(QStringLiteral("Create a course first."));
        return;
    }
    const QList<qint64> ids = m_library->importFiles(currentCourse(), paths);
    const qsizetype skipped = paths.size() - ids.size();
    m_statusLabel->setText(skipped > 0 ? QStringLiteral("Importing %1 file(s). %2 skipped: use PDF, PPTX, TXT, or MD.")
                                             .arg(ids.size())
                                             .arg(skipped)
                                       : QStringLiteral("Importing %1 file(s)…").arg(ids.size()));
    refresh();
}

void StudyLibraryWindow::onImportClicked()
{
    click();
    if (currentCourse() <= 0) {
        beginCourseEdit(false);
        m_statusLabel->setText(QStringLiteral("Create a course first, then import its materials."));
        return;
    }
    QString filter = DocumentExtractor::pdfSupported()
        ? QStringLiteral("Study materials (*.pdf *.pptx *.txt *.md *.markdown)")
        : QStringLiteral("Study materials (*.pptx *.txt *.md *.markdown)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Import study materials"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), filter);
    if (!paths.isEmpty()) {
        importFiles(paths);
    }
}

QList<qint64> StudyLibraryWindow::checkedDocuments() const
{
    QList<qint64> ids;
    for (auto it = m_docChecks.cbegin(); it != m_docChecks.cend(); ++it) {
        if (it.value()->isChecked()) {
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
    if (!doc) {
        return false;
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

    const qint64 course = currentCourse();
    const QVector<DocumentRecord> docs = (m_library && course > 0) ? m_library->database()->documents(course)
                                                                   : QVector<DocumentRecord>();
    m_emptyLabel->setVisible(docs.isEmpty());
    m_emptyLabel->setText(course > 0 ? QStringLiteral("No documents yet. Import lecture slides (PPTX), PDFs, or notes (TXT, MD).")
                                     : QStringLiteral("Create a course to start your Study Library."));
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
        auto* removeBtn = new PixelIconButton(PixelIcon::Close, 22, 9, false, row);
        removeBtn->setToolTip(QStringLiteral("Remove document"));
        removeBtn->setAccessibleName(QStringLiteral("Remove %1").arg(d.title));
        top->addWidget(check);
        top->addWidget(title, 1);
        top->addWidget(viewBtn);
        top->addWidget(removeBtn);
        layout->addLayout(top);

        QString meta = StudyText::kindLabel(d.kind);
        if (d.unitCount > 0) {
            meta += QStringLiteral(" · %1 %2").arg(d.unitCount).arg(unitName(d.kind, d.unitCount));
        }
        meta += QStringLiteral(" · %1 question%2").arg(d.questionCount).arg(d.questionCount == 1 ? QString() : QStringLiteral("s"));
        meta += QStringLiteral(" · %1").arg(StudyText::statusLabel(d.status));
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
                auto* qLabel = makeLabel(QStringLiteral("[%1] %2  (%3)")
                                             .arg(typeTag(q.type), q.prompt,
                                                  q.sources.isEmpty() ? QString() : q.sources.first().location),
                                         "muted", row);
                qLabel->setToolTip(q.sources.isEmpty() ? QString() : q.sources.first().excerpt);
                layout->addWidget(qLabel);
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
    const bool hasCourse = currentCourse() > 0;
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
    m_renameCourseBtn->setEnabled(hasCourse);
    m_deleteCourseBtn->setEnabled(hasCourse && !generating);
    m_importBtn->setEnabled(true);
    m_generateBtn->setEnabled(hasCourse && modelReady && anyText && !generating);
    m_regenerateBtn->setEnabled(hasCourse && modelReady && anyQuestions && !generating);
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

    // The model picker lists installed models plus the chosen one.
    m_syncing = true;
    m_modelCombo->clear();
    QStringList models = m_ollama ? m_ollama->installedModels() : QStringList();
    if (!model.isEmpty() && !models.contains(model)) {
        models.prepend(model);
    }
    for (const QString& m : models) {
        const bool installed = m_ollama && m_ollama->installedModels().contains(m);
        m_modelCombo->addItem(installed ? m : QStringLiteral("%1 (not installed)").arg(m), m);
    }
    m_modelCombo->setCurrentIndex(std::max(0, m_modelCombo->findData(model)));
    m_syncing = false;
    m_modelRow->setVisible(state == OllamaClient::SetupState::Ready || state == OllamaClient::SetupState::ModelMissing);
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

void StudyLibraryWindow::syncFormatControls()
{
    if (!m_settings) {
        return;
    }
    m_syncing = true;
    const int mask = m_settings->studyFormats();
    for (auto it = m_formatChecks.cbegin(); it != m_formatChecks.cend(); ++it) {
        it.value()->setChecked((mask & it.key()) != 0);
    }
    const bool focus = m_settings->studyFormatMode() == SettingsDefaults::kStudyFormatModeFocus;
    m_mixedBtn->setChecked(!focus);
    m_focusBtn->setChecked(focus);
    m_focusCombo->setCurrentIndex(std::max(0, m_focusCombo->findData(m_settings->studyFocusFormat())));
    m_focusCombo->setEnabled(focus);
    m_syncing = false;
}

void StudyLibraryWindow::applyFormatControls()
{
    if (m_syncing || !m_settings) {
        return;
    }
    int mask = 0;
    for (auto it = m_formatChecks.cbegin(); it != m_formatChecks.cend(); ++it) {
        if (it.value()->isChecked()) {
            mask |= it.key();
        }
    }
    if (!m_settings->setStudyFormats(mask)) {
        // At least one format must stay on: undo the last uncheck.
        m_formatHint->setText(QStringLiteral("Keep at least one question format turned on."));
        m_formatHint->show();
        syncFormatControls();
        return;
    }
    m_formatHint->hide();
    m_settings->setStudyFormatMode(m_focusBtn->isChecked() ? SettingsDefaults::kStudyFormatModeFocus
                                                           : SettingsDefaults::kStudyFormatModeMixed);
    m_settings->setStudyFocusFormat(m_focusCombo->currentData().toInt());
    m_settings->save();
    m_focusCombo->setEnabled(m_focusBtn->isChecked());
    emit preferencesChanged();
}

PixelCheckBox* StudyLibraryWindow::formatCheck(int formatBit) const
{
    return m_formatChecks.value(formatBit, nullptr);
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
    reloadCourses(m_settings ? m_settings->studyCourseId() : currentCourse());
    syncFormatControls();
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
        if (m_courseEditRow->isVisible()) {
            finishCourseEdit(false);
        } else {
            hide();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
