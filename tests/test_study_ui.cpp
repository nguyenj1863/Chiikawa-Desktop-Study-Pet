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
#include "ui/QuizPopup.h"
#include "ui/PixelTheme.h"
#include "ui/PixelSegmentedControl.h"
#include "ui/StudyLibraryWindow.h"
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
    QVERIFY(window.formatsToggle()->text().contains(QStringLiteral("Multiple choice")));
    QVERIFY(!window.formatsToggle()->text().contains(QStringLiteral("Written")));
    QVERIFY(window.formatCheck(SettingsDefaults::kStudyFormatSingleChoice)->isChecked());
    QVERIFY(!window.formatCheck(SettingsDefaults::kStudyFormatMultiSelect)->isChecked());
    QVERIFY(!window.formatCheck(SettingsDefaults::kStudyFormatWritten)->isChecked());
    window.formatsToggle()->click();
    QVERIFY(window.formatsExpanded());
    window.formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    QVERIFY(window.formatsToggle()->text().contains(QStringLiteral("Written answer")));
    window.formatsToggle()->click();
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
    prefs->formatsToggle()->click();
    QVERIFY(prefs->formatsExpanded());
    prefs->formatCheck(SettingsDefaults::kStudyFormatWritten)->setChecked(true);
    QVERIFY(prefs->formatsToggle()->text().contains(QStringLiteral("Written answer")));
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
