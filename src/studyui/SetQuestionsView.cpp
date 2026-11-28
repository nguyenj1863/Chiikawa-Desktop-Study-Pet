#include "studyui/SetQuestionsView.h"

#include "study/generation/QuestionValidator.h"
#include "studyui/StudyUiHelpers.h"
#include "ui/PixelSegmentedControl.h"
#include "ui/PixelTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

using StudyUi::makeButton;
using StudyUi::makeLabel;

namespace {

constexpr int kMaxPromptChars = 150;

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

bool matches(const Question& q, SetQuestionsView::Filter filter)
{
    switch (filter) {
    case SetQuestionsView::Filter::All:
        return true;
    case SetQuestionsView::Filter::NeedsContext:
        return QuestionValidator::needsContext(q);
    case SetQuestionsView::Filter::Written:
        return q.origin == QuestionOrigin::Written;
    }
    return true;
}

} // namespace

SetQuestionsView::SetQuestionsView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docList"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 4, 0);
    root->setSpacing(5);

    m_filters = new PixelSegmentedControl(this);
    m_filters->addTab(QStringLiteral("All"));
    m_filters->addTab(QStringLiteral("Needs context"));
    m_filters->addTab(QStringLiteral("Written by you"));
    root->addWidget(m_filters);
    connect(m_filters, &PixelSegmentedControl::tabClicked, this, [this](int) { emit buttonActivated(); });
    connect(m_filters, &PixelSegmentedControl::currentChanged, this, [this](int index) {
        m_filter = static_cast<Filter>(index);
        rebuild();
    });

    // Questions that lean on something the student cannot see: say how many, fix them in one go.
    m_fixRow = new QWidget(this);
    auto* fix = new QHBoxLayout(m_fixRow);
    fix->setContentsMargins(0, 0, 0, 0);
    fix->setSpacing(6);
    m_fixText = makeLabel(QString(), "errorNote", m_fixRow);
    fix->addWidget(m_fixText, 1);
    m_fixAll = makeButton(QStringLiteral("Fix all"), m_fixRow, "chip");
    m_fixAll->setToolTip(QStringLiteral("Rewrite each one from its passage so it shows what it needs"));
    fix->addWidget(m_fixAll, 0, Qt::AlignVCenter);
    m_fixRow->hide();
    root->addWidget(m_fixRow);
    connect(m_fixAll, &QPushButton::clicked, this, [this]() {
        emit buttonActivated();
        emit fixAllRequested();
    });

    m_empty = makeLabel(QStringLiteral("No questions yet."), "muted", this);
    root->addWidget(m_empty);
    m_rows = new QVBoxLayout();
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(4);
    root->addLayout(m_rows);
    root->addStretch(1);
}

QString SetQuestionsView::rowText(const Question& q)
{
    QStringList tags = {typeTag(q.type)};
    for (const SupportRef& s : q.support) {
        tags.append(s.kind == SupportKind::Table ? QStringLiteral("table")
                    : s.kind == SupportKind::Image ? QStringLiteral("figure") : QStringLiteral("data"));
    }
    if (QuestionValidator::needsContext(q)) {
        tags.prepend(QStringLiteral("Needs context"));
    }
    const QString prompt = q.prompt.size() > kMaxPromptChars ? q.prompt.left(kMaxPromptChars - 1).trimmed() + QStringLiteral("…")
                                                             : q.prompt;
    QString text = QStringLiteral("[%1] %2").arg(tags.join(QStringLiteral(" · ")), prompt);
    const QString location = q.sources.isEmpty() ? QString() : q.sources.first().location;
    if (!location.isEmpty() && q.origin != QuestionOrigin::Written) {
        text += QStringLiteral("  (%1)").arg(location);
    }
    if (q.origin == QuestionOrigin::Edited) {
        text += QStringLiteral(" · Edited by you");
    } else if (q.origin == QuestionOrigin::Written) {
        text += QStringLiteral(" · Written by you");
    }
    return text;
}

int SetQuestionsView::needsContextCount() const
{
    int n = 0;
    for (const Question& q : m_questions) {
        n += QuestionValidator::needsContext(q) ? 1 : 0;
    }
    return n;
}

void SetQuestionsView::setQuestions(const QVector<Question>& questions, bool canRewrite)
{
    m_questions = questions;
    m_canRewrite = canRewrite;
    rebuild();
}

void SetQuestionsView::setFilter(Filter filter)
{
    m_filters->setCurrentIndex(static_cast<int>(filter));   // rebuilds through currentChanged
    m_filter = filter;
    rebuild();
}

QStringList SetQuestionsView::shownIds() const
{
    QStringList ids;
    for (const QWidget* row : m_rowWidgets) {
        ids.append(row->property("questionId").toString());
    }
    return ids;
}

QMenu* SetQuestionsView::menuFor(const QString& questionId)
{
    const auto it = std::find_if(m_questions.cbegin(), m_questions.cend(),
                                 [&](const Question& q) { return q.id == questionId; });
    if (it == m_questions.cend()) {
        return nullptr;
    }
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("rowMenu"));
    menu->addAction(QStringLiteral("Edit"), this, [this, questionId]() { emit editRequested(questionId); });
    if (it->origin != QuestionOrigin::Written) {
        QAction* rewrite = menu->addAction(QStringLiteral("Rewrite"), this,
                                           [this, questionId]() { emit rewriteRequested(questionId); });
        rewrite->setEnabled(m_canRewrite && it->chunkId > 0);
        rewrite->setToolTip(QStringLiteral("Write a new version from the same passage; you choose to replace it or keep both"));
    }
    menu->addSeparator();
    menu->addAction(QStringLiteral("Delete"), this, [this, questionId]() { emit deleteRequested(questionId); });
    return menu;
}

void SetQuestionsView::rebuild()
{
    for (QWidget* row : std::as_const(m_rowWidgets)) {
        m_rows->removeWidget(row);
        row->hide();
        row->setParent(nullptr);   // gone from the view right away, not only once deleted
        row->deleteLater();
    }
    m_rowWidgets.clear();
    m_menus.clear();

    const int flagged = needsContextCount();
    m_fixText->setText(QStringLiteral("%1 question%2 need%3 context to be answerable.")
                           .arg(flagged)
                           .arg(flagged == 1 ? QString() : QStringLiteral("s"))
                           .arg(flagged == 1 ? QStringLiteral("s") : QString()));
    m_fixRow->setVisible(flagged > 0);
    m_fixAll->setEnabled(m_canRewrite);
    m_filters->setTabText(1, flagged > 0 ? QStringLiteral("Needs context (%1)").arg(flagged) : QStringLiteral("Needs context"));

    for (const Question& q : std::as_const(m_questions)) {
        if (!matches(q, m_filter)) {
            continue;
        }
        auto* row = new QWidget(this);
        row->setProperty("questionId", q.id);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(4);
        auto* label = makeLabel(rowText(q), QuestionValidator::needsContext(q) ? "errorNote" : "muted", row);
        label->setTextFormat(Qt::PlainText);
        label->setProperty("questionId", q.id);
        label->setToolTip(q.sources.isEmpty() || q.sources.first().excerpt.isEmpty()
                              ? q.prompt
                              : QStringLiteral("%1\n\nFrom: “%2”").arg(q.prompt, q.sources.first().excerpt));
        line->addWidget(label, 1);
        auto* more = new PixelIconButton(PixelIcon::More, 22, 10, false, row);
        more->setToolTip(QStringLiteral("Edit, rewrite or delete"));
        more->setAccessibleName(QStringLiteral("More actions for this question"));
        line->addWidget(more, 0, Qt::AlignTop);
        connect(more, &QPushButton::clicked, this, [this, more, id = q.id]() {
            emit buttonActivated();
            if (QMenu* menu = menuFor(id)) {
                menu->popup(more->mapToGlobal(QPoint(0, more->height())));
            }
        });
        m_rows->addWidget(row);
        m_rowWidgets.append(row);
        m_menus.insert(q.id, more);
    }
    const bool none = m_rowWidgets.isEmpty();
    m_empty->setText(m_questions.isEmpty() ? QStringLiteral("No questions yet. Generate some from your materials, or write your own.")
                     : m_filter == Filter::NeedsContext ? QStringLiteral("Every question shows what it needs.")
                                                        : QStringLiteral("No questions here yet."));
    m_empty->setVisible(none);
}
