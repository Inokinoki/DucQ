#include "QuackServeDialog.h"

#ifdef DUCQ_QUACK

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace ducq {

namespace {
// 128 random bits, hex encoded — well above the server's 4-character
// minimum and comfortable to copy by hand.
QString generateToken()
{
    QString hex;
    for (int i = 0; i < 4; ++i)
        hex += QString::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, u'0');
    return hex;
}
} // namespace

QuackServeDialog::QuackServeDialog(const QString &databaseLabel, const QList<QuackServerEntry> &servers,
                                   StartFn start, StopFn stop, ListFn list, QWidget *parent)
    : QDialog(parent)
    , m_start(std::move(start))
    , m_stop(std::move(stop))
    , m_list(std::move(list))
    , m_databaseName(databaseLabel)
{
    auto *layout = new QVBoxLayout(this);

    m_databaseLabel = new QLabel(this);
    layout->addWidget(m_databaseLabel);

    // Placeholder labels (a truly empty QString would create no QLabel);
    // retranslateUi() fills in the real, translated ones via labelForField().
    m_form = new QFormLayout();
    m_port = new QSpinBox(this);
    m_port->setRange(1024, 65535);
    m_port->setValue(9494);
    m_form->addRow(QStringLiteral(" "), m_port);
    m_token = new QLineEdit(this);
    // Prefilled so the safe path (token-authenticated server) is the default;
    // cleared by hand it still means "no token", which quack_serve rejects.
    m_token->setText(generateToken());
    m_form->addRow(QStringLiteral(" "), m_token);
    layout->addLayout(m_form);

    m_startButton = new QPushButton(this);
    connect(m_startButton, &QPushButton::clicked, this, [this] {
        const QString token = m_token->text().trimmed();
        // Mirror the server's own minimum (QuackServer::ValidateToken):
        // catching it here keeps the dialog open instead of surfacing a raw
        // engine error after the fact.
        if (!token.isEmpty() && token.size() < 4) {
            QMessageBox::warning(this, windowTitle(), tr("The token must be at least 4 characters long."));
            return;
        }
        const QString error = m_start(m_port->value(), token);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        refreshServers();
    });
    layout->addWidget(m_startButton);
    m_port->setFocus();

    m_serversHeader = new QLabel(this);
    layout->addWidget(m_serversHeader);
    m_servers = new QListWidget(this);
    layout->addWidget(m_servers, 1);
    m_emptyLabel = new QLabel(this);
    layout->addWidget(m_emptyLabel);

    m_buttons = new QDialogButtonBox(this);
    m_buttons->addButton(QDialogButtonBox::Close);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    refreshServers();
    retranslateUi();
}

QWidget *QuackServeDialog::serverRow(const QuackServerEntry &entry)
{
    auto *row = new QWidget(m_servers);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(4, 2, 4, 2);

    auto *uriLabel = new QLabel(entry.uri, row);
    uriLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rowLayout->addWidget(uriLabel);

    auto *tokenLine = new QHBoxLayout();
    auto *tokenLabel = new QLabel(tr("token: %1").arg(entry.token), row);
    tokenLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tokenLine->addWidget(tokenLabel, 1);

    auto *copyButton = new QToolButton(row);
    copyButton->setText(QStringLiteral("⧉"));
    copyButton->setToolTip(tr("Copy token"));
    connect(copyButton, &QToolButton::clicked, row, [entry] {
        QGuiApplication::clipboard()->setText(entry.token);
    });
    tokenLine->addWidget(copyButton);

    // The full statement a client needs, ready to paste into any DuckDB —
    // saves assembling host, port and token by hand on the other side.
    const QString attachSql = QStringLiteral("ATTACH '%1' AS remote (TOKEN '%2');")
                                  .arg(entry.uri, QString(entry.token).replace(u'\'', QStringLiteral("''")));
    auto *attachButton = new QToolButton(row);
    attachButton->setText(QStringLiteral("SQL"));
    attachButton->setToolTip(tr("Copy ATTACH statement"));
    connect(attachButton, &QToolButton::clicked, row, [attachSql] {
        QGuiApplication::clipboard()->setText(attachSql);
    });
    tokenLine->addWidget(attachButton);

    auto *stopButton = new QToolButton(row);
    stopButton->setText(QStringLiteral("⏻"));
    stopButton->setToolTip(tr("Stop server"));
    connect(stopButton, &QToolButton::clicked, this, [this, entry] {
        const QString error = m_stop(entry.uri);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        refreshServers();
    });
    tokenLine->addWidget(stopButton);

    rowLayout->addLayout(tokenLine);
    return row;
}

void QuackServeDialog::refreshServers()
{
    const QList<QuackServerEntry> servers = m_list();
    m_servers->clear(); // item widgets die with their items
    for (const QuackServerEntry &entry : servers) {
        auto *item = new QListWidgetItem;
        QWidget *row = serverRow(entry);
        item->setSizeHint(row->sizeHint());
        m_servers->addItem(item);
        m_servers->setItemWidget(item, row);
    }
    const bool empty = servers.isEmpty();
    m_servers->setVisible(!empty);
    m_serversHeader->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}

void QuackServeDialog::retranslateUi()
{
    setWindowTitle(tr("Serve Database over Quack"));
    m_databaseLabel->setText(tr("Sharing %1").arg(m_databaseName));
    const auto setLabel = [this](QWidget *field, const QString &text) {
        if (auto *label = qobject_cast<QLabel *>(m_form->labelForField(field)))
            label->setText(text);
    };
    setLabel(m_port, tr("&Port:"));
    setLabel(m_token, tr("&Token:"));
    m_port->setToolTip(
        tr("Binds to localhost only; clients on this machine attach via quack:localhost:<port>"));
    m_token->setToolTip(tr("Clients must present this token to attach"));
    m_startButton->setText(tr("Start Server"));
    m_serversHeader->setText(tr("Running servers"));
    m_emptyLabel->setText(tr("No servers are running."));
    for (QAbstractButton *button : m_buttons->buttons())
        if (m_buttons->buttonRole(button) == QDialogButtonBox::RejectRole)
            button->setText(tr("Close"));
}

} // namespace ducq

#endif // DUCQ_QUACK
