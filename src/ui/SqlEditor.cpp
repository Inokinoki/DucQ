#include "SqlEditor.h"

#include "Theme.h"

#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

namespace ducq {
namespace {

const char *const kKeywords[] = {
    "SELECT", "FROM",     "WHERE",     "GROUP",   "BY",      "HAVING",  "ORDER",  "LIMIT",   "OFFSET",
    "WITH",   "AS",       "DISTINCT",  "ALL",     "UNION",   "INTERSECT", "EXCEPT", "INSERT", "INTO",
    "VALUES", "UPDATE",   "SET",       "DELETE",  "CREATE",  "TABLE",   "VIEW",   "SCHEMA",  "DATABASE",
    "DROP",   "ALTER",    "ATTACH",    "DETACH",  "USE",     "JOIN",    "LEFT",   "RIGHT",   "INNER",
    "OUTER",  "FULL",     "CROSS",     "ON",      "USING",   "CASE",    "WHEN",   "THEN",    "ELSE",
    "END",    "NULL",     "IS",        "NOT",     "AND",     "OR",      "IN",     "BETWEEN", "LIKE",
    "ILIKE",  "EXISTS",   "CAST",      "INTERVAL", "TRUE",   "FALSE",   "BEGIN",  "COMMIT",  "ROLLBACK",
    "TRANSACTION", "PRIMARY", "FOREIGN", "KEY", "REFERENCES", "UNIQUE", "CHECK",  "DEFAULT", "CONSTRAINT",
    "COPY",   "TO",       "FORMAT",    "HEADER",  "DELIMITER", "QUOTE",  "ESCAPE", "EXPLAIN", "ANALYZE",
    "PRAGMA", "DESCRIBE", "SHOW",      "TABLES",  "SUMMARY", "WINDOW", "OVER",   "PARTITION", "ROWS",
    "RANGE",  "PRECEDING", "FOLLOWING", "CURRENT", "UNBOUNDED", "RETURNING", "IF",  "ASC",     "DESC",
    "NULLS",  "FIRST",    "LAST",      "COLLATE",
};

} // namespace

SqlHighlighter::SqlHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    QString keywordPattern = QStringLiteral("\\b(?:");
    for (const char *keyword : kKeywords) {
        keywordPattern += QLatin1String(keyword);
        keywordPattern += QLatin1Char('|');
    }
    keywordPattern.chop(1);
    keywordPattern += QStringLiteral(")\\b");
    m_rules.append({ QRegularExpression(keywordPattern, QRegularExpression::CaseInsensitiveOption),
                     QTextCharFormat() });
    m_rules.append({ QRegularExpression(QStringLiteral("'[^']*'")), QTextCharFormat() });
    m_rules.append({ QRegularExpression(QStringLiteral("\\b(?:\\d+\\.?\\d*|\\.\\d+)\\b")),
                     QTextCharFormat() });
    retheme();
}

void SqlHighlighter::retheme()
{
    const Theme::Tokens &t = Theme::tokens();

    QTextCharFormat &keywordFormat = m_rules[0].format;
    keywordFormat.setForeground(t.keyword);
    keywordFormat.setFontWeight(QFont::Bold);

    m_rules[1].format.setForeground(t.string);
    m_rules[2].format.setForeground(t.number);

    m_commentFormat.setForeground(t.comment);
    m_commentFormat.setFontItalic(true);
    rehighlight();
}

void SqlHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightRule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
    // Comments last so they override keyword/string/number coloring.
    static const QRegularExpression commentPattern(QStringLiteral("--[^\\n]*"));
    auto it = commentPattern.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        setFormat(match.capturedStart(), match.capturedLength(), m_commentFormat);
    }
}

SqlEditor::SqlEditor(QWidget *parent)
    : QWidget(parent)
{
    m_editor = new QPlainTextEdit(this);
    m_editor->setFrameShape(QFrame::NoFrame);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(qMax(font.pointSize(), 12));
    m_editor->setFont(font);
    m_editor->setTabStopDistance(4 * m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    m_editor->installEventFilter(this);

    m_highlighter = new SqlHighlighter(m_editor->document());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);

    retranslateUi();
}

void SqlEditor::retheme()
{
    m_highlighter->retheme();
}

void SqlEditor::retranslateUi()
{
    m_editor->setPlaceholderText(
        tr("-- Ctrl+Enter runs the SQL, e.g.:\nSELECT * FROM range(10);"));
}

bool SqlEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isExecute = (keyEvent->modifiers() & Qt::ControlModifier)
            && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter);
        if (isExecute) {
            emit executeRequested();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SqlEditor::setPlainText(const QString &text)
{
    m_editor->setPlainText(text);
}

void SqlEditor::replaceText(const QString &text)
{
    // Replace through a cursor instead of setPlainText so the change lands on
    // the undo stack: generated SQL can be undone back to what was typed.
    QTextCursor cursor = m_editor->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);
}

QString SqlEditor::toPlainText() const
{
    return m_editor->toPlainText();
}

QString SqlEditor::selectedText() const
{
    QTextCursor cursor = m_editor->textCursor();
    return cursor.hasSelection() ? cursor.selectedText() : QString();
}

} // namespace ducq
