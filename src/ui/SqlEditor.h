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
    QString toPlainText() const;
    void retranslateUi();

signals:
    void executeRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPlainTextEdit *m_editor = nullptr;
};

} // namespace ducq
