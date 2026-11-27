#include "StudyTestUtils.h"

#include "ai/OllamaClient.h"
#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "pet/PetWindow.h"
#include "quickactions/AnimatedQuickActionButton.h"
#include "quickactions/QuickActionMenu.h"
#include "study/ProgressTracker.h"
#include "study/QuestionGenerator.h"
#include "study/StudyDatabase.h"
#include "study/StudyLibrary.h"
#include "study/QuizController.h"
#include "study/QuizController.h"
#include "ui/QuizPopup.h"
#include "ui/PixelTheme.h"
#include "ui/CollapsibleSection.h"
#include "ui/QuestionEditor.h"
#include "ui/PixelSegmentedControl.h"
#include "ui/StudyLibraryWindow.h"
#include "ui/StudyPracticeView.h"
#include "ui/StudyPreferencesPage.h"
#include "ui/StudySetList.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QMimeData>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

namespace {

QString studyStorageDir()
{
    return QDir::tempPath() + QStringLiteral("/chiikawa-study-ui-tests-%1").arg(QCoreApplication::applicationPid());
}

[[maybe_unused]] const bool kIsolatedStores = [] {
    AppSettings::setStorageScope(QStringLiteral("ChiikawaStudyPet"), QStringLiteral("DesktopPetStudyUiTests"));
    StudyDatabase::setStorageDirectory(studyStorageDir());
    return true;
}();

const QRect kPet(700, 600, 120, 120);

Question choiceQuestion(QuestionType type = QuestionType::SingleChoice)
{
    Question q;
    q.id = type == QuestionType::SingleChoice ? QStringLiteral("sc") : QStringLiteral("ms");
    q.type = type;
    q.prompt = QStringLiteral("What does a LEFT JOIN return?");
    q.category = QStringLiteral("Databases");
    q.topic = QStringLiteral("LEFT JOIN");
    q.explanation = QStringLiteral("A LEFT JOIN keeps every row from the left table.");
    q.options = {
        {QStringLiteral("a"), QStringLiteral("Only rows that match in both tables"), false, QStringLiteral("That is an INNER JOIN.")},
        {QStringLiteral("b"), QStringLiteral("All rows from the left table plus matching right-table rows"), true, QStringLiteral("Every left row is kept.")},
        {QStringLiteral("c"), QStringLiteral("All rows from both tables"), type == QuestionType::MultiSelect, QStringLiteral("Full outer join.")},
        {QStringLiteral("d"), QStringLiteral("Only unmatched rows"), false, QStringLiteral("Matches are kept too.")},
    };
    q.sources.append({QStringLiteral("Lecture 6"), QStringLiteral("slide 3"), QStringLiteral("A LEFT JOIN returns every row from the left table.")});
    return q;
}

Question writtenQuestion()
{
    Question q;
    q.id = QStringLiteral("w");
    q.type = QuestionType::Written;
    q.prompt = QStringLiteral("Which play features the line \"To be, or not to be\"?");
    q.category = QStringLiteral("English");
    q.topic = QStringLiteral("Hamlet");
    q.expectedAnswer = QStringLiteral("Hamlet");
    q.keyConcepts = {QStringLiteral("Hamlet")};
    q.answers.append({QStringLiteral("Hamlet"), {}});
    q.explanation = QStringLiteral("Prince Hamlet speaks the soliloquy.");
    q.sources.append({QStringLiteral("English 210"), QStringLiteral("p. 14"), QStringLiteral("Hamlet's soliloquy.")});
    return q;
}

/** Adds a study set with a ready document and `count` generated-looking questions of one type. */
qint64 seedSet(StudyLibrary& library, const QString& name, int count, QuestionType type = QuestionType::SingleChoice)
{
    StudyDatabase* db = library.database();
    const qint64 set = db->addStudySet(name).value_or(0);
    const qint64 doc = db->addDocument(set, QStringLiteral("Deck"), QStringLiteral("deck.pptx"), DocumentKind::Pptx).value_or(0);
    db->setDocumentStatus(doc, DocumentStatus::Ready);
    for (int i = 0; i < count; ++i) {
        Question q = choiceQuestion(type);
        q.id = QStringLiteral("seed-%1-%2-%3").arg(set).arg(static_cast<int>(type)).arg(i);
        q.prompt = QStringLiteral("Question %1 of %2").arg(i + 1).arg(name);
        q.topic = QStringLiteral("topic %1").arg(i);
        q.setId = set;
        q.documentId = doc;
        db->saveQuestion(q, 0, QStringLiteral("test"));
    }
    return set;
}

/** Answers the open single-choice question correctly (option "b") and checks it. */
void answerCorrectly(QuizPopup* view)
{
    QVERIFY(view->phase() == QuizPopup::Phase::Asking);
    for (PixelOptionButton* option : view->optionButtons()) {
        if (option->property("optionId").toString() == QLatin1String("b")) {
            option->click();
        }
    }
    QVERIFY(view->submitButton()->isEnabled());
    view->submitButton()->click();
}

quint16 closedPort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 port = s.serverPort();
    s.close();
    return port;
}

/** Points the app's Ollama client at a closed port so tests never reach a real local model. */
void disconnectOllama(OllamaClient* client)
{
    QVERIFY(client);
    QVERIFY(client->setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort()))));
    client->setExecutableCandidates({});
}

} // namespace

class TestStudyUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

    void singleChoiceUsesRadiosAndMarksResult();
    void multiSelectUsesCheckboxesAndExplainsEveryOption();
    void writtenAnswerSuggestsJudgesAndReveals();
    void popupScrollsInsteadOfGrowingPastTheCap();
    void popupDoesNotStealFocusAndCanPostpone();
    void noticeOffersAction();
    void optionButtonsWrapLongText();

    void libraryWindowManagesSetsFormatsAndDocuments();
    void libraryListsSetsAndDrillsIntoOne();
    void libraryHidesTheReadyBannerAndCollapsesFormats();
    void libraryExplainsWhichFilesCanBeImported();
    void preferencesTabControlsCheckInsFormatsAndProgress();
    void practiceRunsASetFromItsPage();
    void studyTypographyMatchesTheSharedTheme();
    void practiceSkipEndAndFormatRules();
    void practicingEarlyKeepsTheReviewSchedule();
    void questionEditorValidatesEveryFormat();
    void ownQuestionsCanBeWrittenEditedPracticedAndDeleted();
    void petStudyFlowWorksWithoutOllama();
    void studyShortcutOpensTheStudyWindow();

private:
    static void clearSettings();
};

void TestStudyUi::clearSettings()
{
    QSettings s(AppSettings::storageOrganization(), AppSettings::storageApplication());
    s.clear();
    s.sync();
}

void TestStudyUi::initTestCase()
{
    clearSettings();
    QDir(studyStorageDir()).removeRecursively();
}

void TestStudyUi::cleanup()
{
    clearSettings();
}

void TestStudyUi::cleanupTestCase()
{
    QDir(studyStorageDir()).removeRecursively();
}

void TestStudyUi::singleChoiceUsesRadiosAndMarksResult()
{
    QuizPopup popup;
    QSignalSpy chosen(&popup, &QuizPopup::choiceSubmitted);
    popup.showQuestion(choiceQuestion(), kPet);
    QCOMPARE(popup.width(), QuizPopup::kStudyWidth);
    QVERIFY(popup.hintLabel()->isVisible());
    QVERIFY(popup.hintLabel()->text().contains(QStringLiteral("Choose one")));
    QVERIFY(!popup.revealButton()->isVisible());
    QCOMPARE(popup.submitButton()->text(), QStringLiteral("Check Answer"));
    const auto options = popup.optionButtons();
    QCOMPARE(options.size(), 4);
    for (PixelOptionButton* o : options) {
        QCOMPARE(o->indicator(), PixelOptionButton::Indicator::Radio);
        QCOMPARE(o->mark(), PixelOptionButton::Mark::None);   // no clues before answering
        QVERIFY(o->detail().isEmpty());
    }
    QVERIFY(!popup.submitButton()->isEnabled());

    options.at(0)->click();
    options.at(2)->click();
    QVERIFY(!options.at(0)->isChecked());   // radios are exclusive
    QVERIFY(options.at(2)->isChecked());
    QVERIFY(popup.submitButton()->isEnabled());
    popup.submitButton()->click();
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.first().first().toString(), QStringLiteral("c"));

    Evaluation e;
    e.verdict = Verdict::Incorrect;
    e.feedback = QStringLiteral("Full outer join. Answer: All rows from the left table plus matching right-table rows.");
    for (const QuestionOption& o : choiceQuestion().options) {
        e.options.append({o.id, o.id == QStringLiteral("c"), o.correct, o.explanation});
    }
    popup.showEvaluation(e, 0);
    QVERIFY(popup.isShowingResult());
    QVERIFY(popup.resultLabel()->text().startsWith(QStringLiteral("Not quite")));
    QCOMPARE(options.at(1)->mark(), PixelOptionButton::Mark::Correct);
    QCOMPARE(options.at(2)->mark(), PixelOptionButton::Mark::Incorrect);
    QCOMPARE(options.at(0)->mark(), PixelOptionButton::Mark::None);
    QCOMPARE(options.at(2)->detail(), QStringLiteral("Full outer join."));
    QVERIFY(options.at(0)->detail().isEmpty());   // unpicked wrong options stay quiet in single choice
    QCOMPARE(popup.sourceLabel()->text(), QStringLiteral("Source: Lecture 6 · slide 3"));
    // The picked and the right option are explained inline, so no duplicate feedback line.
    QVERIFY(!popup.feedbackLabel()->isVisible());
    QCOMPARE(popup.submitButton()->text(), QStringLiteral("Done"));
    QVERIFY(popup.remainingDismissMs() > 10000);   // results stay long enough to read
}

void TestStudyUi::multiSelectUsesCheckboxesAndExplainsEveryOption()
{
    QuizPopup popup;
    QSignalSpy selected(&popup, &QuizPopup::selectionSubmitted);
    popup.showQuestion(choiceQuestion(QuestionType::MultiSelect), kPet);
    QVERIFY(popup.hintLabel()->text().contains(QStringLiteral("Select all that apply")));
    const auto options = popup.optionButtons();
    for (PixelOptionButton* o : options) {
        QCOMPARE(o->indicator(), PixelOptionButton::Indicator::Check);
    }
    options.at(1)->click();
    options.at(3)->click();
    QVERIFY(options.at(1)->isChecked() && options.at(3)->isChecked());   // several can be checked
    popup.submitButton()->click();
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.first().first().toStringList(), (QStringList{QStringLiteral("b"), QStringLiteral("d")}));

    Evaluation e;
    e.verdict = Verdict::Partial;
    e.score = 0.0;
    for (const QuestionOption& o : choiceQuestion(QuestionType::MultiSelect).options) {
        e.options.append({o.id, o.id == QStringLiteral("b") || o.id == QStringLiteral("d"), o.correct, o.explanation});
    }
    popup.showEvaluation(e, 6);
    QVERIFY(popup.resultLabel()->text().contains(QStringLiteral("Partially correct · +6 XP")));
    QCOMPARE(options.at(1)->mark(), PixelOptionButton::Mark::Correct);
    QCOMPARE(options.at(2)->mark(), PixelOptionButton::Mark::Missed);
    QCOMPARE(options.at(3)->mark(), PixelOptionButton::Mark::Incorrect);
    for (PixelOptionButton* o : options) {
        QVERIFY(!o->detail().isEmpty());   // every statement explained
        QVERIFY(!o->isEnabled());
    }
}

void TestStudyUi::writtenAnswerSuggestsJudgesAndReveals()
{
    QuizPopup popup;
    QSignalSpy written(&popup, &QuizPopup::writtenSubmitted);
    QSignalSpy reveal(&popup, &QuizPopup::revealRequested);
    QSignalSpy rated(&popup, &QuizPopup::selfAssessed);
    popup.showQuestion(writtenQuestion(), kPet);
    QVERIFY(popup.writtenEdit()->isVisible());
    QVERIFY(popup.revealButton()->isVisible());
    QCOMPARE(popup.submitButton()->text(), QStringLiteral("Check My Answer"));
    QVERIFY(!popup.submitButton()->isEnabled());
    // Four buttons fit without clipping: Reveal and Check get their own row.
    for (QPushButton* b : {popup.laterButton(), popup.skipButton(), popup.revealButton(), popup.submitButton()}) {
        QVERIFY2(b->width() >= b->sizeHint().width(), qPrintable(b->text()));
    }
    QVERIFY(popup.submitButton()->y() > popup.skipButton()->y());

    popup.writtenEdit()->setPlainText(QStringLiteral("Hamelt"));
    QVERIFY(popup.submitButton()->isEnabled());
    QTest::keyClick(popup.writtenEdit(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(0).toString(), QStringLiteral("Hamelt"));
    QCOMPARE(written.at(0).at(1).toBool(), true);

    popup.showSuggestion(QStringLiteral("Hamlet"));
    QCOMPARE(popup.phase(), QuizPopup::Phase::Suggesting);
    QCOMPARE(popup.suggestionLabel()->text(), QStringLiteral("Did you mean “Hamlet”?"));
    QVERIFY(!popup.submitButton()->isEnabled());
    popup.keepAnswerButton()->click();
    QCOMPARE(written.at(1).at(0).toString(), QStringLiteral("Hamelt"));
    QCOMPARE(written.at(1).at(1).toBool(), false);
    popup.useSuggestionButton()->click();
    QCOMPARE(written.at(2).at(0).toString(), QStringLiteral("Hamlet"));
    QCOMPARE(popup.writtenEdit()->toPlainText(), QStringLiteral("Hamlet"));

    popup.showJudging();
    QCOMPARE(popup.submitButton()->text(), QStringLiteral("Checking…"));
    QVERIFY(!popup.submitButton()->isEnabled());
    QVERIFY(popup.writtenEdit()->isReadOnly());

    popup.revealButton()->click();
    QCOMPARE(reveal.count(), 1);
    popup.showReveal();
    QCOMPARE(popup.phase(), QuizPopup::Phase::Revealed);
    QVERIFY(popup.expectedLabel()->text().contains(QStringLiteral("Expected answer: Hamlet")));
    QVERIFY(popup.expectedLabel()->text().contains(QStringLiteral("Key points:")));
    QVERIFY(popup.sourceLabel()->text().contains(QStringLiteral("English 210 · p. 14")));
    QVERIFY(popup.selfAssessButton(SelfRating::GotIt)->isVisible());
    QVERIFY(!popup.submitButton()->isVisible());
    QCOMPARE(popup.remainingDismissMs(), 0);   // waits for the student's rating
    popup.selfAssessButton(SelfRating::PartlyGotIt)->click();
    QCOMPARE(rated.count(), 1);
    QCOMPARE(rated.first().first().toInt(), static_cast<int>(SelfRating::PartlyGotIt));

    popup.showEvaluation(StudyEval::selfAssessment(SelfRating::PartlyGotIt), 2);
    QVERIFY(popup.resultLabel()->text().contains(QStringLiteral("Partially got it · +2 XP")));
}

void TestStudyUi::popupScrollsInsteadOfGrowingPastTheCap()
{
    QCOMPARE(QuizPopup::heightCap(QRect(0, 0, 1920, 1040)), QuizPopup::kMaxHeight);
    QCOMPARE(QuizPopup::heightCap(QRect(0, 0, 1280, 500)), 300);
    QVERIFY(QuizPopup::heightCap(QRect(0, 0, 800, 200)) >= 200);

    Question big = choiceQuestion(QuestionType::MultiSelect);
    big.prompt = QString(QStringLiteral("Which of these long statements about relational joins are supported? ")).repeated(4);
    for (QuestionOption& o : big.options) {
        o.text = o.text + QStringLiteral(" — ") + QString(QStringLiteral("with a long qualifying clause that wraps across lines ")).repeated(3);
    }
    QuizPopup popup;
    popup.showQuestion(big, kPet);
    QScreen* screen = QGuiApplication::screenAt(kPet.center());
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    QVERIFY2(popup.height() <= QuizPopup::heightCap(avail), qPrintable(QString::number(popup.height())));
    QVERIFY(popup.scrollArea()->verticalScrollBar()->maximum() > 0);   // content scrolls rather than clipping
    QVERIFY(avail.contains(popup.geometry()));
    // Short questions are not padded out to the cap.
    QuizPopup small;
    small.showQuestion(writtenQuestion(), kPet);
    QVERIFY(small.height() < popup.height());
    QCOMPARE(small.scrollArea()->verticalScrollBar()->maximum(), 0);
}

void TestStudyUi::popupDoesNotStealFocusAndCanPostpone()
{
    QWidget host;
    host.resize(200, 100);
    host.show();
    host.activateWindow();
    if (!QTest::qWaitForWindowActive(&host)) {
        QSKIP("Window activation is unavailable on this platform plugin");
    }
    QuizPopup popup;
    QSignalSpy later(&popup, &QuizPopup::postponeRequested);
    QSignalSpy skipped(&popup, &QuizPopup::skipRequested);
    QSignalSpy closed(&popup, &QuizPopup::closed);
    popup.showQuestion(writtenQuestion(), kPet);
    QTest::qWait(100);
    QCOMPARE(QApplication::activeWindow(), &host);
    QVERIFY(!popup.writtenEdit()->hasFocus());
    QTest::mouseClick(popup.writtenEdit()->viewport(), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(popup.writtenEdit()->hasFocus(), 1000);

    popup.laterButton()->click();
    QCOMPARE(later.count(), 1);
    QCOMPARE(skipped.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 1000);
}

void TestStudyUi::noticeOffersAction()
{
    QuizPopup popup;
    QSignalSpy action(&popup, &QuizPopup::noticeActionTriggered);
    QSignalSpy skipped(&popup, &QuizPopup::skipRequested);
    popup.showNotice(QStringLiteral("Your Study Library is empty"), QStringLiteral("Import lecture slides."),
                     QStringLiteral("Open Study Library"), kPet);
    QCOMPARE(popup.phase(), QuizPopup::Phase::Notice);
    QVERIFY(!popup.skipButton()->isVisible());
    QVERIFY(!popup.laterButton()->isVisible());
    QCOMPARE(popup.submitButton()->text(), QStringLiteral("Open Study Library"));
    QVERIFY(popup.remainingDismissMs() > 0);
    popup.submitButton()->click();
    QCOMPARE(action.count(), 1);
    QCOMPARE(skipped.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 1000);
}

void TestStudyUi::optionButtonsWrapLongText()
{
    PixelOptionButton shortOption(QStringLiteral("Yes"), PixelOptionButton::Indicator::Radio);
    PixelOptionButton longOption(QString(QStringLiteral("a long option that needs several lines ")).repeated(5),
                                 PixelOptionButton::Indicator::Check);
    QVERIFY(longOption.hasHeightForWidth());
    QVERIFY(longOption.heightForWidth(260) > 2 * shortOption.heightForWidth(260));
    const int before = longOption.heightForWidth(260);
    longOption.setDetail(QStringLiteral("An explanation shown after grading."));
    QVERIFY(longOption.heightForWidth(260) > before);
    QVERIFY(longOption.isCheckable());
    QVERIFY(shortOption.autoExclusive());
    QVERIFY(!longOption.autoExclusive());
}

void TestStudyUi::libraryWindowManagesSetsFormatsAndDocuments()
{
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);
    QTRY_COMPARE_WITH_TIMEOUT(ollama.setupState(), OllamaClient::SetupState::NotInstalled, 5000);
    QVERIFY(window.getOllamaButton()->isVisible());
    QVERIFY(!window.startOllamaButton()->isVisible());
    QVERIFY(!window.downloadModelButton()->isVisible());
    QVERIFY(window.setupLabel()->text().contains(QStringLiteral("ollama.com")));

    // Courses
    QCOMPARE(window.currentSet(), 0);
    const qint64 studySet = window.createSet(QStringLiteral("CS 340"));
    QVERIFY(studySet > 0);
    QCOMPARE(window.currentSet(), studySet);
    QCOMPARE(settings.studySetId(), studySet);
    QCOMPARE(window.createSet(QStringLiteral("cs 340")), 0);   // duplicate name rejected
    QVERIFY(window.renameCurrentSet(QStringLiteral("CS 340 Databases")));
    QCOMPARE(window.currentSetName(), QStringLiteral("CS 340 Databases"));

    // Formats: at least one stays on; Focus mode is saved.
    window.formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->setChecked(false);
    window.formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(false);
    QCOMPARE(settings.studyFormats(), SettingsDefaults::kStudyFormatSingleChoice);
    window.formatCheck(SettingsDefaults::kStudyFormatSingleChoice)->setChecked(false);
    QVERIFY(window.formatCheck(SettingsDefaults::kStudyFormatSingleChoice)->isChecked());
    QCOMPARE(settings.studyFormats(), SettingsDefaults::kStudyFormatSingleChoice);
    window.formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    window.focusModeButton()->click();
    window.focusCombo()->setCurrentIndex(window.focusCombo()->findData(SettingsDefaults::kStudyFormatWritten));
    QCOMPARE(settings.studyFormatMode(), SettingsDefaults::kStudyFormatModeFocus);
    QCOMPARE(settings.studyFocusFormat(), SettingsDefaults::kStudyFormatWritten);
    AppSettings reloaded;
    QCOMPARE(reloaded.studyFormats(), SettingsDefaults::kStudyFormatSingleChoice | SettingsDefaults::kStudyFormatWritten);

    // Documents
    QTemporaryDir inputs;
    const QString deck = inputs.filePath(QStringLiteral("lecture.pptx"));
    QFile f(deck);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(StudyTestUtils::buildPptx({{QStringLiteral("Joins"), {QStringLiteral("A LEFT JOIN keeps every row from the left table.")}, QString(), false}}));
    f.close();
    QSignalSpy processed(&library, &StudyLibrary::documentProcessed);
    window.importFiles({deck});
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.documentRows().size(), 1, 2000);
    const qint64 doc = window.documentRows().first();
    QVERIFY(window.documentMeta(doc)->text().contains(QStringLiteral("1 slide")));
    QVERIFY(window.documentMeta(doc)->text().contains(QStringLiteral("Text ready")));
    // Without a model, generation is unavailable but the document is ready.
    QVERIFY(!window.generateButton()->isEnabled());
    window.documentCheck(doc)->setChecked(false);
    QCOMPARE(library.database()->document(doc)->selected, false);

    QVERIFY(window.removeDocument(doc, true));
    QTRY_VERIFY_WITH_TIMEOUT(window.documentRows().isEmpty(), 2000);
    QVERIFY(window.removeCurrentSet(true));
    QCOMPARE(window.currentSet(), 0);
    QCOMPARE(settings.studySetId(), 0);
}

void TestStudyUi::studyShortcutOpensTheStudyWindow()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.save();
    }
    PetWindow pet;
    pet.quickActionMenu()->setNativeOutsideObserverEnabled(false);
    pet.quickActionMenu()->setReducedMotionOverride(1);   // open instantly
    pet.showPet();
    QApplication::processEvents();
    QVERIFY(!pet.studyLibraryWindow());

    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(pet.rect().center()),
                    QPointF(pet.mapToGlobal(pet.rect().center())), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&pet, &dbl);
    QCOMPARE(pet.quickActionMenu()->state(), QuickActionMenu::State::Open);
    QVERIFY(pet.quickActionMenu()->studyButton());
    // Study sits immediately above Settings in the radial stack.
    QCOMPARE(pet.quickActionMenu()->indexOfId(QStringLiteral("builtin:study")) + 1,
             pet.quickActionMenu()->indexOfId(QStringLiteral("builtin:settings")));
    pet.quickActionMenu()->studyButton()->click();
    QVERIFY(pet.studyLibraryWindow());
    QVERIFY(pet.studyLibraryWindow()->isVisible());
    disconnectOllama(pet.ollamaClient());
    pet.studyLibraryWindow()->hide();
}

void TestStudyUi::libraryListsSetsAndDrillsIntoOne()
{
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);

    // Nothing yet: the list explains how to start and shows no rows.
    QVERIFY(!window.isShowingSet());
    QVERIFY(window.setList()->ids().isEmpty());
    QVERIFY(window.titleLabel()->text().contains(QStringLiteral("Study Library")));
    QVERIFY(!window.backButton()->isVisibleTo(&window));

    const qint64 a = library.database()->addStudySet(QStringLiteral("Biology")).value_or(0);
    const qint64 b = library.database()->addStudySet(QStringLiteral("Databases")).value_or(0);
    QVERIFY(a > 0 && b > 0);
    window.showNear(kPet);   // reopening reloads the list
    QCOMPARE(window.setList()->ids().size(), 2);
    QVERIFY(window.setList()->metaText(a).contains(QStringLiteral("0 documents")));
    QVERIFY(!window.isShowingSet());

    // Clicking a row opens that set's materials; Back returns to the list.
    QFrame* row = window.setList()->rowFor(b);
    QVERIFY(row);
    QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, row->rect().center());
    QVERIFY(window.isShowingSet());
    QCOMPARE(window.currentSet(), b);
    QCOMPARE(window.currentSetName(), QStringLiteral("Databases"));
    QVERIFY(window.titleLabel()->text().contains(QStringLiteral("Databases")));
    QVERIFY(window.backButton()->isVisibleTo(&window));
    QCOMPARE(settings.studySetId(), b);
    window.backButton()->click();
    QVERIFY(!window.isShowingSet());
    QVERIFY(window.titleLabel()->text().contains(QStringLiteral("Study Library")));

    // Row actions work on their own set without opening it.
    QVERIFY(window.renameSet(a, QStringLiteral("Cell Biology")));
    QCOMPARE(window.setList()->ids().size(), 2);
    QVERIFY(window.removeSet(a, true));
    QCOMPARE(window.setList()->ids(), QList<qint64>{b});
    QVERIFY(!window.isShowingSet());

    // Creating a set goes straight to its materials.
    const qint64 c = window.createSet(QStringLiteral("Networks"));
    QVERIFY(c > 0);
    QVERIFY(window.isShowingSet());
    QCOMPARE(window.currentSet(), c);
    QVERIFY(window.removeSet(c, true));
    QVERIFY(window.removeSet(b, true));
    QVERIFY(window.setList()->ids().isEmpty());
}

void TestStudyUi::libraryHidesTheReadyBannerAndCollapsesFormats()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);
    QTRY_COMPARE_WITH_TIMEOUT(ollama.setupState(), OllamaClient::SetupState::NotInstalled, 5000);
    QVERIFY(window.setupCard()->isVisibleTo(&window));   // the user has something to do

    // A working model needs no banner at all.
    emit ollama.setupStateChanged(OllamaClient::SetupState::Ready);
    QVERIFY(!window.setupCard()->isVisibleTo(&window));
    QVERIFY(!window.setupLabel()->isVisibleTo(&window));
    emit ollama.setupStateChanged(OllamaClient::SetupState::NotRunning);
    QVERIFY(window.setupCard()->isVisibleTo(&window));

    // Question formats: collapsed, multiple choice by default, expandable.
    QCOMPARE(settings.studyFormats(), SettingsDefaults::kStudyFormatSingleChoice);
    const qint64 id = window.createSet(QStringLiteral("Formats"));
    QVERIFY(id > 0);
    QVERIFY(window.isShowingSet());
    QVERIFY(!window.formatsExpanded());
    QVERIFY(window.formatsSection()->summaryLabel()->text().contains(QStringLiteral("Multiple choice")));
    QVERIFY(!window.formatsSection()->summaryLabel()->text().contains(QStringLiteral("Written")));
    QVERIFY(window.formatCheck(SettingsDefaults::kStudyFormatSingleChoice)->isChecked());
    QVERIFY(!window.formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->isChecked());
    QVERIFY(!window.formatCheck(SettingsDefaults::kStudyFormatWritten)->isChecked());
    window.formatsSection()->setExpanded(!window.formatsExpanded(), false);
    QVERIFY(window.formatsExpanded());
    window.formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    QVERIFY(window.formatsSection()->summaryLabel()->text().contains(QStringLiteral("Written answer")));
    window.formatsSection()->setExpanded(!window.formatsExpanded(), false);
    QVERIFY(!window.formatsExpanded());
    QVERIFY(window.removeCurrentSet(true));
}

void TestStudyUi::libraryExplainsWhichFilesCanBeImported()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);
    const qint64 id = window.createSet(QStringLiteral("Imports"));
    QVERIFY(id > 0);

    const QString types = window.importTypesLabel()->text();
    QVERIFY(types.contains(QStringLiteral(".pptx")));
    QVERIFY(types.contains(QStringLiteral(".txt")));
    QVERIFY(types.contains(QStringLiteral(".md")));
    QVERIFY(types.contains(QStringLiteral("PDF")));
    QCOMPARE(window.importButton()->text(), QStringLiteral("Choose files…"));
    QVERIFY(window.importPptxButton()->isEnabled());
    QVERIFY(window.importTextButton()->isEnabled());

    // Unsupported files are named in the message instead of being silently dropped.
    window.importFiles({QStringLiteral("C:/notes/report.docx"), QStringLiteral("C:/notes/old.ppt")});
    const QString message = window.statusLabel()->text();
    QVERIFY(message.contains(QStringLiteral("report.docx")));
    QVERIFY(message.contains(QStringLiteral("old.ppt")));
    QVERIFY(message.contains(QStringLiteral(".pptx")));
    QVERIFY(message.contains(QStringLiteral("Save older")));
    QVERIFY(window.documentRows().isEmpty());
    QVERIFY(window.removeCurrentSet(true));
}

void TestStudyUi::preferencesTabControlsCheckInsFormatsAndProgress()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);

    // Two tabs; Library first, Preferences second.
    QCOMPARE(window.tabs()->count(), 2);
    QCOMPARE(window.currentTab(), 0);
    window.tabs()->button(1)->click();
    QCOMPARE(window.currentTab(), 1);
    StudyPreferencesPage* prefs = window.preferences();
    QVERIFY(prefs);
    window.showTab(0);
    QCOMPARE(window.currentTab(), 0);

    // Chiikawa's questions: off by default, saved immediately, and the app is told.
    QVERIFY(!settings.checkInsEnabled());
    QVERIFY(!prefs->checkInsCheck()->isChecked());
    QSignalSpy changed(&window, &StudyLibraryWindow::preferencesChanged);
    prefs->checkInsCheck()->setChecked(true);
    QVERIFY(settings.checkInsEnabled());
    QVERIFY(changed.count() >= 1);
    prefs->frequencyCombo()->setCurrentIndex(prefs->frequencyCombo()->findData(30));
    QCOMPARE(settings.checkInMinutes(), 30);
    AppSettings reloaded;
    QVERIFY(reloaded.checkInsEnabled());
    QCOMPARE(reloaded.checkInMinutes(), 30);

    // The study set Chiikawa draws from follows the library.
    const qint64 a = library.database()->addStudySet(QStringLiteral("Alpha")).value_or(0);
    const qint64 b = library.database()->addStudySet(QStringLiteral("Beta")).value_or(0);
    window.showNear(kPet);
    QVERIFY(prefs->sourceCombo()->isEnabled());
    QVERIFY(prefs->sourceCombo()->findData(a) >= 0);
    prefs->sourceCombo()->setCurrentIndex(prefs->sourceCombo()->findData(b));
    QCOMPARE(settings.studySetId(), b);

    // Ask-now is forwarded to the app.
    QSignalSpy ask(&window, &StudyLibraryWindow::askQuestionRequested);
    prefs->askNowButton()->click();
    QCOMPARE(ask.count(), 1);

    // Formats live here: multiple choice by default, summary follows changes.
    QVERIFY(prefs->formatsSummary().contains(QStringLiteral("Multiple choice")));
    QVERIFY(!prefs->formatsExpanded());
    prefs->formatsSection()->setExpanded(true, false);
    QVERIFY(prefs->formatsExpanded());
    prefs->formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    QVERIFY(prefs->formatsSection()->summaryLabel()->text().contains(QStringLiteral("Written answer")));
    QVERIFY(window.practiceHint()->text().contains(QStringLiteral("written answer")));
    prefs->formatCheck(SettingsDefaults::kStudyFormatSingleChoice)->setChecked(false);
    prefs->formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(false);   // would leave none on
    QVERIFY(settings.studyFormats() != 0);

    // Reset progress clears answers, keeps questions, and is announced.
    QCOMPARE(prefs->progressLabel()->text(), QStringLiteral("No study answers yet"));
    QSignalSpy reset(&window, &StudyLibraryWindow::progressResetRequested);
    QVERIFY(prefs->resetProgress(true));
    QCOMPARE(reset.count(), 1);

    // The model card only appears when a model can be picked or downloaded.
    emit ollama.setupStateChanged(OllamaClient::SetupState::NotInstalled);
    QVERIFY(!prefs->modelCard()->isVisibleTo(prefs));
    emit ollama.setupStateChanged(OllamaClient::SetupState::ModelMissing);
    QVERIFY(prefs->modelCard()->isVisibleTo(prefs));

    QVERIFY(window.removeSet(a, true));
    QVERIFY(window.removeSet(b, true));
}

void TestStudyUi::practiceRunsASetFromItsPage()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);

    const qint64 empty = window.createSet(QStringLiteral("Empty"));
    QVERIFY(empty > 0);
    QVERIFY(!window.practiceButton()->isEnabled());   // nothing to practise yet
    QVERIFY(!window.startPractice());
    window.showSetList();

    const qint64 set = seedSet(library, QStringLiteral("Joins"), 3);
    window.showNear(kPet);
    window.openSet(set);
    QVERIFY(window.practiceButton()->isEnabled());
    QVERIFY(window.practiceButton()->text().contains(QStringLiteral("(3)")));

    window.practiceButton()->click();
    QVERIFY(window.isPracticing());
    QVERIFY(!window.isShowingSet() || window.titleLabel()->text().contains(QStringLiteral("Practice")));
    StudyPracticeView* view = window.practiceView();
    QVERIFY(view->isActive());
    QCOMPARE(view->progressLabel()->text(), QStringLiteral("Question 1 of 3"));

    for (int i = 0; i < 3; ++i) {
        QuizPopup* qv = view->questionView();
        QVERIFY(qv->isEmbedded());
        QCOMPARE(view->progressLabel()->text(), QStringLiteral("Question %1 of 3").arg(i + 1));
        answerCorrectly(qv);
        QVERIFY(qv->phase() == QuizPopup::Phase::Result);
        QCOMPARE(qv->submitButton()->text(), i < 2 ? QStringLiteral("Next question") : QStringLiteral("Finish"));
        qv->submitButton()->click();   // advance
    }
    QVERIFY(view->showingSummary());
    QCOMPARE(view->summary().answered, 3);
    QCOMPARE(view->summary().correct, 3);
    QVERIFY(view->summary().xp > 0);
    QVERIFY(view->summaryLabel()->text().contains(QStringLiteral("3 of 3")));

    // Every answer was recorded with a rating and moved its card out of "new".
    QSqlQuery q(library.database()->connection());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM attempts WHERE rating IS NOT NULL")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 3);
    q.finish();
    const QVector<Question> questions = library.database()->setQuestions(set, {}, true);
    QCOMPARE(questions.size(), 3);
    for (const Question& question : questions) {
        QCOMPARE(tracker.card(question.id).state, CardState::Review);
    }
    // Practising does not touch the pet's "recently asked" memory.
    QVERIFY(settings.recentQuestionIds().isEmpty());

    view->backButton()->click();
    QVERIFY(!window.isPracticing());
    QVERIFY(window.isShowingSet());
    QVERIFY(window.removeSet(set, true));
    QVERIFY(window.removeSet(empty, true));
}

void TestStudyUi::practiceSkipEndAndFormatRules()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);

    // Two multiple choice and one multiple select question in the same set.
    const qint64 set = seedSet(library, QStringLiteral("Formats"), 2);
    {
        StudyDatabase* db = library.database();
        const qint64 doc = db->documents(set).first().id;
        Question multi = choiceQuestion(QuestionType::MultiSelect);
        multi.id = QStringLiteral("multi-1");
        multi.setId = set;
        multi.documentId = doc;
        db->saveQuestion(multi, 0, QStringLiteral("test"));
    }
    window.showNear(kPet);
    window.openSet(set);
    // Multiple choice is the default, so only the two single-choice questions are offered.
    QVERIFY(window.practiceButton()->text().contains(QStringLiteral("(2)")));
    window.preferences()->formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->setChecked(true);
    QVERIFY(window.practiceButton()->text().contains(QStringLiteral("(3)")));
    window.preferences()->formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->setChecked(false);
    QVERIFY(window.practiceButton()->text().contains(QStringLiteral("(2)")));

    // Skip the first question, answer the second, then end the session early.
    QVERIFY(window.startPractice());
    StudyPracticeView* view = window.practiceView();
    view->questionView()->skipButton()->click();
    QCOMPARE(view->progressLabel()->text(), QStringLiteral("Question 2 of 2"));
    answerCorrectly(view->questionView());
    QCOMPARE(view->questionView()->submitButton()->text(), QStringLiteral("Finish"));
    view->endButton()->click();   // ends with answers: shows the summary
    QVERIFY(view->showingSummary());
    QCOMPARE(view->summary().skipped, 1);
    QCOMPARE(view->summary().answered, 1);
    QVERIFY(view->summaryLabel()->text().contains(QStringLiteral("1 skipped")));

    // Practice again starts a fresh session; ending without answering just leaves.
    view->againButton()->click();
    QVERIFY(!view->showingSummary());
    QCOMPARE(view->summary().answered, 0);
    view->endButton()->click();
    QVERIFY(!window.isPracticing());

    // Leaving through Back drops a session without a summary.
    QVERIFY(window.startPractice());
    window.backButton()->click();
    QVERIFY(!window.isPracticing());
    QVERIFY(window.isShowingSet());
    QVERIFY(window.removeSet(set, true));
}

void TestStudyUi::practicingEarlyKeepsTheReviewSchedule()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);

    const qint64 set = seedSet(library, QStringLiteral("Schedule"), 2);
    window.showNear(kPet);
    window.openSet(set);
    QVERIFY(window.startPractice());
    StudyPracticeView* view = window.practiceView();
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 2; ++i) {
            answerCorrectly(view->questionView());
            view->questionView()->submitButton()->click();
        }
        QVERIFY(view->showingSummary());
        if (round == 0) {
            view->againButton()->click();   // second session starts right away
        }
    }
    // The second pass was early (cards were scheduled for tomorrow): attempts and XP are kept,
    // but each card was only scheduled once.
    QSqlQuery q(library.database()->connection());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM attempts")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 4);
    q.finish();
    for (const Question& question : library.database()->setQuestions(set, {}, true)) {
        QCOMPARE(tracker.card(question.id).reps, 1);
    }
    window.practiceView()->abandon();
    QVERIFY(window.removeSet(set, true));
}

void TestStudyUi::studyTypographyMatchesTheSharedTheme()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);
    const qint64 set = seedSet(library, QStringLiteral("Fonts"), 1);
    window.showNear(kPet);
    window.openSet(set);
    window.showTab(1);
    QApplication::processEvents();

    // Reference controls: built under the same shared style sheet the Settings window uses.
    QWidget host;
    host.setStyleSheet(PixelTheme::styleSheet());
    auto* refCheck = new PixelCheckBox(QStringLiteral("x"), &host);
    auto* refOption = new PixelOptionButton(QStringLiteral("x"), PixelOptionButton::Indicator::Radio, &host);
    auto* refCombo = new QComboBox(&host);
    auto* refLabel = new QLabel(QStringLiteral("x"), &host);
    auto* refTitle = new QLabel(QStringLiteral("x"), &host);
    refTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto* refMuted = new QLabel(QStringLiteral("x"), &host);
    refMuted->setObjectName(QStringLiteral("muted"));
    auto* refButton = new QPushButton(QStringLiteral("x"), &host);
    host.show();
    const auto same = [](const QFont& a, const QFont& b) {
        return a.family() == b.family() && a.pixelSize() == b.pixelSize() && a.weight() == b.weight();
    };
    const auto check = [&](QWidget* study, QWidget* reference, const char* what) {
        study->ensurePolished();
        reference->ensurePolished();
        QVERIFY2(same(study->font(), reference->font()),
                 qPrintable(QStringLiteral("%1: %2 %3px w%4 vs %5 %6px w%7")
                                .arg(QLatin1String(what), study->font().family()).arg(study->font().pixelSize()).arg(study->font().weight())
                                .arg(reference->font().family()).arg(reference->font().pixelSize()).arg(reference->font().weight())));
    };

    StudyPreferencesPage* prefs = window.preferences();
    check(prefs->checkInsCheck(), refCheck, "preferences checkbox");
    check(prefs->formatCheck(SettingsDefaults::kStudyFormatWritten), refCheck, "format checkbox");
    check(prefs->mixedModeButton(), refOption, "radio option");
    check(prefs->frequencyCombo(), refCombo, "frequency combo");
    check(prefs->sourceCombo(), refCombo, "set combo");
    check(prefs->focusCombo(), refCombo, "format combo");
    check(prefs->askNowButton(), refButton, "button");
    check(prefs->formatsSection()->titleLabel(), refTitle, "section title");
    for (QLabel* label : prefs->findChildren<QLabel*>()) {
        if (label->objectName() == QLatin1String("muted")) {
            check(label, refMuted, "muted label");
        } else if (label->objectName().isEmpty() && label->text().size() < 14) {
            check(label, refLabel, "plain label");
        }
    }
    // The painted controls take their font from the theme, not the OS default.
    QVERIFY(same(refCheck->font(), PixelTheme::uiFont()));
    QVERIFY(same(refOption->font(), PixelTheme::uiFont()));

    // The set page and its dropdown-style controls use the same classes.
    for (QPushButton* chip : window.findChildren<QPushButton*>(QStringLiteral("chip"))) {
        check(chip, refButton, "chip");
    }
    QVERIFY(window.removeSet(set, true));
}

void TestStudyUi::questionEditorValidatesEveryFormat()
{
    using Input = QuestionEditor::Input;
    Question q;
    QString error;

    Input in;
    QVERIFY(!QuestionEditor::build(in, q, error));
    QVERIFY(error.contains(QStringLiteral("question")));

    // Multiple choice
    in.prompt = QStringLiteral("What does a LEFT JOIN keep?");
    in.options = {QStringLiteral("Left rows"), QString(), QString(), QString()};
    in.correct = {true, false, false, false};
    QVERIFY(!QuestionEditor::build(in, q, error));   // only one answer
    QVERIFY(error.contains(QStringLiteral("two answers")));
    in.options = {QStringLiteral("Left rows"), QStringLiteral("Right rows"), QString(), QString()};
    in.correct = {false, false, false, false};
    QVERIFY(!QuestionEditor::build(in, q, error));   // nothing ticked
    in.correct = {true, true, false, false};
    QVERIFY(!QuestionEditor::build(in, q, error));   // two correct for single choice
    in.options = {QStringLiteral("Left rows"), QStringLiteral(" left  ROWS "), QString(), QString()};
    in.correct = {true, false, false, false};
    QVERIFY(!QuestionEditor::build(in, q, error));   // duplicate answers
    QVERIFY(error.contains(QStringLiteral("same")));
    in.options = {QStringLiteral("Left rows"), QStringLiteral("Right rows"), QString(), QStringLiteral("All rows")};
    in.correct = {true, false, false, false};
    QVERIFY2(QuestionEditor::build(in, q, error), qPrintable(error));
    QCOMPARE(q.options.size(), 3);   // blank rows are skipped
    QCOMPARE(q.options.at(2).id, QStringLiteral("c"));
    QCOMPARE(q.type, QuestionType::SingleChoice);
    QVERIFY(q.isValid() == false);   // the id is assigned by the library, not the editor
    QVERIFY(!q.topic.isEmpty());     // defaults to the start of the question

    // Multiple select: at least one, but not all, correct
    in.type = QuestionType::MultiSelect;
    in.correct = {true, true, false, true};
    QVERIFY(!QuestionEditor::build(in, q, error));
    in.correct = {false, false, false, false};
    QVERIFY(!QuestionEditor::build(in, q, error));
    in.correct = {true, false, false, true};
    QVERIFY2(QuestionEditor::build(in, q, error), qPrintable(error));
    QCOMPARE(q.type, QuestionType::MultiSelect);

    // Written answer
    Input written;
    written.type = QuestionType::Written;
    written.prompt = QStringLiteral("Which join keeps unmatched left rows?");
    QVERIFY(!QuestionEditor::build(written, q, error));
    written.expected = QStringLiteral("LEFT JOIN");
    written.alsoAccept = {QStringLiteral("left outer join"), QStringLiteral("LEFT JOIN"), QStringLiteral("left outer join")};
    QVERIFY2(QuestionEditor::build(written, q, error), qPrintable(error));
    QCOMPARE(q.expectedAnswer, QStringLiteral("LEFT JOIN"));
    QCOMPARE(q.answers.size(), 1);
    QCOMPARE(q.answers.first().aliases, QStringList{QStringLiteral("left outer join")});
    QCOMPARE(q.keyConcepts, QStringList{QStringLiteral("LEFT JOIN")});   // defaults to the answer
    written.keyPoints = {QStringLiteral("keeps unmatched left rows")};
    QVERIFY(QuestionEditor::build(written, q, error));
    QCOMPARE(q.keyConcepts, QStringList{QStringLiteral("keeps unmatched left rows")});
}

void TestStudyUi::ownQuestionsCanBeWrittenEditedPracticedAndDeleted()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    QuestionGenerator generator(library.database(), &ollama);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);
    StudyDatabase* db = library.database();

    const qint64 set = window.createSet(QStringLiteral("Mine"));
    QVERIFY(set > 0);
    QVERIFY(window.writeQuestionButton()->isEnabled());
    QVERIFY(!window.practiceButton()->isEnabled());   // nothing to practise yet

    // The editor takes over the page; cancelling brings the documents back.
    QVERIFY(!window.questionEditor()->isVisibleTo(&window));
    window.writeQuestionButton()->click();
    QVERIFY(window.questionEditor()->isVisibleTo(&window));
    QVERIFY(!window.importButton()->isVisibleTo(&window));
    window.questionEditor()->cancelButton()->click();
    QVERIFY(!window.questionEditor()->isVisibleTo(&window));
    QVERIFY(window.importButton()->isVisibleTo(&window));

    // A rejected question shows why and stores nothing.
    window.writeQuestionButton()->click();
    window.questionEditor()->saveButton()->click();
    QVERIFY(window.questionEditor()->errorLabel()->isVisibleTo(window.questionEditor()));
    QVERIFY(db->documents(set).isEmpty());

    // Write a multiple choice question through the form.
    QuestionEditor::Input in;
    in.prompt = QStringLiteral("What does a LEFT JOIN keep?");
    in.options = {QStringLiteral("Every left row"), QStringLiteral("Only matches"), QString(), QString()};
    in.correct = {true, false, false, false};
    in.explanation = QStringLiteral("Unmatched left rows get NULLs on the right.");
    window.questionEditor()->setInput(in);
    window.questionEditor()->saveButton()->click();
    QVERIFY(!window.questionEditor()->isVisibleTo(&window));
    const QVector<DocumentRecord> docs = db->documents(set);
    QCOMPARE(docs.size(), 1);
    QCOMPARE(docs.first().kind, DocumentKind::Manual);
    QCOMPARE(docs.first().questionCount, 1);
    QVERIFY(!window.documentRows().isEmpty());
    const QVector<Question> stored = db->documentQuestions(docs.first().id);
    QCOMPARE(stored.size(), 1);
    const QString id = stored.first().id;
    QVERIFY(id.startsWith(QStringLiteral("m-")));
    QCOMPARE(stored.first().options.size(), 2);
    QVERIFY(stored.first().options.first().correct);
    QCOMPARE(stored.first().explanation, QStringLiteral("Unmatched left rows get NULLs on the right."));

    // Other formats, added directly (the same path the form uses).
    Question multi;
    multi.type = QuestionType::MultiSelect;
    multi.prompt = QStringLiteral("Which are outer joins?");
    multi.topic = QStringLiteral("joins");
    multi.options = {{QStringLiteral("a"), QStringLiteral("LEFT"), true, {}}, {QStringLiteral("b"), QStringLiteral("RIGHT"), true, {}},
                     {QStringLiteral("c"), QStringLiteral("INNER"), false, {}}};
    QVERIFY(window.saveOwnQuestion(multi, false));
    Question written;
    written.type = QuestionType::Written;
    written.prompt = QStringLiteral("Name the join that keeps all left rows.");
    written.topic = QStringLiteral("joins");
    written.expectedAnswer = QStringLiteral("LEFT JOIN");
    written.answers.append({QStringLiteral("LEFT JOIN"), {QStringLiteral("left outer join")}});
    written.keyConcepts = {QStringLiteral("LEFT JOIN")};
    QVERIFY(window.saveOwnQuestion(written, false));
    QCOMPARE(db->documents(set).size(), 1);   // all three share one "My questions" document
    QCOMPARE(db->documents(set).first().questionCount, 3);

    // They are practised like any other question (multiple choice by default).
    QVERIFY(window.practiceButton()->isEnabled());
    QVERIFY(window.practiceButton()->text().contains(QStringLiteral("(1)")));
    window.preferences()->formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->setChecked(true);
    window.preferences()->formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    QVERIFY(window.practiceButton()->text().contains(QStringLiteral("(3)")));
    QVERIFY(window.startPractice());
    QVERIFY(window.practiceView()->isActive());
    window.practiceView()->abandon();
    window.showSetList();
    window.openSet(set);

    // Generate and Regenerate never touch what you wrote.
    QVERIFY(!generator.start({docs.first().id}, {QuestionType::SingleChoice}, true));
    QCOMPARE(db->documentQuestions(docs.first().id).size(), 3);
    QVERIFY(!window.removeDocument(docs.first().id, true));   // questions are deleted one by one
    QVERIFY(!db->removeQuestions(docs.first().id));
    QCOMPARE(db->documentQuestions(docs.first().id).size(), 3);
    QVERIFY(!window.generateButton()->isEnabled());

    // Editing keeps the question's ID and its answer history.
    Question original = *db->question(id);
    Evaluation correct;
    correct.verdict = Verdict::Correct;
    correct.score = 1.0;
    QVERIFY(tracker.record(original, correct) > 0);
    QVERIFY(window.editOwnQuestion(id));
    QVERIFY(window.questionEditor()->isEditingExisting());
    QCOMPARE(window.questionEditor()->editingId(), id);
    QVERIFY(window.questionEditor()->promptEdit()->toPlainText().contains(QStringLiteral("LEFT JOIN")));
    window.questionEditor()->promptEdit()->setPlainText(QStringLiteral("What does a LEFT OUTER JOIN keep?"));
    window.questionEditor()->saveButton()->click();
    const auto edited = db->question(id);
    QVERIFY(edited);
    QCOMPARE(edited->prompt, QStringLiteral("What does a LEFT OUTER JOIN keep?"));
    QCOMPARE(edited->options.size(), 2);
    QCOMPARE(db->documentQuestions(docs.first().id).size(), 3);   // edited in place, not duplicated
    QSqlQuery q(db->connection());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM attempts WHERE question_id = '%1'").arg(id)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    q.finish();

    // Deleting a question removes it and its history.
    QVERIFY(window.removeOwnQuestion(id, true));
    QVERIFY(!db->question(id));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM attempts WHERE question_id = '%1'").arg(id)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    q.finish();
    QCOMPARE(db->documents(set).first().questionCount, 2);

    // Your questions go away with their set.
    QVERIFY(window.removeSet(set, true));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM questions WHERE id LIKE 'm-%'")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TestStudyUi::petStudyFlowWorksWithoutOllama()
{
    {
        AppSettings settings;
        settings.setOnboardingCompleted(true);
        settings.setStudyFormats(SettingsDefaults::kStudyFormatSingleChoice | SettingsDefaults::kStudyFormatWritten);
        settings.setStudyFormatMode(SettingsDefaults::kStudyFormatModeFocus);
        settings.setStudyFocusFormat(SettingsDefaults::kStudyFormatSingleChoice);
        settings.save();
    }
    PetWindow pet;
    pet.showPet();
    QApplication::processEvents();
    QVERIFY(!pet.studyLibrary());   // nothing study-related is created until it is used

    // Ask with an empty library: a notice offers the Study Library.
    pet.askQuestionNow();
    QVERIFY(pet.studyLibrary());
    disconnectOllama(pet.ollamaClient());
    QuizPopup* popup = pet.quizPopup();
    QCOMPARE(popup->phase(), QuizPopup::Phase::Notice);
    QSignalSpy closed(popup, &QuizPopup::closed);
    popup->submitButton()->click();
    QVERIFY(pet.studyLibraryWindow());
    QVERIFY(pet.studyLibraryWindow()->isVisible());
    QVERIFY(!pet.canShowCheckIn());   // the library counts as an open dialog
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 1000);

    // Set up a study set with cached questions directly (generation itself is covered elsewhere).
    const qint64 studySet = pet.studyLibraryWindow()->createSet(QStringLiteral("Databases"));
    StudyDatabase* db = pet.studyLibrary()->database();
    const qint64 doc = *db->addDocument(studySet, QStringLiteral("Lecture 6"), QStringLiteral("l6.pptx"), DocumentKind::Pptx);
    Question sc = choiceQuestion();
    sc.setId = studySet;
    sc.documentId = doc;
    sc.difficulty = Difficulty::Medium;
    Question w = writtenQuestion();
    w.setId = studySet;
    w.documentId = doc;
    QVERIFY(db->saveQuestion(sc, 0, QString()));
    QVERIFY(db->saveQuestion(w, 0, QString()));
    pet.studyLibraryWindow()->hide();
    QCOMPARE(pet.appSettings()->studySetId(), studySet);

    // Focus mode: only multiple choice questions are asked.
    pet.askQuestionNow();
    QCOMPARE(popup->phase(), QuizPopup::Phase::Asking);
    QCOMPARE(popup->question().type, QuestionType::SingleChoice);
    QCOMPARE(popup->question().sources.first().title, QStringLiteral("Lecture 6"));
    PixelOptionButton* right = nullptr;
    for (PixelOptionButton* o : popup->optionButtons()) {
        if (o->property("optionId").toString() == QStringLiteral("b")) {
            right = o;
        }
    }
    QVERIFY(right);   // found by ID wherever shuffling placed it
    right->click();
    popup->submitButton()->click();
    QVERIFY(popup->isShowingResult());
    QVERIFY(popup->resultLabel()->text().startsWith(QStringLiteral("Correct · +20 XP")));
    QCOMPARE(pet.progressTracker()->progress(studySet).xp, 20);
    popup->closeButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(pet.quizController()->state(), QuizController::State::Idle, 1000);

    // Written answers still work with Ollama unavailable: exact matches are graded locally,
    // anything needing the model becomes "Needs review" instead of failing.
    pet.appSettings()->setStudyFocusFormat(SettingsDefaults::kStudyFormatWritten);
    pet.applySettings();
    pet.askQuestionNow();
    QCOMPARE(popup->question().type, QuestionType::Written);
    popup->writtenEdit()->setPlainText(QStringLiteral("I think it is Macbeth or maybe Othello"));
    popup->submitButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(popup->isShowingResult(), 5000);
    QCOMPARE(popup->resultLabel()->text(), QStringLiteral("Needs review"));
    QVERIFY(popup->expectedLabel()->text().contains(QStringLiteral("Hamlet")));
    popup->closeButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(pet.quizController()->state(), QuizController::State::Idle, 1000);

    // Reveal + self-assessment is recorded separately with reduced XP.
    QTest::qWait(250);   // let the closing fade finish
    pet.askQuestionNow();
    QCOMPARE(popup->question().type, QuestionType::Written);
    popup->revealButton()->click();
    QCOMPARE(popup->phase(), QuizPopup::Phase::Revealed);
    popup->selfAssessButton(SelfRating::GotIt)->click();
    QVERIFY(popup->isShowingResult());
    const StudyProgress progress = pet.progressTracker()->progress(studySet);
    QCOMPARE(progress.selfAssessed, 1);
    QCOMPARE(progress.selfGotIt, 1);
    QCOMPARE(progress.selfXp, 5);   // 25% of a medium question's 20 XP
    QCOMPARE(progress.attempted, 2);
    QCOMPARE(progress.needsReview, 1);
}

QTEST_MAIN(TestStudyUi)
#include "test_study_ui.moc"
