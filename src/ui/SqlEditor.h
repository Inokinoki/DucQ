#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>
#include <QWidget>

class QPlainTextEdit;
class QKeyEvent;

namespace ducq {

class SqlHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SqlHighlighter(QTextDocument *document);

    // Re-reads the theme tokens and re-highlights the document.
    void retheme();

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightRule> m_rules;
    QTextCharFormat m_commentFormat; // applied last so it wins over keywords
};

class SqlEditor : public QWidget {
    Q_OBJECT
public:
    explicit SqlEditor(QWidget *parent = nullptr);

    QPlainTextEdit *editorWidget() const { return m_editor; }
    void setPlainText(const QString &text);
    // Replaces the document while keeping the undo stack, so generated SQL
    // (history, table double-click) can be undone back to the typed text.
    void replaceText(const QString &text);
    QString toPlainText() const;
    // Selected text, if any (used to run only the selection).
    QString selectedText() const;
    void retranslateUi();
    void retheme();

signals:
    void executeRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPlainTextEdit *m_editor = nullptr;
    SqlHighlighter *m_highlighter = nullptr;
};

} // namespace ducq
