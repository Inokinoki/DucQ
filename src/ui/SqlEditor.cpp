#include "SqlEditor.h"

#include "Theme.h"

#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

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

constexpr int kMaxCountedMatches = 10000;
} // namespace

// ---------------------------------------------------------------------------
// CodeEditor: the editor surface with a line-number gutter and a subtle
// current-line highlight. No Q_OBJECT needed — everything is lambdas.
// ---------------------------------------------------------------------------
class CodeEditor : public QPlainTextEdit {
public:
    explicit CodeEditor(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_lineNumberArea(new LineNumberArea(this))
    {
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this] { updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect &rect, int dy) { updateLineNumberArea(rect, dy); });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this,
                [this] { highlightCurrentLine(); });
        updateLineNumberAreaWidth();
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
            ++digits;
        return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), Theme::tokens().window);
        const QColor activeNumber = Theme::tokens().text;
        const QColor otherNumber = Theme::tokens().placeholder;
        painter.setFont(font());

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        const int cursorBlock = textCursor().blockNumber();
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                painter.setPen(blockNumber == cursorBlock ? activeNumber : otherNumber);
                painter.drawText(0, top, m_lineNumberArea->width() - 8, fontMetrics().height(),
                                 Qt::AlignRight, QString::number(blockNumber + 1));
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

    // Re-derives theme-dependent visuals.
    void retheme()
    {
        highlightCurrentLine();
        m_lineNumberArea->update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect contents = contentsRect();
        m_lineNumberArea->setGeometry(contents.left(), contents.top(), lineNumberAreaWidth(),
                                      contents.height());
    }

private:
    void updateLineNumberAreaWidth() { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

    void updateLineNumberArea(const QRect &rect, int dy)
    {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth();
    }

    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> selections;
        if (!isReadOnly()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(Theme::tokens().currentLine);
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selections.append(selection);
        }
        setExtraSelections(selections);
    }

    class LineNumberArea : public QWidget {
    public:
        explicit LineNumberArea(CodeEditor *editor)
            : QWidget(editor)
            , m_editor(editor)
        {
        }

        QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

    protected:
        void paintEvent(QPaintEvent *event) override
        {
            m_editor->lineNumberAreaPaintEvent(event);
        }

    private:
        CodeEditor *m_editor;
    };

    LineNumberArea *m_lineNumberArea = nullptr;
};

// ---------------------------------------------------------------------------
// FindBar: collapsible find/replace strip. Strings arrive translated from
// SqlEditor::retranslateUi; search runs against the CodeEditor document.
// ---------------------------------------------------------------------------
class FindBar : public QWidget {
public:
    FindBar(CodeEditor *editor, QWidget *parent)
        : QWidget(parent)
        , m_editor(editor)
    {
        m_findInput = new QLineEdit(this);
        m_replaceInput = new QLineEdit(this);
        m_countLabel = new QLabel(this);
        m_countLabel->setMinimumWidth(60);

        auto makeArrowButton = [this](Qt::ArrowType arrow) {
            auto *button = new QToolButton(this);
            button->setArrowType(arrow);
            button->setAutoRaise(true);
            return button;
        };
        m_prevButton = makeArrowButton(Qt::UpArrow);
        m_nextButton = makeArrowButton(Qt::DownArrow);
        m_closeButton = makeArrowButton(Qt::RightArrow); // styled in retranslate via tooltip
        m_replaceButton = new QPushButton(this);
        m_replaceAllButton = new QPushButton(this);

        auto *findRow = new QHBoxLayout(this);
        findRow->setContentsMargins(6, 4, 6, 4);
        findRow->addWidget(m_findInput, 1);
        findRow->addWidget(m_countLabel);
        findRow->addWidget(m_prevButton);
        findRow->addWidget(m_nextButton);
        findRow->addWidget(m_replaceInput, 1);
        findRow->addWidget(m_replaceButton);
        findRow->addWidget(m_replaceAllButton);
        findRow->addWidget(m_closeButton);

        connect(m_findInput, &QLineEdit::textChanged, this, [this] { countMatches(); });
        connect(m_findInput, &QLineEdit::returnPressed, this, [this] { findNext(false); });
        connect(m_prevButton, &QToolButton::clicked, this, [this] { findNext(true); });
        connect(m_nextButton, &QToolButton::clicked, this, [this] { findNext(false); });
        connect(m_replaceButton, &QPushButton::clicked, this, [this] { replaceCurrent(); });
        connect(m_replaceAllButton, &QPushButton::clicked, this, [this] { replaceAll(); });
        connect(m_closeButton, &QToolButton::clicked, this, [this] { hide(); });

        // Shift+Enter searches backwards from the find field.
        m_findInput->installEventFilter(this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_findInput && event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return && (keyEvent->modifiers() & Qt::ShiftModifier)) {
                findNext(true);
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    QLineEdit *findInput() const { return m_findInput; }

    void focusFind()
    {
        show();
        m_findInput->setFocus();
        m_findInput->selectAll();
    }

    void retranslate(const QString &findPlaceholder, const QString &replacePlaceholder,
                     const QString &prevTip, const QString &nextTip, const QString &replaceText,
                     const QString &replaceAllText, const QString &closeTip)
    {
        m_findInput->setPlaceholderText(findPlaceholder);
        m_replaceInput->setPlaceholderText(replacePlaceholder);
        m_prevButton->setToolTip(prevTip);
        m_nextButton->setToolTip(nextTip);
        m_closeButton->setToolTip(closeTip);
        m_replaceButton->setText(replaceText);
        m_replaceAllButton->setText(replaceAllText);
    }

    // The callback receives the match count (0 = none) so the owner can show
    // it in its own translated wording.
    void setCountCallback(std::function<void(int)> callback) { m_countCallback = std::move(callback); }

    void setCountText(const QString &text) { m_countLabel->setText(text); }

    int countMatches()
    {
        const QString needle = m_findInput->text();
        int count = 0;
        if (!needle.isEmpty()) {
            QTextCursor cursor(m_editor->document());
            while (!(cursor = m_editor->document()->find(needle, cursor)).isNull()) {
                ++count;
                if (count >= kMaxCountedMatches)
                    break;
            }
        }
        if (m_countCallback)
            m_countCallback(count);
        return count;
    }

    void findNext(bool backward)
    {
        const QString needle = m_findInput->text();
        if (needle.isEmpty())
            return;
        QTextDocument::FindFlags flags;
        if (backward)
            flags |= QTextDocument::FindBackward;
        QTextCursor found = m_editor->document()->find(needle, m_editor->textCursor(), flags);
        if (found.isNull()) {
            // Wrap around: from the start (or past the end when searching up).
            QTextCursor wrap = m_editor->textCursor();
            wrap.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
            found = m_editor->document()->find(needle, wrap, flags);
        }
        if (!found.isNull())
            m_editor->setTextCursor(found);
    }

    void replaceCurrent()
    {
        const QString needle = m_findInput->text();
        if (needle.isEmpty())
            return;
        QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection() && cursor.selectedText().compare(needle, Qt::CaseInsensitive) == 0)
            cursor.insertText(m_replaceInput->text());
        findNext(false);
    }

    void replaceAll()
    {
        const QString needle = m_findInput->text();
        if (needle.isEmpty())
            return;
        const QString replacement = m_replaceInput->text();
        QTextCursor cursor(m_editor->document());
        cursor.movePosition(QTextCursor::Start);
        while (!(cursor = m_editor->document()->find(needle, cursor)).isNull()) {
            cursor.insertText(replacement);
        }
        countMatches(); // refresh the label through the callback
    }

private:
    CodeEditor *m_editor = nullptr;
    QLineEdit *m_findInput = nullptr;
    QLineEdit *m_replaceInput = nullptr;
    QLabel *m_countLabel = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QPushButton *m_replaceButton = nullptr;
    QPushButton *m_replaceAllButton = nullptr;
    std::function<void(int)> m_countCallback;
};

// ---------------------------------------------------------------------------

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

QPlainTextEdit *SqlEditor::editorWidget() const
{
    return m_editor;
}

SqlEditor::SqlEditor(QWidget *parent)
    : QWidget(parent)
{
    m_editor = new CodeEditor(this);
    m_editor->setFrameShape(QFrame::NoFrame);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(qMax(font.pointSize(), 12));
    m_editor->setFont(font);
    m_editor->setTabStopDistance(4 * m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    m_editor->installEventFilter(this);

    m_highlighter = new SqlHighlighter(m_editor->document());

    m_findBar = new FindBar(m_editor, this);
    m_findBar->hide();
    m_findBar->setCountCallback([this](int count) { updateMatchCount(count); });
    // Esc closes the bar; scoped so it only fires while the bar has focus.
    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_findBar);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this] { closeFindBar(); });
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this] { openFindBar(); });
    auto *findAgainShortcut = new QShortcut(QKeySequence::FindNext, m_findBar);
    findAgainShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findAgainShortcut, &QShortcut::activated, this, [this] { m_findBar->findNext(false); });
    auto *findPrevShortcut = new QShortcut(QKeySequence::FindPrevious, m_findBar);
    findPrevShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findPrevShortcut, &QShortcut::activated, this, [this] { m_findBar->findNext(true); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_findBar);
    layout->addWidget(m_editor, 1);

    connect(m_findBar->findInput(), &QLineEdit::textChanged, this,
            [this](const QString &) { m_findBar->countMatches(); });

    retranslateUi();
}

void SqlEditor::openFindBar()
{
    m_findBar->focusFind();
    updateMatchCount(m_findBar->countMatches());
}

void SqlEditor::closeFindBar()
{
    m_findBar->hide();
    m_editor->setFocus();
}

void SqlEditor::updateMatchCount(int count)
{
    if (count <= 0)
        m_findBar->setCountText(tr("No matches"));
    else
        m_findBar->setCountText(tr("%1 matches").arg(count));
}

void SqlEditor::retranslateUi()
{
    m_editor->setPlaceholderText(
        tr("-- Ctrl+Enter runs the SQL, e.g.:\nSELECT * FROM range(10);"));
    m_findBar->retranslate(tr("Find"), tr("Replace with"), tr("Previous match"),
                           tr("Next match"), tr("Replace"), tr("Replace All"), tr("Close"));
}

void SqlEditor::retheme()
{
    m_highlighter->retheme();
    m_editor->retheme();
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
    if (!cursor.hasSelection())
        return QString();
    // QTextCursor::selectedText separates lines with U+2029, not '\n'.
    QString text = cursor.selectedText();
    text.replace(QChar(0x2029), QLatin1Char('\n'));
    return text;
}

} // namespace ducq
