#include "MockLlmClient.h"
#include "StudyTestUtils.h"

#include "ai/OllamaClient.h"
#include "app/AppSettings.h"
#include "app/SettingsDefaults.h"
#include "pet/PetWindow.h"
#include "quickactions/AnimatedQuickActionButton.h"
#include "quickactions/QuickActionMenu.h"
#include "alarm/AlarmScheduler.h"
#include "alarm/AlarmSoundManager.h"
#include "study/ingest/OcrEngine.h"
#include "study/progress/ProgressTracker.h"
#include "study/generation/QuestionGenerator.h"
#include "study/generation/QuestionValidator.h"
#include "study/StudyDatabase.h"
#include "study/StudyLibrary.h"
#include "study/practice/QuizController.h"
#include "study/practice/QuizController.h"
#include "studyui/QuizPopup.h"
#include "ui/PixelTheme.h"
#include "ui/CollapsibleSection.h"
#include "studyui/QuestionEditor.h"
#include "studyui/SetQuestionsView.h"
#include "ui/PixelSegmentedControl.h"
#include "studyui/StudyLibraryWindow.h"
#include "studyui/StudyPracticeView.h"
#include "studyui/StudyPreferencesPage.h"
#include "studyui/StudySetList.h"
#include "ui/SettingsPanel.h"
#include "studyui/SupportView.h"
#include "timer/StudyTimer.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
    void libraryHidesTheReadyBanner();
    void libraryExplainsWhichFilesCanBeImported();
    void preferencesTabControlsCheckInsFormatsAndProgress();
    void practiceRunsASetFromItsPage();
    void studyTypographyMatchesTheSharedTheme();
    void practiceSkipEndAndFormatRules();
    void practicingEarlyKeepsTheReviewSchedule();
    void questionEditorValidatesEveryFormat();
    void ownQuestionsCanBeWrittenEditedPracticedAndDeleted();
    void generatedQuestionsCanBeEditedRewrittenAndDeleted();
    void librarySetsShowStatusAndQuestionsHaveTheirOwnTab();
    void questionsNeedingContextAreFlaggedAndFixedTogether();
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
    popup.showEvaluation(e);
    QVERIFY(popup.isShowingResult());
    QVERIFY(popup.resultLabel()->text().startsWith(QStringLiteral("Not quite")));
    // The verdict is a tinted card with an icon, in the largest type of the question, after the answers.
    QVERIFY(popup.resultCard()->isVisibleTo(&popup));
    QCOMPARE(popup.resultCard()->property("verdict").toString(), QStringLiteral("incorrect"));
    QCOMPARE(popup.verdictIcon()->property("pixelIcon").toInt(), static_cast<int>(PixelIcon::Close));
    QVERIFY(!popup.verdictIcon()->pixmap().isNull());
    popup.resultLabel()->ensurePolished();
    QCOMPARE(popup.resultLabel()->font().pixelSize(), PixelTheme::kVerdictPx);
    QVERIFY(popup.resultLabel()->font().pixelSize() > popup.optionButtons().first()->font().pixelSize());
    QApplication::processEvents();
    QVERIFY(popup.resultCard()->mapTo(&popup, QPoint()).y() > options.at(3)->mapTo(&popup, QPoint()).y());
    QVERIFY(popup.resultCard()->isAncestorOf(popup.feedbackLabel()));
    QVERIFY(popup.submitButton()->isVisibleTo(&popup));   // the next step is always in reach, outside the scroll area
    QVERIFY(!popup.scrollArea()->isAncestorOf(popup.submitButton()));
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
    popup.showEvaluation(e);
    QCOMPARE(popup.resultLabel()->text(), QStringLiteral("Partially correct"));
    QCOMPARE(popup.resultCard()->property("verdict").toString(), QStringLiteral("partial"));
    QCOMPARE(popup.verdictIcon()->property("pixelIcon").toInt(), static_cast<int>(PixelIcon::Partial));
    QVERIFY(popup.verdictDetail()->isVisibleTo(&popup));
    QCOMPARE(popup.verdictDetail()->text(), QStringLiteral("1 of 2 correct answers picked"));
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
    QVERIFY(popup.resultCard()->isAncestorOf(popup.expectedLabel()));
    QVERIFY(popup.resultCard()->property("verdict").toString().isEmpty());   // neutral until rated
    QVERIFY(popup.sourceLabel()->text().contains(QStringLiteral("English 210 · p. 14")));
    QVERIFY(popup.selfAssessButton(SelfRating::GotIt)->isVisible());
    QVERIFY(!popup.submitButton()->isVisible());
    QCOMPARE(popup.remainingDismissMs(), 0);   // waits for the student's rating
    popup.selfAssessButton(SelfRating::PartlyGotIt)->click();
    QCOMPARE(rated.count(), 1);
    QCOMPARE(rated.first().first().toInt(), static_cast<int>(SelfRating::PartlyGotIt));

    popup.showEvaluation(StudyEval::selfAssessment(SelfRating::PartlyGotIt));
    QCOMPARE(popup.resultLabel()->text(), QStringLiteral("Partially got it"));
    QVERIFY(popup.resultCard()->isVisibleTo(&popup));

    // A new question starts without any result.
    popup.showQuestion(writtenQuestion(), kPet);
    QVERIFY(!popup.resultCard()->isVisibleTo(&popup));
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

    // Study sets
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
    QCOMPARE(window.documentStatus(doc)->text(), QStringLiteral("Text ready"));   // status as a pill
    QVERIFY(!window.documentMeta(doc)->text().contains(QStringLiteral("Text ready")));
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

void TestStudyUi::libraryHidesTheReadyBanner()
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
    // Question formats are covered in preferencesTabControlsCheckInsFormatsAndProgress (they live there now).
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

    // One Add files button; the types it takes are in its tooltip, not in paragraphs on the page.
    const QString types = window.importButton()->toolTip();
    QVERIFY(types.contains(QStringLiteral(".pptx")));
    QVERIFY(types.contains(QStringLiteral(".txt")));
    QVERIFY(types.contains(QStringLiteral(".md")));
    QVERIFY(types.contains(QStringLiteral("PDF")));
    QVERIFY(types.contains(QStringLiteral("drop files")));
    QVERIFY(types.contains(StudyLibraryWindow::importTypesText()));
    QCOMPARE(window.importButton()->text(), QStringLiteral("Add files"));
    QVERIFY(window.importButton()->isEnabled());

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
    QVERIFY(!prefs->formatsSection()->summaryLabel()->text().contains(QStringLiteral("Written")));
    QVERIFY(prefs->formatCheck(SettingsDefaults::kStudyFormatSingleChoice)->isChecked());
    QVERIFY(!prefs->formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->isChecked());
    QVERIFY(!prefs->formatCheck(SettingsDefaults::kStudyFormatWritten)->isChecked());
    QVERIFY(!prefs->formatsExpanded());
    prefs->formatsSection()->setExpanded(true, false);
    QVERIFY(prefs->formatsExpanded());
    prefs->formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    QVERIFY(prefs->formatsSection()->summaryLabel()->text().contains(QStringLiteral("Written answer")));
    prefs->formatsSection()->setExpanded(false, false);
    QVERIFY(!prefs->formatsExpanded());
    QVERIFY(window.practiceButton()->toolTip().contains(QStringLiteral("written answer")));
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
    // The recommended model and the optional larger one are always on offer, with their size.
    QVERIFY(prefs->modelCombo()->findData(QString::fromLatin1(OllamaClient::kDefaultModel)) >= 0);
    const int larger = prefs->modelCombo()->findData(QString::fromLatin1(OllamaClient::kHigherQualityModel));
    QVERIFY(larger >= 0);
    QVERIFY(prefs->modelCombo()->itemText(larger).contains(QStringLiteral("higher quality")));
    QVERIFY(prefs->modelCombo()->itemText(larger).contains(QStringLiteral("6.6 GB")));

    // Scanned pages and figures are read automatically: Preferences has no technical section for them.
    for (CollapsibleSection* section : prefs->findChildren<CollapsibleSection*>()) {
        QVERIFY2(!section->titleLabel()->text().contains(QStringLiteral("Scans"), Qt::CaseInsensitive),
                 qPrintable(section->titleLabel()->text()));
    }
    for (QLabel* label : prefs->findChildren<QLabel*>()) {
        QVERIFY2(!label->text().contains(OcrEngine::statusText()), qPrintable(label->text()));
        QVERIFY(!label->text().contains(QStringLiteral("vision model"), Qt::CaseInsensitive));
    }

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
    QVERIFY(view->summaryLabel()->text().contains(QStringLiteral("100% right")));
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
    // The second pass was early (cards were scheduled for tomorrow): attempts are kept,
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

    // Every other Study surface uses the same classes: set rows, the question editor, the
    // practice page and the figure/table block under a question.
    auto* refError = new QLabel(QStringLiteral("x"), &host);
    refError->setObjectName(QStringLiteral("errorNote"));
    const auto checkTree = [&](QWidget* root, const char* what) {
        for (QLabel* label : root->findChildren<QLabel*>()) {
            if (label->objectName() == QLatin1String("muted")) {
                check(label, refMuted, what);
            } else if (label->objectName() == QLatin1String("sectionTitle")) {
                check(label, refTitle, what);
            } else if (label->objectName() == QLatin1String("errorNote")) {
                check(label, refError, what);
            }
        }
        for (QComboBox* combo : root->findChildren<QComboBox*>()) {
            check(combo, refCombo, what);
        }
        for (PixelCheckBox* box : root->findChildren<PixelCheckBox*>()) {
            check(box, refCheck, what);
        }
    };
    window.showTab(0);
    window.showSetList();
    QApplication::processEvents();
    checkTree(window.setList(), "set row");
    window.openSet(set);
    QVERIFY(window.writeQuestionButton());
    window.writeQuestionButton()->click();
    QApplication::processEvents();
    checkTree(window.questionEditor(), "question editor");
    check(window.questionEditor()->supportCombo(), refCombo, "editor figure combo");
    check(window.questionEditor()->errorLabel(), refMuted, "editor error size");
    window.questionEditor()->cancelButton()->click();
    QVERIFY(window.startPractice());
    QApplication::processEvents();
    checkTree(window.practiceView(), "practice view");

    Question withTable = choiceQuestion();
    SupportRef table;
    table.kind = SupportKind::Table;
    table.caption = QStringLiteral("Table 1: Access times");
    table.table = {{QStringLiteral("Level"), QStringLiteral("Time")}, {{QStringLiteral("L1"), QStringLiteral("1 ns")}}};
    withTable.support = {table};
    QuizPopup popup;
    popup.showQuestion(withTable, kPet);
    QApplication::processEvents();
    QVERIFY(!popup.supportView()->captions().isEmpty());
    for (QLabel* caption : popup.supportView()->captions()) {
        check(caption, refMuted, "figure caption");
    }
    for (QLabel* cells : popup.supportView()->tables()) {
        check(cells, refLabel, "table");
    }
    popup.close();

    // The Settings Alarm Time dropdowns are the reference every dropdown matches (family, size, weight).
    StudyTimer timer;
    AlarmScheduler scheduler(&settings);
    AlarmSoundManager sound;
    SettingsPanel panel(&settings, &timer, &scheduler, &sound);
    QComboBox* alarmTime = nullptr;
    QComboBox* otherSettingsCombo = nullptr;
    for (QComboBox* combo : panel.findChildren<QComboBox*>()) {
        (combo->objectName() == QLatin1String("timeSelect") ? alarmTime : otherSettingsCombo) = combo;
    }
    QVERIFY(alarmTime);
    QVERIFY(otherSettingsCombo);
    alarmTime->ensurePolished();
    QVERIFY(same(alarmTime->font(), PixelTheme::controlFont()));
    QCOMPARE(alarmTime->font().pixelSize(), PixelTheme::kControlPx);
    QCOMPARE(alarmTime->font().weight(), PixelTheme::kControlWeight);
    check(refCombo, alarmTime, "shared combo rule vs Alarm Time");
    check(otherSettingsCombo, alarmTime, "Settings combo vs Alarm Time");
    for (QComboBox* combo : {prefs->frequencyCombo(), prefs->sourceCombo(), prefs->focusCombo(), prefs->modelCombo()}) {
        check(combo, alarmTime, "Study combo vs Alarm Time");
        QCOMPARE(combo->palette().color(QPalette::ButtonText), alarmTime->palette().color(QPalette::ButtonText));
    }

    // The window's page tabs (Library, Preferences) are larger than the small chips, from the theme.
    QPushButton* libraryTab = window.tabs()->button(0);
    QCOMPARE(libraryTab->objectName(), QStringLiteral("navTab"));
    libraryTab->ensurePolished();
    QCOMPARE(libraryTab->font().pixelSize(), PixelTheme::kEmphasisPx);
    QVERIFY(libraryTab->font().pixelSize() > refButton->font().pixelSize());
    window.practiceView()->endButton()->click();
    QVERIFY(window.removeSet(set, true));

    // No Study source file declares its own font: sizes and faces come from PixelTheme.
    const QDir ui(QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/../src/studyui"));
    const QStringList files = ui.entryList({QStringLiteral("Study*.cpp"), QStringLiteral("QuizPopup.cpp"),
                                            QStringLiteral("QuestionEditor.cpp"), QStringLiteral("SupportView.cpp")}, QDir::Files);
    QVERIFY(files.size() >= 7);
    const QRegularExpression literal(QStringLiteral(R"(font-size\s*:|font-family\s*:|font-weight\s*:|setPixelSize\(|setPointSize\(|QFont\s+\w+\()"));
    for (const QString& name : files) {
        QFile source(ui.filePath(name));
        QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Text));
        int lineNumber = 0;
        while (!source.atEnd()) {
            const QString line = QString::fromUtf8(source.readLine());
            ++lineNumber;
            QVERIFY2(!literal.match(line).hasMatch(),
                     qPrintable(QStringLiteral("%1:%2 declares its own font: %3").arg(name).arg(lineNumber).arg(line.trimmed())));
        }
    }
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
    QVERIFY(tracker.record(original, correct));
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

void TestStudyUi::generatedQuestionsCanBeEditedRewrittenAndDeleted()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    StudyDatabase* db = library.database();

    // A generated question from slide 2, shown with the slide's table.
    const QString passage = QStringLiteral(
        "A LEFT JOIN returns every row from the left table and the matching rows from the right table. "
        "Rows without a match get NULL values for the right table's columns.");
    const qint64 set = db->addStudySet(QStringLiteral("Joins")).value_or(0);
    const qint64 doc = db->addDocument(set, QStringLiteral("Lecture 4"), QStringLiteral("l4.pptx"), DocumentKind::Pptx).value_or(0);
    QVERIFY(db->replaceChunks(doc, {{0, 0, 0, SegmentKind::Slide, 2, 0, passage}}));
    db->setDocumentStatus(doc, DocumentStatus::Ready);
    const qint64 chunkId = db->chunks(doc).first().id;
    SupportAsset table;
    table.documentId = doc;
    table.unitKind = SegmentKind::Slide;
    table.unitNumber = 2;
    table.kind = SupportKind::Table;
    table.table.headers = {QStringLiteral("Join"), QStringLiteral("Keeps")};
    table.table.rows = {{QStringLiteral("LEFT"), QStringLiteral("all left rows")}};
    const qint64 assetId = db->addSupportAsset(table).value_or(0);
    Question generated = choiceQuestion();
    generated.id = QStringLiteral("gen-1");
    generated.setId = set;
    generated.documentId = doc;
    generated.prompt = QStringLiteral("According to the table, what does a LEFT JOIN keep?");
    generated.sources.first().excerpt = QStringLiteral("A LEFT JOIN returns every row from the left table");
    generated.sources.first().location = QStringLiteral("slide 2");
    SupportRef ref;
    ref.kind = SupportKind::Table;
    ref.assetId = assetId;
    generated.support.append(ref);
    QVERIFY(db->saveQuestion(generated, chunkId, QStringLiteral("model")));
    Evaluation right;
    right.verdict = Verdict::Correct;
    right.score = 1.0;
    QVERIFY(tracker.record(*db->question(generated.id), right));

    MockLlmClient llm;
    int rewrites = 0;
    llm.responder = [&rewrites](const LlmRequest&) -> MockLlmClient::Reply {
        ++rewrites;
        QJsonArray options;
        const QStringList texts = {QStringLiteral("Every left row"), QStringLiteral("Only matching rows"),
                                   QStringLiteral("Only right rows"), QStringLiteral("No rows")};
        for (int i = 0; i < 4; ++i) {
            options.append(QJsonObject{{QStringLiteral("text"), texts.at(i)}, {QStringLiteral("correct"), i == 0},
                                       {QStringLiteral("why"), QStringLiteral("See your notes.")}});
        }
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("concept"), QStringLiteral("LEFT JOIN rows %1").arg(rewrites)},
            {QStringLiteral("visual"), 0},
            {QStringLiteral("evidence"), QStringLiteral("A LEFT JOIN returns every row from the left table")},
            {QStringLiteral("question"), QStringLiteral("Which rows does a LEFT JOIN always return (version %1)?").arg(rewrites)},
            {QStringLiteral("options"), options},
            {QStringLiteral("explanation"), QStringLiteral("Unmatched left rows are kept with NULLs.")},
            {QStringLiteral("difficulty"), QStringLiteral("easy")},
        }).toJson(QJsonDocument::Compact));
    };
    QuestionGenerator generator(db, &llm);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);
    window.openSet(set);

    // Edit: the editor shows the source and offers the slide's table, already chosen.
    QVERIFY(window.editOwnQuestion(generated.id));
    QuestionEditor* editor = window.questionEditor();
    QVERIFY(editor->sourceLabel()->isVisibleTo(editor));
    QVERIFY(editor->sourceLabel()->text().contains(QStringLiteral("Lecture 4 · slide 2")));
    QVERIFY(editor->sourceLabel()->text().contains(QStringLiteral("returns every row")));
    QCOMPARE(editor->supportCombo()->count(), 2);
    QCOMPARE(editor->supportCombo()->currentData().toLongLong(), assetId);

    // Removing the table while the question still says "the table" is refused.
    editor->supportCombo()->setCurrentIndex(0);
    editor->saveButton()->click();
    QVERIFY(editor->errorLabel()->isVisibleTo(editor));
    QCOMPARE(db->question(generated.id)->prompt, generated.prompt);

    // Keep the table and reword the question: same ID, source, passage and history; marked edited.
    editor->supportCombo()->setCurrentIndex(1);
    editor->promptEdit()->setPlainText(QStringLiteral("Using the table, which rows does a LEFT JOIN keep?"));
    editor->saveButton()->click();
    const auto edited = db->question(generated.id);
    QVERIFY(edited);
    QCOMPARE(edited->prompt, QStringLiteral("Using the table, which rows does a LEFT JOIN keep?"));
    QCOMPARE(edited->origin, QuestionOrigin::Edited);
    QVERIFY(edited->editedAt > 0);
    QCOMPARE(edited->chunkId, chunkId);
    QCOMPARE(edited->sources.first().excerpt, generated.sources.first().excerpt);
    QCOMPARE(edited->support.size(), 1);
    QCOMPARE(edited->options.at(1).explanation, generated.options.at(1).explanation);   // unchanged answers keep theirs
    QCOMPARE(tracker.progress(set).answered(), 1);
    QStringList rowTexts;
    for (QLabel* l : window.findChildren<QLabel*>()) {
        if (l->property("questionId").toString() == generated.id) {
            rowTexts.append(l->text());
        }
    }
    QCOMPARE(rowTexts.size(), 1);
    QVERIFY(rowTexts.first().contains(QStringLiteral("table")));
    QVERIFY(rowTexts.first().contains(QStringLiteral("Edited by you")));

    // Regenerating the whole document keeps the edited question.
    QVERIFY(db->removeQuestions(doc));
    QVERIFY(db->question(generated.id));

    // Rewrite: a new version is saved, then the student chooses. First "Keep both" ...
    QVERIFY(window.regenerateQuestion(generated.id));
    QTRY_VERIFY_WITH_TIMEOUT(window.regenerationPrompt()->isVisibleTo(&window), 5000);
    QVERIFY(window.findChild<QLabel*>() != nullptr);
    const QString firstNew = window.pendingRegeneration().second;
    QCOMPARE(window.pendingRegeneration().first, generated.id);
    QVERIFY(db->question(firstNew));
    QCOMPARE(db->question(firstNew)->chunkId, chunkId);
    QCOMPARE(db->question(firstNew)->type, generated.type);
    QVERIFY(window.resolveRegeneration(generated.id, firstNew, 2));
    QVERIFY(!window.regenerationPrompt()->isVisibleTo(&window));
    QVERIFY(db->question(generated.id));
    QVERIFY(db->question(firstNew));

    // ... then "Replace": the old question and its history go, the new one stays.
    QVERIFY(window.regenerateQuestion(generated.id));
    QTRY_VERIFY_WITH_TIMEOUT(window.regenerationPrompt()->isVisibleTo(&window), 5000);
    const QString secondNew = window.pendingRegeneration().second;
    QVERIFY(secondNew != firstNew);
    QVERIFY(window.resolveRegeneration(generated.id, secondNew, 1));
    QVERIFY(!db->question(generated.id));
    QVERIFY(db->question(secondNew));
    QCOMPARE(tracker.progress(set).answered(), 0);

    // Your own questions cannot be "rewritten" (there is no passage behind them).
    QVERIFY(!generator.regenerateQuestion(QStringLiteral("no-such-question")));

    // Delete a generated question.
    QVERIFY(window.removeOwnQuestion(firstNew, true));
    QVERIFY(!db->question(firstNew));
    QVERIFY(window.removeSet(set, true));
}

void TestStudyUi::librarySetsShowStatusAndQuestionsHaveTheirOwnTab()
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
    StudyDatabase* db = library.database();

    const qint64 ready = seedSet(library, QStringLiteral("Ready set"), 3);
    const qint64 empty = db->addStudySet(QStringLiteral("Empty set")).value_or(0);
    const qint64 broken = db->addStudySet(QStringLiteral("Broken set")).value_or(0);
    const qint64 badDoc = db->addDocument(broken, QStringLiteral("Scan"), QStringLiteral("scan.pdf"), DocumentKind::Pdf).value_or(0);
    db->setDocumentStatus(badDoc, DocumentStatus::Failed);
    window.showNear(kPet);

    // A row is the name, two counts and a status pill; nothing else to read.
    QCOMPARE(window.setList()->statusPill(ready)->text(), QStringLiteral("Ready"));
    QCOMPARE(window.setList()->statusPill(ready)->property("tone").toString(), QStringLiteral("ready"));
    QCOMPARE(window.setList()->statusPill(empty)->text(), QStringLiteral("No questions yet"));
    QCOMPARE(window.setList()->statusPill(broken)->text(), QStringLiteral("Needs attention"));
    QVERIFY(window.setList()->metaText(ready).contains(QStringLiteral("3 questions")));
    for (QPushButton* b : window.setList()->findChildren<QPushButton*>()) {
        QVERIFY2(b->text().isEmpty(), qPrintable(b->text()));   // no Rename/Delete chips on rows any more
    }

    // Rename and Delete live in the row's menu.
    QVERIFY(window.setList()->menuButton(empty));
    QMenu* menu = window.setList()->menuFor(empty);
    QVERIFY(menu);
    QStringList actions;
    for (QAction* a : menu->actions()) {
        if (!a->isSeparator()) {
            actions.append(a->text());
        }
    }
    QCOMPARE(actions, (QStringList{QStringLiteral("Rename"), QStringLiteral("Delete")}));
    QSignalSpy renameAsked(window.setList(), &StudySetList::renameRequested);
    menu->actions().first()->trigger();   // Delete asks in a dialog; Rename edits inline
    QCOMPARE(renameAsked.count(), 1);
    QCOMPARE(renameAsked.first().first().toLongLong(), empty);
    menu->deleteLater();

    // Opening a set: actions on top, then Materials | Questions with counts.
    window.openSet(ready);
    QApplication::processEvents();
    QVERIFY(window.isShowingSet());
    QCOMPARE(window.contentTabs()->count(), 2);
    QCOMPARE(window.contentTabs()->button(0)->text(), QStringLiteral("Materials (1)"));
    QCOMPARE(window.contentTabs()->button(1)->text(), QStringLiteral("Questions (3)"));
    QCOMPARE(window.contentTabs()->currentIndex(), 0);
    QVERIFY(window.importButton()->isVisibleTo(&window));
    QVERIFY(!window.writeQuestionButton()->isVisibleTo(&window));
    QVERIFY(window.practiceButton()->mapTo(&window, QPoint()).y() < window.contentTabs()->mapTo(&window, QPoint()).y());
    window.contentTabs()->button(1)->click();
    QVERIFY(window.questionsView()->isVisibleTo(&window));
    QVERIFY(window.writeQuestionButton()->isVisibleTo(&window));
    QCOMPARE(window.questionsView()->shownIds().size(), 3);
    QVERIFY(!window.questionsView()->fixAllButton()->isVisibleTo(&window));   // nothing to fix

    // A question's Edit, Rewrite and Delete are in its menu.
    const QString first = window.questionsView()->shownIds().first();
    QMenu* questionMenu = window.questionsView()->menuFor(first);
    QVERIFY(questionMenu);
    QStringList questionActions;
    for (QAction* a : questionMenu->actions()) {
        if (!a->isSeparator()) {
            questionActions.append(a->text());
        }
    }
    QCOMPARE(questionActions, (QStringList{QStringLiteral("Edit"), QStringLiteral("Rewrite"), QStringLiteral("Delete")}));
    QSignalSpy edit(window.questionsView(), &SetQuestionsView::editRequested);
    questionMenu->actions().first()->trigger();
    QCOMPARE(edit.count(), 1);
    QVERIFY(window.questionEditor()->isVisibleTo(&window));   // the editor opened for that question
    window.questionEditor()->cancelButton()->click();
    questionMenu->deleteLater();

    // The set's own menu holds Regenerate, Rename and Delete.
    QMenu* setMenu = window.setMenu();
    QVERIFY(setMenu);
    QStringList setActions;
    for (QAction* a : setMenu->actions()) {
        if (!a->isSeparator()) {
            setActions.append(a->text());
        }
    }
    QCOMPARE(setActions.size(), 3);
    QVERIFY(setActions.first().startsWith(QStringLiteral("Regenerate")));
    QVERIFY(!setMenu->actions().first()->isEnabled());   // no model here, nothing to regenerate with
    setMenu->deleteLater();

    for (const qint64 id : {ready, empty, broken}) {
        QVERIFY(window.removeSet(id, true));
    }
}

void TestStudyUi::questionsNeedingContextAreFlaggedAndFixedTogether()
{
    clearSettings();
    AppSettings settings;
    StudyLibrary library;
    QVERIFY(library.isOpen());
    OllamaClient ollama;
    disconnectOllama(&ollama);
    ProgressTracker tracker(library.database());
    StudyDatabase* db = library.database();

    const QString passage = QStringLiteral(
        "During the benchmark, 45 of 60 requests were cache hits and the rest went to main memory. "
        "The hit rate is the share of requests served by the cache, so a higher hit rate means fewer slow trips.");
    const qint64 set = db->addStudySet(QStringLiteral("Caches")).value_or(0);
    const qint64 doc = db->addDocument(set, QStringLiteral("Lecture 9"), QStringLiteral("l9.pptx"), DocumentKind::Pptx).value_or(0);
    QVERIFY(db->replaceChunks(doc, {{0, 0, 0, SegmentKind::Slide, 4, 0, passage}}));
    db->setDocumentStatus(doc, DocumentStatus::Ready);
    const qint64 chunkId = db->chunks(doc).first().id;
    // Two questions generated before the context rules: both lean on things the student cannot see.
    for (const QString& prompt : {QStringLiteral("According to your notes, what is the correct percentage based on the observed data?"),
                                  QStringLiteral("As mentioned earlier, what share of requests hit the cache?")}) {
        Question q = choiceQuestion();
        q.id = QStringLiteral("old-%1").arg(prompt.size());
        q.setId = set;
        q.documentId = doc;
        q.prompt = prompt;
        q.topic = prompt.left(20);
        q.options.first().text = QStringLiteral("75%");
        q.sources.first().excerpt = QStringLiteral("45 of 60 requests were cache hits");
        QVERIFY(db->saveQuestion(q, chunkId, QStringLiteral("model")));
    }
    const qint64 other = seedSet(library, QStringLiteral("Other"), 1);   // unrelated set, untouched

    MockLlmClient llm;
    int calls = 0;
    llm.responder = [&calls](const LlmRequest&) -> MockLlmClient::Reply {
        ++calls;
        QJsonArray options;
        const QStringList texts = {QStringLiteral("75%"), QStringLiteral("45%"), QStringLiteral("60%"), QStringLiteral("25%")};
        for (int i = 0; i < 4; ++i) {
            options.append(QJsonObject{{QStringLiteral("text"), texts.at(i)}, {QStringLiteral("correct"), i == 0},
                                       {QStringLiteral("why"), QStringLiteral("45 / 60 = 0.75.")}});
        }
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("concept"), QStringLiteral("Hit rate %1").arg(calls)},
            {QStringLiteral("evidence"), QStringLiteral("45 of 60 requests were cache hits")},
            {QStringLiteral("data"), QStringLiteral("45 of 60 requests were cache hits")},
            {QStringLiteral("question"), QStringLiteral("What was the cache's hit rate (version %1)?").arg(calls)},
            {QStringLiteral("options"), options},
            {QStringLiteral("explanation"), QStringLiteral("45 hits out of 60 requests is 75%.")},
            {QStringLiteral("difficulty"), QStringLiteral("easy")},
        }).toJson(QJsonDocument::Compact));
    };
    QuestionGenerator generator(db, &llm);
    StudyLibraryWindow window(&settings, &library, &generator, &ollama, &tracker);
    window.showNear(kPet);

    // The set says it needs attention, and practice leaves the two questions out.
    QCOMPARE(window.setList()->statusPill(set)->text(), QStringLiteral("Needs attention"));
    window.openSet(set);
    QVERIFY(!window.practiceButton()->isEnabled());
    SetQuestionsView* view = window.questionsView();
    QCOMPARE(view->count(), 2);
    QCOMPARE(view->needsContextCount(), 2);
    window.contentTabs()->button(1)->click();
    QVERIFY(view->fixAllButton()->isVisibleTo(&window));
    QCOMPARE(view->filterTabs()->button(1)->text(), QStringLiteral("Needs context (2)"));
    view->setFilter(SetQuestionsView::Filter::NeedsContext);
    QCOMPARE(view->shownIds().size(), 2);
    for (QLabel* label : view->findChildren<QLabel*>()) {
        if (!label->property("questionId").toString().isEmpty()) {
            QVERIFY(label->text().startsWith(QStringLiteral("[Needs context")));
        }
    }

    // Fix all: asked once, then every flagged question is rewritten and replaced.
    QVERIFY(window.fixAllQuestions(true));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFixingQuestions(), 10000);
    QCOMPARE(calls, 2);
    const QVector<Question> now = db->documentQuestions(doc);
    QCOMPARE(now.size(), 2);
    for (const Question& q : now) {
        QVERIFY2(!QuestionValidator::needsContext(q), qPrintable(q.prompt));
        QCOMPARE(q.support.size(), 1);   // the numbers it needs are shown with it
        QCOMPARE(q.support.first().kind, SupportKind::Data);
    }
    QCOMPARE(view->needsContextCount(), 0);
    QVERIFY(!view->fixAllButton()->isVisibleTo(&window));
    QVERIFY(window.statusLabel()->text().contains(QStringLiteral("Rewrote 2 questions")));
    QVERIFY(window.practiceButton()->isEnabled());
    QCOMPARE(window.setList()->statusPill(set)->text(), QStringLiteral("Ready"));
    QCOMPARE(db->setQuestions(other, {}, false).size(), 1);
    QVERIFY(window.removeSet(set, true));
    QVERIFY(window.removeSet(other, true));
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
    QCOMPARE(popup->resultLabel()->text(), QStringLiteral("Correct"));
    QCOMPARE(pet.progressTracker()->progress(studySet).correct, 1);
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

    // Reveal + self-assessment is recorded separately from automatic grading.
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
    QCOMPARE(progress.answered(), 3);
    QCOMPARE(progress.attempted, 2);
    QCOMPARE(progress.needsReview, 1);
}

QTEST_MAIN(TestStudyUi)
#include "test_study_ui.moc"
