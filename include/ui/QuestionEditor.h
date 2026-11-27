#pragma once

#include "study/Question.h"

#include <QFrame>
#include <QList>
#include <QStringList>

class PixelCheckBox;
class PixelSegmentedControl;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QWidget;

/**
 * Inline form for writing your own question: multiple choice, multiple select, or written
 * answer. It validates and builds a Question; storing it is the host's job.
 */
class QuestionEditor : public QFrame {
    Q_OBJECT

public:
    static constexpr int kOptionRows = 4;

    /** Raw form contents, separate from the widgets so validation can be tested directly. */
    struct Input {
        QuestionType type = QuestionType::SingleChoice;
        QString prompt;
        QString topic;
        QString explanation;
        QStringList options;      // choice types; blank rows are ignored
        QList<bool> correct;      // same length as options
        QString expected;         // written answer
        QStringList alsoAccept;   // written: other accepted answers
        QStringList keyPoints;    // written: optional key points (defaults to the expected answer)
    };

    explicit QuestionEditor(QWidget* parent = nullptr);

    /** Validates the input and builds a question (id and set are filled in by the host). */
    [[nodiscard]] static bool build(const Input& input, Question& out, QString& error);

    void startNew();
    void startEdit(const Question& question);
    void cancel();
    [[nodiscard]] bool isEditingExisting() const { return !m_editingId.isEmpty(); }
    [[nodiscard]] QString editingId() const { return m_editingId; }

    [[nodiscard]] Input input() const;
    void setInput(const Input& input);
    void setType(QuestionType type);
    void setError(const QString& message);

    // Accessors for tests
    [[nodiscard]] QPlainTextEdit* promptEdit() const { return m_prompt; }
    [[nodiscard]] QLineEdit* optionEdit(int row) const { return m_optionEdits.value(row, nullptr); }
    [[nodiscard]] PixelCheckBox* optionCheck(int row) const { return m_optionChecks.value(row, nullptr); }
    [[nodiscard]] QLineEdit* expectedEdit() const { return m_expected; }
    [[nodiscard]] QPushButton* saveButton() const { return m_saveBtn; }
    [[nodiscard]] QPushButton* saveAnotherButton() const { return m_saveAnotherBtn; }
    [[nodiscard]] QPushButton* cancelButton() const { return m_cancelBtn; }
    [[nodiscard]] QLabel* errorLabel() const { return m_error; }
    [[nodiscard]] QLabel* titleLabel() const { return m_title; }
    [[nodiscard]] PixelSegmentedControl* typeTabs() const { return m_typeTabs; }
    [[nodiscard]] QuestionType type() const;

signals:
    /** A valid question was built. isEdit is true when it replaces an existing question. */
    void saveRequested(const Question& question, bool addAnother, bool isEdit);
    void cancelled();
    void buttonActivated();

private:
    void submit(bool addAnother);
    void applyType();
    void clearFields();

    PixelSegmentedControl* m_typeTabs = nullptr;
    QLabel* m_title = nullptr;
    QPlainTextEdit* m_prompt = nullptr;
    QLineEdit* m_topic = nullptr;
    QLineEdit* m_explanation = nullptr;
    QWidget* m_choiceBox = nullptr;
    QLabel* m_choiceHint = nullptr;
    QList<PixelCheckBox*> m_optionChecks;
    QList<QLineEdit*> m_optionEdits;
    QWidget* m_writtenBox = nullptr;
    QLineEdit* m_expected = nullptr;
    QLineEdit* m_alsoAccept = nullptr;
    QLineEdit* m_keyPoints = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_saveAnotherBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    QString m_editingId;
    Difficulty m_editingDifficulty = Difficulty::Medium;
    bool m_syncing = false;
};
