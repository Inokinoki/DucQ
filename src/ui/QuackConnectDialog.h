#pragma once

#ifdef DUCQ_QUACK

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ducq {

// Asks for a Quack server address ("host[:port]"), an optional token and the
// alias the attached remote database gets. Only compiled into DUCQ_QUACK
// builds; the File menu action is hidden everywhere else.
//
// The dialog does not close on Connect: connecting is a network operation the
// owner runs asynchronously. It emits connectRequested() after local
// validation, switches to its in-progress state, and is driven from there —
// accept() (by the owner) on success, showError() on failure, which restores
// the fields so the input survives for a retry.
class QuackConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit QuackConnectDialog(QWidget *parent = nullptr);

    // Prefills the server combo, most-recent-first.
    void setRecentTargets(const QStringList &targets);

    // "host[:port]", without the quack: scheme; a pasted "quack://…" prefix
    // is stripped.
    QString server() const;
    QString token() const;
    QString alias() const;

    // Disables the fields and the Connect button so one attempt cannot be
    // started twice; Cancel stays enabled as the way out.
    void setConnecting(bool connecting);
    // Leaves the in-progress state and shows a failed attempt's error text.
    void showError(const QString &message);

signals:
    // Field values after local validation passed.
    void connectRequested(const QString &server, const QString &token, const QString &alias);

private:
    void retranslateUi();

    QComboBox *m_server = nullptr;
    QLineEdit *m_token = nullptr;
    QLineEdit *m_alias = nullptr;
    QFormLayout *m_form = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLabel *m_errorLabel = nullptr;
    bool m_connecting = false; // retranslate keeps the right button text
};

} // namespace ducq

#endif // DUCQ_QUACK
