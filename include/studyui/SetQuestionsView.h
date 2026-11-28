#pragma once

#include "study/Question.h"

#include <QHash>
#include <QWidget>

class PixelIconButton;
class PixelSegmentedControl;
class QLabel;
class QMenu;
class QPushButton;
class QVBoxLayout;

/**
 * Every question of the open study set in one list, filterable (All, Needs context, Written by
 * you). A row shows the format, any figure/table/data it carries, the question and where it comes
 * from; Edit, Rewrite and Delete sit in the row's "more" menu. Questions that cannot be answered
 * from what they show are tagged "Needs context" and can all be rewritten at once.
 */
class SetQuestionsView : public QWidget {
    Q_OBJECT

public:
    enum class Filter {
        All = 0,
        NeedsContext = 1,
        Written = 2
    };

    explicit SetQuestionsView(QWidget* parent = nullptr);

    /** canRewrite is false while the generator is busy. */
    void setQuestions(const QVector<Question>& questions, bool canRewrite);
    void setFilter(Filter filter);
    [[nodiscard]] Filter filter() const { return m_filter; }

    [[nodiscard]] int count() const { return static_cast<int>(m_questions.size()); }
    [[nodiscard]] int needsContextCount() const;
    /** "[Choice · table] Which rows … (slide 3) · Edited by you", as shown on the row. */
    [[nodiscard]] static QString rowText(const Question& question);

    // Accessors for tests
    [[nodiscard]] QStringList shownIds() const;
    [[nodiscard]] PixelSegmentedControl* filterTabs() const { return m_filters; }
    [[nodiscard]] PixelIconButton* menuButton(const QString& questionId) const { return m_menus.value(questionId); }
    /** The menu a row's "more" button opens (built on demand). */
    [[nodiscard]] QMenu* menuFor(const QString& questionId);
    [[nodiscard]] QPushButton* fixAllButton() const { return m_fixAll; }
    [[nodiscard]] QLabel* emptyLabel() const { return m_empty; }

signals:
    void editRequested(const QString& questionId);
    void rewriteRequested(const QString& questionId);
    void deleteRequested(const QString& questionId);
    /** Rewrite every question that needs context. */
    void fixAllRequested();
    void buttonActivated();

private:
    void rebuild();

    PixelSegmentedControl* m_filters = nullptr;
    QWidget* m_fixRow = nullptr;
    QLabel* m_fixText = nullptr;
    QPushButton* m_fixAll = nullptr;
    QLabel* m_empty = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QList<QWidget*> m_rowWidgets;
    QHash<QString, PixelIconButton*> m_menus;
    QVector<Question> m_questions;
    bool m_canRewrite = true;
    Filter m_filter = Filter::All;
};
