#pragma once

#ifdef DUCQ_QUACK

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;

namespace ducq {

// Asks for a Quack server address ("host[:port]"), an optional token and the
// alias the attached remote database gets. Only compiled into DUCQ_QUACK
// builds; the File menu action is hidden everywhere else.
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

private:
    void retranslateUi();

    QComboBox *m_server = nullptr;
    QLineEdit *m_token = nullptr;
    QLineEdit *m_alias = nullptr;
    QFormLayout *m_form = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace ducq

#endif // DUCQ_QUACK
