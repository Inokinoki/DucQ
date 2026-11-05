#pragma once

#ifdef DUCQ_QUACK

#include <QDialog>
#include <QList>
#include <functional>

class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace ducq {

// A server started from this window for one open session. The server lives
// in that session's database instance, so closing the session takes it down.
struct QuackServerEntry {
    int sessionId;
    QString uri;  // "quack:localhost:9494" — what clients attach to
    QString token;
};

// Starts and stops quack_serve instances for the active session and lists
// the ones this window has started (servers from other sessions — or other
// processes — are intentionally not shown; the owner filters the list).
// The owner supplies the operations; the dialog only collects input.
class QuackServeDialog : public QDialog {
    Q_OBJECT
public:
    // Runs CALL quack_serve on the owner's session; returns the error text,
    // empty on success. The owner records the new server.
    using StartFn = std::function<QString(int port, const QString &token)>;
    // Runs CALL quack_stop and forgets the entry; empty return = success.
    using StopFn = std::function<QString(const QString &uri)>;
    // Servers currently running for the owner's session.
    using ListFn = std::function<QList<QuackServerEntry>()>;

    QuackServeDialog(const QString &databaseLabel, const QList<QuackServerEntry> &servers,
                     StartFn start, StopFn stop, ListFn list, QWidget *parent = nullptr);

private:
    void refreshServers();
    void retranslateUi();
    QWidget *serverRow(const QuackServerEntry &entry);

    QLabel *m_databaseLabel = nullptr;
    QString m_databaseName; // shown in m_databaseLabel; survives retranslateUi
    QFormLayout *m_form = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_token = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_serversHeader = nullptr;
    QListWidget *m_servers = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    StartFn m_start;
    StopFn m_stop;
    ListFn m_list;
};

} // namespace ducq

#endif // DUCQ_QUACK
