#include "SqlEditor.h"

#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPlainTextEdit>
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
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0x05, 0x50, 0xae));
    keywordFormat.setFontWeight(QFont::Bold);

    QString keywordPattern = QStringLiteral("\\b(?:");
    for (const char *keyword : kKeywords) {
        keywordPattern += QLatin1String(keyword);
        keywordPattern += QLatin1Char('|');
    }
    keywordPattern.chop(1);
    keywordPattern += QStringLiteral(")\\b");
    m_rules.append({ QRegularExpression(keywordPattern, QRegularExpression::CaseInsensitiveOption),
                     keywordFormat });

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(0x0a, 0x7a, 0x2f));
    m_rules.append({ QRegularExpression(QStringLiteral("'[^']*'")), stringFormat });

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0x95, 0x38, 0x00));
    m_rules.append({ QRegularExpression(QStringLiteral("\\b(?:\\d+\\.?\\d*|\\.\\d+)\\b")), numberFormat });

    m_commentFormat.setForeground(QColor(0x6e, 0x77, 0x81));
    m_commentFormat.setFontItalic(true);
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
    m_editor->setPlaceholderText(QStringLiteral("-- Ctrl+Enter 执行 SQL，示例：\nSELECT * FROM range(10);"));
    m_editor->installEventFilter(this);

    new SqlHighlighter(m_editor->document());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);
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

QString SqlEditor::toPlainText() const
{
    return m_editor->toPlainText();
}

} // namespace ducq
