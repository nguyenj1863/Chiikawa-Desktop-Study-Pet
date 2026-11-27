#include "ui/QuestionEditor.h"

#include "study/QuestionValidator.h"
#include "ui/PixelSegmentedControl.h"
#include "ui/PixelTheme.h"
#include "ui/StudyUiHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

using namespace StudyUi;

namespace {

constexpr int kMaxPromptChars = 600;

QStringList splitList(const QString& text)
{
    QStringList out;
    for (const QString& part : text.split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && !out.contains(trimmed, Qt::CaseInsensitive)) {
            out.append(trimmed);
        }
    }
    return out;
}

QString defaultTopic(const QString& prompt)
{
    const QString oneLine = prompt.simplified();
    if (oneLine.size() <= 40) {
        return oneLine;
    }
    const int cut = oneLine.lastIndexOf(QLatin1Char(' '), 40);
    return oneLine.left(cut > 12 ? cut : 40).trimmed();
}

} // namespace

QuestionEditor::QuestionEditor(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("studyCard"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 9);
    root->setSpacing(5);

    m_title = makeLabel(QStringLiteral("Write a question"), "sectionTitle", this);
    root->addWidget(m_title);

    m_typeTabs = new PixelSegmentedControl(this);
    m_typeTabs->addTab(QStringLiteral("Multiple choice"));
    m_typeTabs->addTab(QStringLiteral("Multiple select"));
    m_typeTabs->addTab(QStringLiteral("Written"));
    root->addWidget(m_typeTabs);
    connect(m_typeTabs, &PixelSegmentedControl::tabClicked, this, [this](int) { emit buttonActivated(); });
    connect(m_typeTabs, &PixelSegmentedControl::currentChanged, this, [this](int) { applyType(); });

    root->addWidget(new QLabel(QStringLiteral("Question"), this));
    m_prompt = new QPlainTextEdit(this);
    m_prompt->setObjectName(QStringLiteral("questionPrompt"));
    m_prompt->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_prompt->setPlaceholderText(QStringLiteral("What do you want to be asked?"));
    m_prompt->setTabChangesFocus(true);
    m_prompt->setFixedHeight(58);
    root->addWidget(m_prompt);

    // Multiple choice and multiple select
    m_choiceBox = new QWidget(this);
    auto* choice = new QVBoxLayout(m_choiceBox);
    choice->setContentsMargins(0, 0, 0, 0);
    choice->setSpacing(4);
    m_choiceHint = makeLabel(QString(), "muted", m_choiceBox);
    choice->addWidget(m_choiceHint);
    for (int i = 0; i < kOptionRows; ++i) {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* check = new PixelCheckBox(QString(), m_choiceBox);
        check->setFixedWidth(26);
        check->setToolTip(QStringLiteral("Tick the correct answer"));
        check->setAccessibleName(QStringLiteral("Answer %1 is correct").arg(i + 1));
        auto* edit = new QLineEdit(m_choiceBox);
        edit->setStyleSheet(PixelTheme::textInputStyleSheet());
        edit->setPlaceholderText(QStringLiteral("Answer %1").arg(i + 1));
        edit->setMaxLength(300);
        row->addWidget(check);
        row->addWidget(edit, 1);
        choice->addLayout(row);
        m_optionChecks.append(check);
        m_optionEdits.append(edit);
        connect(check, &QCheckBox::toggled, this, [this, check](bool on) {
            // One right answer: ticking a box clears the others.
            if (on && !m_syncing && type() == QuestionType::SingleChoice) {
                m_syncing = true;
                for (PixelCheckBox* other : std::as_const(m_optionChecks)) {
                    if (other != check) {
                        other->setChecked(false);
                    }
                }
                m_syncing = false;
            }
        });
    }
    root->addWidget(m_choiceBox);

    // Written answer
    m_writtenBox = new QWidget(this);
    auto* written = new QVBoxLayout(m_writtenBox);
    written->setContentsMargins(0, 0, 0, 0);
    written->setSpacing(4);
    m_expected = new QLineEdit(m_writtenBox);
    m_expected->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_expected->setPlaceholderText(QStringLiteral("The correct answer"));
    m_alsoAccept = new QLineEdit(m_writtenBox);
    m_alsoAccept->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_alsoAccept->setPlaceholderText(QStringLiteral("Also accept (comma-separated, optional)"));
    m_keyPoints = new QLineEdit(m_writtenBox);
    m_keyPoints->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_keyPoints->setPlaceholderText(QStringLiteral("Key points a good answer covers (optional)"));
    written->addWidget(m_expected);
    written->addWidget(m_alsoAccept);
    written->addWidget(m_keyPoints);
    root->addWidget(m_writtenBox);

    m_topic = new QLineEdit(this);
    m_topic->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_topic->setPlaceholderText(QStringLiteral("Topic (optional, used to track what you find hard)"));
    m_topic->setMaxLength(80);
    m_explanation = new QLineEdit(this);
    m_explanation->setStyleSheet(PixelTheme::textInputStyleSheet());
    m_explanation->setPlaceholderText(QStringLiteral("Explanation shown after answering (optional)"));
    root->addWidget(m_topic);
    root->addWidget(m_explanation);

    m_error = makeLabel(QString(), "muted", this);
    m_error->setStyleSheet(QStringLiteral("color: #F48C94;"));
    m_error->hide();
    root->addWidget(m_error);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_cancelBtn = makeButton(QStringLiteral("Cancel"), this);
    m_saveAnotherBtn = makeButton(QStringLiteral("Save and add another"), this);
    m_saveBtn = makeButton(QStringLiteral("Save"), this, "primary");
    buttons->addStretch(1);
    buttons->addWidget(m_cancelBtn);
    buttons->addWidget(m_saveAnotherBtn);
    buttons->addWidget(m_saveBtn);
    root->addLayout(buttons);
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        submit(false);
    });
    connect(m_saveAnotherBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        submit(true);
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        cancel();
    });

    applyType();
    hide();
}

QuestionType QuestionEditor::type() const
{
    switch (m_typeTabs->currentIndex()) {
    case 1:
        return QuestionType::MultiSelect;
    case 2:
        return QuestionType::Written;
    default:
        return QuestionType::SingleChoice;
    }
}

void QuestionEditor::setType(QuestionType type)
{
    m_typeTabs->setCurrentIndex(type == QuestionType::MultiSelect ? 1 : type == QuestionType::Written ? 2 : 0);
    applyType();
}

void QuestionEditor::applyType()
{
    const QuestionType t = type();
    const bool written = t == QuestionType::Written;
    m_choiceBox->setVisible(!written);
    m_writtenBox->setVisible(written);
    m_choiceHint->setText(t == QuestionType::MultiSelect ? QStringLiteral("Tick every correct answer (at least one, not all).")
                                                         : QStringLiteral("Tick the one correct answer. Leave unused rows empty."));
    if (t == QuestionType::SingleChoice) {
        // Keep a single tick when switching from multiple select.
        bool seen = false;
        m_syncing = true;
        for (PixelCheckBox* check : std::as_const(m_optionChecks)) {
            if (check->isChecked()) {
                if (seen) {
                    check->setChecked(false);
                }
                seen = true;
            }
        }
        m_syncing = false;
    }
    setError(QString());
}

void QuestionEditor::setError(const QString& message)
{
    m_error->setText(message);
    m_error->setVisible(!message.isEmpty());
}

void QuestionEditor::clearFields()
{
    m_syncing = true;
    m_prompt->clear();
    m_topic->clear();
    m_explanation->clear();
    for (int i = 0; i < m_optionEdits.size(); ++i) {
        m_optionEdits.at(i)->clear();
        m_optionChecks.at(i)->setChecked(false);
    }
    m_expected->clear();
    m_alsoAccept->clear();
    m_keyPoints->clear();
    m_syncing = false;
    setError(QString());
}

void QuestionEditor::startNew()
{
    m_editingId.clear();
    m_editingDifficulty = Difficulty::Medium;
    clearFields();
    setType(QuestionType::SingleChoice);
    m_title->setText(QStringLiteral("Write a question"));
    m_saveAnotherBtn->show();
    show();
    m_prompt->setFocus();
}

void QuestionEditor::startEdit(const Question& question)
{
    m_editingId = question.id;
    m_editingDifficulty = question.difficulty;
    clearFields();
    Input in;
    in.type = question.type;
    in.prompt = question.prompt;
    in.topic = question.topic;
    in.explanation = question.explanation;
    for (const QuestionOption& option : question.options) {
        in.options.append(option.text);
        in.correct.append(option.correct);
    }
    in.expected = question.expectedAnswer;
    if (!question.answers.isEmpty()) {
        in.alsoAccept = question.answers.first().aliases;
    }
    if (!(question.keyConcepts.size() == 1 && question.keyConcepts.first() == question.expectedAnswer)) {
        in.keyPoints = question.keyConcepts;
    }
    setInput(in);
    m_title->setText(QStringLiteral("Edit question"));
    m_saveAnotherBtn->hide();
    show();
    m_prompt->setFocus();
}

void QuestionEditor::cancel()
{
    m_editingId.clear();
    clearFields();
    hide();
    emit cancelled();
}

QuestionEditor::Input QuestionEditor::input() const
{
    Input in;
    in.type = type();
    in.prompt = m_prompt->toPlainText();
    in.topic = m_topic->text();
    in.explanation = m_explanation->text();
    for (int i = 0; i < m_optionEdits.size(); ++i) {
        in.options.append(m_optionEdits.at(i)->text());
        in.correct.append(m_optionChecks.at(i)->isChecked());
    }
    in.expected = m_expected->text();
    in.alsoAccept = splitList(m_alsoAccept->text());
    in.keyPoints = splitList(m_keyPoints->text());
    return in;
}

void QuestionEditor::setInput(const Input& in)
{
    m_syncing = true;
    setType(in.type);
    m_prompt->setPlainText(in.prompt);
    m_topic->setText(in.topic);
    m_explanation->setText(in.explanation);
    for (int i = 0; i < m_optionEdits.size(); ++i) {
        m_optionEdits.at(i)->setText(i < in.options.size() ? in.options.at(i) : QString());
        m_optionChecks.at(i)->setChecked(i < in.correct.size() && in.correct.at(i));
    }
    m_expected->setText(in.expected);
    m_alsoAccept->setText(in.alsoAccept.join(QStringLiteral(", ")));
    m_keyPoints->setText(in.keyPoints.join(QStringLiteral(", ")));
    m_syncing = false;
}

bool QuestionEditor::build(const Input& in, Question& out, QString& error)
{
    const QString prompt = in.prompt.trimmed();
    if (prompt.isEmpty()) {
        error = QStringLiteral("Write the question first.");
        return false;
    }
    if (prompt.size() > kMaxPromptChars) {
        error = QStringLiteral("That question is too long (%1 characters at most).").arg(kMaxPromptChars);
        return false;
    }
    Question q;
    q.type = in.type;
    q.prompt = prompt;
    q.topic = in.topic.trimmed().isEmpty() ? defaultTopic(prompt) : in.topic.trimmed();
    q.explanation = in.explanation.trimmed();
    q.difficulty = Difficulty::Medium;

    if (in.type == QuestionType::Written) {
        const QString expected = in.expected.trimmed();
        if (expected.isEmpty()) {
            error = QStringLiteral("Add the correct answer.");
            return false;
        }
        q.expectedAnswer = expected;
        QStringList aliases;
        for (const QString& alias : in.alsoAccept) {
            const QString a = alias.trimmed();
            if (!a.isEmpty() && a.compare(expected, Qt::CaseInsensitive) != 0 && !aliases.contains(a, Qt::CaseInsensitive)) {
                aliases.append(a);
            }
        }
        q.answers.append({expected, aliases});
        for (const QString& point : in.keyPoints) {
            if (!point.trimmed().isEmpty()) {
                q.keyConcepts.append(point.trimmed());
            }
        }
        if (q.keyConcepts.isEmpty()) {
            q.keyConcepts.append(expected);
        }
    } else {
        QSet<QString> seen;
        int correctCount = 0;
        int count = 0;
        for (int i = 0; i < in.options.size(); ++i) {
            const QString text = in.options.at(i).trimmed();
            if (text.isEmpty()) {
                continue;
            }
            const QString key = QuestionValidator::normalizeForMatch(text);
            if (seen.contains(key)) {
                error = QStringLiteral("Two answers are the same: \"%1\".").arg(text);
                return false;
            }
            seen.insert(key);
            const bool correct = i < in.correct.size() && in.correct.at(i);
            correctCount += correct ? 1 : 0;
            QuestionOption option;
            option.id = QString(QChar(static_cast<char16_t>('a' + count)));
            option.text = text;
            option.correct = correct;
            q.options.append(option);
            ++count;
        }
        if (count < 2) {
            error = QStringLiteral("Add at least two answers.");
            return false;
        }
        if (in.type == QuestionType::SingleChoice && correctCount != 1) {
            error = correctCount == 0 ? QStringLiteral("Tick the correct answer.") : QStringLiteral("Only one answer can be correct.");
            return false;
        }
        if (in.type == QuestionType::MultiSelect && (correctCount == 0 || correctCount == count)) {
            error = correctCount == 0 ? QStringLiteral("Tick every correct answer.")
                                      : QStringLiteral("Not every answer can be correct. Untick at least one.");
            return false;
        }
    }
    out = q;
    error.clear();
    return true;
}

void QuestionEditor::submit(bool addAnother)
{
    Question q;
    QString error;
    if (!build(input(), q, error)) {
        setError(error);
        return;
    }
    setError(QString());
    const bool edit = !m_editingId.isEmpty();
    q.id = m_editingId;
    q.difficulty = edit ? m_editingDifficulty : Difficulty::Medium;
    emit saveRequested(q, addAnother, edit);
    if (addAnother) {
        const QuestionType keep = type();
        m_editingId.clear();
        clearFields();
        setType(keep);
        m_prompt->setFocus();
    } else {
        m_editingId.clear();
        clearFields();
        hide();
    }
}
