#pragma once

#include "study/Question.h"

#include <QDialog>
#include <QPixmap>
#include <QWidget>

class QLabel;
class QVBoxLayout;

/** An image that keeps its aspect ratio at any width (capped height); clicking opens it larger. */
class SupportImage : public QWidget {
    Q_OBJECT

public:
    static constexpr int kMaxHeight = 240;

    explicit SupportImage(const QPixmap& pixmap, QWidget* parent = nullptr);

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] const QPixmap& pixmap() const { return m_pixmap; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPixmap m_pixmap;
};

/** Frameless viewer that shows a figure as large as the screen allows; click or Esc closes it. */
class SupportImageViewer : public QDialog {
    Q_OBJECT

public:
    SupportImageViewer(const QPixmap& pixmap, const QString& caption, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPixmap m_pixmap;
};

/**
 * The visuals a question depends on (figure, table, or data snippet), shown between the
 * question and its answers so the student always sees what the question refers to.
 */
class SupportView : public QWidget {
    Q_OBJECT

public:
    explicit SupportView(QWidget* parent = nullptr);

    /** location is where the question comes from ("p. 4"); captions name it so the page reference stays visible. */
    void setSupport(const QVector<SupportRef>& support, const QString& location = QString());
    [[nodiscard]] bool isEmpty() const { return m_count == 0; }

    // Accessors for tests
    [[nodiscard]] QList<SupportImage*> images() const;
    [[nodiscard]] QList<QLabel*> tables() const;
    [[nodiscard]] QList<QLabel*> captions() const;

    /** Rich-text table for a QLabel, using the theme's text colours. */
    [[nodiscard]] static QString tableHtml(const TableData& table);
    /** "Figure 1: Caches · p. 4", "From p. 4" when there is no caption, or just the caption. */
    [[nodiscard]] static QString captionFor(const SupportRef& ref, const QString& location);

signals:
    void imageOpened();

private:
    QVBoxLayout* m_layout = nullptr;
    int m_count = 0;
};
