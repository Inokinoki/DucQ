#include "QuackConnectDialog.h"

#ifdef DUCQ_QUACK

#include "Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPushButton>
#include <QVBoxLayout>

namespace ducq {

namespace {
// The alias becomes the attached database's name in SQL, so it must be a
// plain identifier.
const QRegularExpression kAliasPattern { QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$") };
constexpr char kDefaultAlias[] = "remote";
} // namespace

QuackConnectDialog::QuackConnectDialog(QWidget *parent)
    : QDialog(parent)
{
    auto *layout = new QVBoxLayout(this);

    m_form = new QFormLayout();
    // Rows are created with a placeholder label text (a truly empty QString
    // would create no QLabel at all); retranslateUi() fills in the real,
    // translated labels via labelForField().
    m_server = new QComboBox(this);
    m_server->setEditable(true);
    m_server->setInsertPolicy(QComboBox::NoInsert);
    m_form->addRow(QStringLiteral(" "), m_server);
    m_token = new QLineEdit(this);
    m_token->setEchoMode(QLineEdit::Password);
    m_form->addRow(QStringLiteral(" "), m_token);
    m_alias = new QLineEdit(this);
    m_alias->setText(QLatin1String(kDefaultAlias));
    m_form->addRow(QStringLiteral(" "), m_alias);
    layout->addLayout(m_form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::tokens().error.name()));
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    m_buttons = new QDialogButtonBox(this);
    m_buttons->addButton(QDialogButtonBox::Cancel);
    m_connectButton = m_buttons->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    // Not QDialogButtonBox::accepted: that would also fire when the owner
    // calls accept() to close a successful connect, re-entering the handler.
    // The button emits connectRequested instead; the owner drives accept(),
    // setConnecting() and showError() from the attempt's real outcome.
    connect(m_connectButton, &QPushButton::clicked, this, [this] {
        // Validate locally first: on bad input the dialog stays open (and
        // filled) instead of starting an attempt that cannot work.
        if (server().isEmpty()) {
            QMessageBox::warning(this, windowTitle(), tr("Enter a server address, e.g. localhost:9494."));
            return;
        }
        if (!kAliasPattern.match(alias()).hasMatch()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("The alias must be a plain identifier (letters, digits, underscore)."));
            return;
        }
        emit connectRequested(server(), token(), alias());
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
    if (m_connectButton)
        m_connectButton->setDefault(true);
    m_server->setFocus();

    retranslateUi();
}

void QuackConnectDialog::setConnecting(bool connecting)
{
    m_connecting = connecting;
    m_server->setEnabled(!connecting);
    m_token->setEnabled(!connecting);
    m_alias->setEnabled(!connecting);
    m_connectButton->setEnabled(!connecting);
    m_connectButton->setText(connecting ? tr("Connecting…") : tr("Connect"));
    if (connecting)
        m_errorLabel->setVisible(false);
}

void QuackConnectDialog::showError(const QString &message)
{
    setConnecting(false);
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);
    // The label just grew by a few wrapped lines; re-fit the dialog so the
    // full message is visible instead of clipping it at the bottom.
    adjustSize();
}

void QuackConnectDialog::setRecentTargets(const QStringList &targets)
{
    m_server->clear();
    m_server->addItems(targets);
    m_server->setCurrentIndex(-1);
}

QString QuackConnectDialog::server() const
{
    QString text = m_server->currentText().trimmed();
    if (text.startsWith(QLatin1String("quack://")))
        text.remove(0, 8);
    else if (text.startsWith(QLatin1String("quack:")))
        text.remove(0, 6);
    return text.trimmed();
}

QString QuackConnectDialog::token() const
{
    return m_token->text();
}

QString QuackConnectDialog::alias() const
{
    return m_alias->text().trimmed();
}

void QuackConnectDialog::retranslateUi()
{
    setWindowTitle(tr("Connect to DuckDB Server"));
    const auto setLabel = [this](QWidget *field, const QString &text) {
        if (auto *label = qobject_cast<QLabel *>(m_form->labelForField(field)))
            label->setText(text);
    };
    setLabel(m_server, tr("&Server address:"));
    setLabel(m_token, tr("&Token (optional):"));
    setLabel(m_alias, tr("&Alias:"));
    m_server->lineEdit()->setPlaceholderText(QStringLiteral("localhost:9494"));
    m_token->setPlaceholderText(tr("Leave empty if the server has no token"));
    m_alias->setToolTip(tr("Name the remote database gets after ATTACH"));
    m_connectButton->setText(m_connecting ? tr("Connecting…") : tr("Connect"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
}

} // namespace ducq

#endif // DUCQ_QUACK
