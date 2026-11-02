#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QPlainTextEdit;

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

// Internal: QPlainTextEdit with a line-number gutter and a current-line
// highlight, both colored from the theme tokens. Defined in the .cpp.
class CodeEditor;

// Internal: collapsible find/replace bar above the editor. Defined in the .cpp.
class FindBar;

class SqlEditor : public QWidget {
    Q_OBJECT
public:
    explicit SqlEditor(QWidget *parent = nullptr);

    QPlainTextEdit *editorWidget() const;
    void setPlainText(const QString &text);
    // Replaces the document while keeping the undo stack, so generated SQL
    // (history, table double-click) can be undone back to the typed text.
    void replaceText(const QString &text);
    QString toPlainText() const;
    // Selected text with paragraph separators normalized to '\n', or empty.
    QString selectedText() const;
    void retranslateUi();
    void retheme();

signals:
    void executeRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void openFindBar();
    void closeFindBar();
    void updateMatchCount(int count);

    CodeEditor *m_editor = nullptr;
    SqlHighlighter *m_highlighter = nullptr;
    FindBar *m_findBar = nullptr;
};

} // namespace ducq
