#include "ui/StudyPreferencesPage.h"

#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "study/ProgressTracker.h"
#include "study/QuestionGenerator.h"
#include "study/StudyLibrary.h"
#include "ui/PixelTheme.h"
#include "ui/StudyUiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

using namespace StudyUi;

StudyPreferencesPage::StudyPreferencesPage(AppSettings* settings, StudyLibrary* library, QuestionGenerator* generator,
                                           OllamaClient* ollama, ProgressTracker* progress, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_library(library)
    , m_generator(generator)
    , m_ollama(ollama)
    , m_tracker(progress)
{
    setObjectName(QStringLiteral("studyPrefs"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#studyPrefs { background: transparent; border: none; }"));
    build();
    reloadSets();
    syncFromSettings();
}

void StudyPreferencesPage::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 4, 0);
    root->setSpacing(7);

    // Chiikawa's questions
    auto* askCard = makeCard(this);
    auto* ask = new QVBoxLayout(askCard);
    ask->setContentsMargins(10, 8, 10, 9);
    ask->setSpacing(5);
    ask->addWidget(makeLabel(QStringLiteral("Chiikawa's questions"), "sectionTitle", askCard));
    m_checkIns = new PixelCheckBox(QStringLiteral("Ask me questions while I study"), askCard);
    m_checkIns->setObjectName(QStringLiteral("checkInsEnableCheck"));
    m_checkIns->setToolTip(QStringLiteral("Chiikawa occasionally asks a quick question while the study timer runs"));
    ask->addWidget(m_checkIns);

    auto* sourceRow = new QHBoxLayout();
    sourceRow->setSpacing(6);
    sourceRow->addWidget(new QLabel(QStringLiteral("From"), askCard));
    m_source = new QComboBox(askCard);
    m_source->setObjectName(QStringLiteral("studySourceCombo"));
    m_source->setCursor(Qt::PointingHandCursor);
    m_source->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_source->setToolTip(QStringLiteral("The study set Chiikawa asks questions from"));
    configureTranslucentPopup(m_source);
    sourceRow->addWidget(m_source, 1);
    ask->addLayout(sourceRow);

    auto* frequencyRow = new QHBoxLayout();
    frequencyRow->setSpacing(6);
    frequencyRow->addWidget(new QLabel(QStringLiteral("How often"), askCard));
    m_frequency = new QComboBox(askCard);
    m_frequency->setObjectName(QStringLiteral("checkInFrequencyCombo"));
    m_frequency->setCursor(Qt::PointingHandCursor);
    m_frequency->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const int minutes : SettingsDefaults::kCheckInMinuteChoices) {
        m_frequency->addItem(QStringLiteral("About every %1 min").arg(minutes), minutes);
    }
    configureTranslucentPopup(m_frequency);
    frequencyRow->addWidget(m_frequency, 1);
    ask->addLayout(frequencyRow);

    auto* askRow = new QHBoxLayout();
    askRow->setSpacing(6);
    askRow->addWidget(makeLabel(QStringLiteral("Questions appear while the study timer runs."), "muted", askCard), 1);
    m_askNow = makeButton(QStringLiteral("Ask me now"), askCard, "primary");
    m_askNow->setToolTip(QStringLiteral("Show a question next to Chiikawa right now"));
    askRow->addWidget(m_askNow);
    ask->addLayout(askRow);
    root->addWidget(askCard);

    connect(m_checkIns, &QCheckBox::toggled, this, &StudyPreferencesPage::applyCheckIns);
    connect(m_checkIns, &QCheckBox::clicked, this, [this](bool) { emit buttonActivated(); });
    connect(m_source, &QComboBox::currentIndexChanged, this, [this](int) { applyCheckIns(); });
    connect(m_source, &QComboBox::activated, this, [this](int) { emit buttonActivated(); });
    connect(m_frequency, &QComboBox::currentIndexChanged, this, [this](int) { applyCheckIns(); });
    connect(m_frequency, &QComboBox::activated, this, [this](int) { emit buttonActivated(); });
    connect(m_askNow, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit askQuestionRequested();
    });

    // Question formats: collapsed, multiple choice by default
    auto* formatCard = makeCard(this);
    auto* formats = new QVBoxLayout(formatCard);
    formats->setContentsMargins(10, 6, 10, 6);
    formats->setSpacing(3);
    m_formatsToggle = makeButton(QString(), formatCard, "chip");
    m_formatsToggle->setObjectName(QStringLiteral("formatsToggle"));
    m_formatsToggle->setStyleSheet(QStringLiteral("QPushButton#formatsToggle { text-align: left; background: transparent; border: none;"
                                                  " color: #F3F6FC; padding: 3px 2px; }"
                                                  " QPushButton#formatsToggle:hover { color: #FFFFFF; }"));
    m_formatsToggle->setIconSize(QSize(12, 12));
    formats->addWidget(m_formatsToggle);
    m_formatsBody = new QWidget(formatCard);
    auto* body = new QVBoxLayout(m_formatsBody);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(3);
    const QList<QPair<int, QString>> formatNames = {
        {SettingsDefaults::kStudyFormatSingleChoice, QStringLiteral("Multiple choice")},
        {SettingsDefaults::kStudyFormatMultiSelect, QStringLiteral("Multiple select")},
        {SettingsDefaults::kStudyFormatWritten, QStringLiteral("Written answer")},
    };
    auto* checks = new QHBoxLayout();
    checks->setSpacing(4);
    for (const auto& f : formatNames) {
        auto* check = new PixelCheckBox(f.second, m_formatsBody);
        check->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_formatChecks.insert(f.first, check);
        checks->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &StudyPreferencesPage::applyFormatControls);
        connect(check, &QCheckBox::clicked, this, [this](bool) { emit buttonActivated(); });
    }
    checks->addStretch(1);
    body->addLayout(checks);
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(4);
    m_mixedBtn = new PixelOptionButton(QStringLiteral("Mixed"), PixelOptionButton::Indicator::Radio, m_formatsBody);
    m_focusBtn = new PixelOptionButton(QStringLiteral("Only"), PixelOptionButton::Indicator::Radio, m_formatsBody);
    for (PixelOptionButton* b : {m_mixedBtn, m_focusBtn}) {
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        b->setFixedWidth(b == m_mixedBtn ? 82 : 66);
        b->setFixedHeight(b->heightForWidth(b->width()));
        connect(b, &QAbstractButton::toggled, this, &StudyPreferencesPage::applyFormatControls);
        connect(b, &QAbstractButton::clicked, this, [this](bool) { emit buttonActivated(); });
    }
    m_focusCombo = new QComboBox(m_formatsBody);
    m_focusCombo->setCursor(Qt::PointingHandCursor);
    configureTranslucentPopup(m_focusCombo);
    for (const auto& f : formatNames) {
        m_focusCombo->addItem(f.second, f.first);
    }
    connect(m_focusCombo, &QComboBox::currentIndexChanged, this, &StudyPreferencesPage::applyFormatControls);
    modeRow->addWidget(m_mixedBtn);
    modeRow->addWidget(m_focusBtn);
    modeRow->addWidget(m_focusCombo, 1);
    body->addLayout(modeRow);
    m_formatHint = makeLabel(QString(), "muted", m_formatsBody);
    m_formatHint->hide();
    body->addWidget(m_formatHint);
    formats->addWidget(m_formatsBody);
    root->addWidget(formatCard);
    connect(m_formatsToggle, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        setFormatsExpanded(!formatsExpanded());
    });
    setFormatsExpanded(false);

    // AI model (only offered when a model can be chosen or downloaded)
    m_modelCard = makeCard(this);
    auto* model = new QHBoxLayout(m_modelCard);
    model->setContentsMargins(10, 8, 10, 8);
    model->setSpacing(6);
    model->addWidget(makeLabel(QStringLiteral("AI model"), "sectionTitle", m_modelCard));
    m_modelCombo = new QComboBox(m_modelCard);
    m_modelCombo->setObjectName(QStringLiteral("studyModelCombo"));
    m_modelCombo->setCursor(Qt::PointingHandCursor);
    m_modelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    configureTranslucentPopup(m_modelCombo);
    model->addWidget(m_modelCombo, 1);
    root->addWidget(m_modelCard);
    connect(m_modelCombo, &QComboBox::activated, this, [this](int index) {
        emit buttonActivated();
        const QString name = m_modelCombo->itemData(index).toString();
        if (m_settings) {
            m_settings->setStudyModel(name);
            m_settings->save();
        }
        if (m_ollama) {
            m_ollama->setModel(name);
        }
        if (m_generator) {
            m_generator->setModelName(name);
        }
        emit preferencesChanged();
    });

    // Progress
    auto* progressCard = makeCard(this);
    auto* progress = new QHBoxLayout(progressCard);
    progress->setContentsMargins(10, 8, 10, 8);
    progress->setSpacing(6);
    m_progressLabel = makeLabel(QString(), "sectionSummary", progressCard);
    m_reset = makeButton(QStringLiteral("Reset progress"), progressCard);
    m_reset->setToolTip(QStringLiteral("Clear study XP, answer history and review schedules. Documents and questions are kept."));
    progress->addWidget(m_progressLabel, 1);
    progress->addWidget(m_reset);
    root->addWidget(progressCard);
    connect(m_reset, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        resetProgress(false);
    });

    root->addStretch(1);
}

bool StudyPreferencesPage::formatsExpanded() const
{
    return m_formatsBody && !m_formatsBody->isHidden();
}

void StudyPreferencesPage::setFormatsExpanded(bool expanded)
{
    m_formatsBody->setVisible(expanded);
    m_formatsToggle->setIcon(PixelTheme::icon(expanded ? PixelIcon::ChevronDown : PixelIcon::ChevronRight,
                                              QColor(0xC8, 0xD4, 0xE6), 12));
    m_formatsToggle->setAccessibleName(expanded ? QStringLiteral("Hide question formats") : QStringLiteral("Show question formats"));
}

QString StudyPreferencesPage::formatsSummary() const
{
    if (!m_settings) {
        return QString();
    }
    if (m_settings->studyFormatMode() == SettingsDefaults::kStudyFormatModeFocus) {
        const int index = m_focusCombo->findData(m_settings->studyFocusFormat());
        return QStringLiteral("Only %1").arg(index >= 0 ? m_focusCombo->itemText(index).toLower() : QString());
    }
    const int mask = m_settings->studyFormats();
    QStringList names;
    for (int i = 0; i < m_focusCombo->count(); ++i) {
        if (mask & m_focusCombo->itemData(i).toInt()) {
            names.append(m_focusCombo->itemText(i));
        }
    }
    return names.size() == m_focusCombo->count() ? QStringLiteral("All formats") : names.join(QStringLiteral(", "));
}

void StudyPreferencesPage::updateFormatsSummary()
{
    m_formatsToggle->setText(QStringLiteral(" Question formats  ·  %1").arg(formatsSummary()));
}

void StudyPreferencesPage::reloadSets()
{
    m_syncing = true;
    const qint64 wanted = m_settings ? m_settings->studySetId() : 0;
    m_source->clear();
    if (m_library && m_library->isOpen()) {
        for (const StudySet& s : m_library->database()->studySets()) {
            m_source->addItem(s.name, s.id);
        }
    }
    if (m_source->count() == 0) {
        m_source->addItem(QStringLiteral("No study sets yet"), 0);
    }
    m_source->setCurrentIndex(std::max(0, m_source->findData(wanted)));
    m_source->setEnabled(m_source->itemData(0).toLongLong() > 0);
    m_syncing = false;
}

void StudyPreferencesPage::syncFromSettings()
{
    if (!m_settings) {
        return;
    }
    m_syncing = true;
    m_checkIns->setChecked(m_settings->checkInsEnabled());
    const int freq = m_frequency->findData(m_settings->checkInMinutes());
    m_frequency->setCurrentIndex(freq >= 0 ? freq : 0);
    m_source->setCurrentIndex(std::max(0, m_source->findData(m_settings->studySetId())));

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
    updateFormatsSummary();
    refreshProgress();
}

void StudyPreferencesPage::applyCheckIns()
{
    if (m_syncing || !m_settings) {
        return;
    }
    m_settings->setCheckInsEnabled(m_checkIns->isChecked());
    m_settings->setCheckInMinutes(m_frequency->currentData().toInt());
    const qint64 source = m_source->currentData().toLongLong();
    if (source > 0) {
        m_settings->setStudySetId(source);
    }
    m_settings->save();
    refreshProgress();
    emit preferencesChanged();
}

void StudyPreferencesPage::applyFormatControls()
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
        syncFromSettings();
        return;
    }
    m_formatHint->hide();
    m_settings->setStudyFormatMode(m_focusBtn->isChecked() ? SettingsDefaults::kStudyFormatModeFocus
                                                           : SettingsDefaults::kStudyFormatModeMixed);
    m_settings->setStudyFocusFormat(m_focusCombo->currentData().toInt());
    m_settings->save();
    m_focusCombo->setEnabled(m_focusBtn->isChecked());
    updateFormatsSummary();
    emit preferencesChanged();
}

void StudyPreferencesPage::refreshProgress()
{
    if (!m_progressLabel) {
        return;
    }
    if (!m_tracker || !m_settings) {
        m_progressLabel->setText(QStringLiteral("No study answers yet"));
        return;
    }
    const StudyProgress p = m_tracker->progress(m_settings->studySetId());
    const int answered = p.attempted + p.selfAssessed;
    if (answered == 0) {
        m_progressLabel->setText(QStringLiteral("No study answers yet"));
        return;
    }
    QString text = QStringLiteral("%1 XP · %2 answered").arg(p.totalXp()).arg(answered);
    if (!p.reviewTopics.isEmpty()) {
        text += QStringLiteral(" · %1 to review").arg(p.reviewTopics.size());
    }
    m_progressLabel->setText(text);
}

void StudyPreferencesPage::updateModels(OllamaClient::SetupState state)
{
    const QString model = m_ollama ? m_ollama->model() : (m_settings ? m_settings->studyModel() : QString());
    const QStringList installed = m_ollama ? m_ollama->installedModels() : QStringList();
    QStringList models = installed;
    if (!model.isEmpty() && !models.contains(model)) {
        models.prepend(model);
    }
    m_syncing = true;
    m_modelCombo->clear();
    for (const QString& m : models) {
        m_modelCombo->addItem(installed.contains(m) ? m : QStringLiteral("%1 (not installed)").arg(m), m);
    }
    m_modelCombo->setCurrentIndex(std::max(0, m_modelCombo->findData(model)));
    m_syncing = false;
    m_modelCard->setVisible(state == OllamaClient::SetupState::Ready || state == OllamaClient::SetupState::ModelMissing);
}

bool StudyPreferencesPage::resetProgress(bool confirmed)
{
    if (!confirmed) {
        PixelConfirmDialog dlg(QStringLiteral("Reset study progress?"),
                               QStringLiteral("Study XP, answer history and review schedules go back to zero. Your documents and questions are kept."),
                               QStringLiteral("Reset"), QStringLiteral("Cancel"), this);
        const int res = dlg.exec();
        emit buttonActivated();
        if (res != QDialog::Accepted) {
            return false;
        }
    }
    if (m_tracker) {
        m_tracker->reset(0);
    }
    emit resetRequested();
    refreshProgress();
    return true;
}
