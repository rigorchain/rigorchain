// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OPTIONSDIALOG_H
#define BITCOIN_QT_OPTIONSDIALOG_H

#include <QDialog>
#include <QValidator>

#include <tor_mode.h>

class ClientModel;
class OptionsModel;
class QValidatedLineEdit;

QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

namespace Ui {
class OptionsDialog;
}

/** Proxy address widget validator, checks for a valid proxy address.
 */
class ProxyAddressValidator : public QValidator
{
    Q_OBJECT

public:
    explicit ProxyAddressValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** Preferences dialog. */
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent, bool enableWallet);
    ~OptionsDialog();

    enum Tab {
        TAB_MAIN,
        TAB_NETWORK,
    };

    void setClientModel(ClientModel* client_model);
    void setModel(OptionsModel *model);
    void setMapper();
    void setCurrentTab(OptionsDialog::Tab tab);

private Q_SLOTS:
    /* set OK button state (enabled / disabled) */
    void setOkButtonState(bool fState);
    void on_resetButton_clicked();
    void on_openBitcoinConfButton_clicked();
    void on_okButton_clicked();
    void on_cancelButton_clicked();

    void on_showTrayIcon_stateChanged(int state);

    void togglePruneWarning(bool enabled);
    void showRestartWarning(bool fPersistent = false);
    void clearStatusLabel();
    void updateProxyValidationState();
    /* query the networks, for which the default proxy is used */
    void updateDefaultProxyNets();
    void updateTorFormState();
    void refreshTorStatus();

Q_SIGNALS:
    void proxyIpChecks(QValidatedLineEdit *pUiProxyIp, uint16_t nProxyPort);
    void quitOnReset();

private:
    void createTorGroup();
    void loadTorForm();

    Ui::OptionsDialog *ui;
    ClientModel* m_client_model{nullptr};
    OptionsModel* model{nullptr};
    QDataWidgetMapper* mapper{nullptr};
    class QComboBox* m_tor_mode{nullptr};
    class QLineEdit* m_tor_socks_host{nullptr};
    class QLineEdit* m_tor_socks_port{nullptr};
    class QLineEdit* m_tor_control_host{nullptr};
    class QLineEdit* m_tor_control_port{nullptr};
    class QLineEdit* m_tor_password{nullptr};
    class QLabel* m_tor_status{nullptr};
    class QLabel* m_onion_address{nullptr};
    class QLabel* m_onion_peers{nullptr};
    class QLabel* m_tor_detail{nullptr};
    class QPushButton* m_copy_onion{nullptr};
    class QTimer* m_tor_timer{nullptr};
    TorNetworkMode m_tor_mode_at_open{TorNetworkMode::Normal};
    bool m_tor_form_dirty{false};
    bool m_tor_loading{false};
    QString m_active_onion;
};

#endif // BITCOIN_QT_OPTIONSDIALOG_H
