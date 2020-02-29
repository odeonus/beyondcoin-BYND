#ifndef CONFIGUREDOMAINDIALOG_H
#define CONFIGUREDOMAINDIALOG_H

#include "platformstyle.h"

#include <QDialog>

namespace Ui {
    class ConfigureDomainDialog;
}

class WalletModel;

/** Dialog for editing an address and associated information.
 */
class ConfigureDomainDialog : public QDialog
{
    Q_OBJECT

public:

    explicit ConfigureDomainDialog(const PlatformStyle *platformStyle,
                                 const QString &_domain, const QString &data,
                                 bool _firstUpdate, QWidget *parent = nullptr);
    ~ConfigureDomainDialog();

    void setModel(WalletModel *walletModel);
    const QString &getReturnData() const { return returnData; }
    const QString &getTransferTo() const { return returnTransferTo; }

public Q_SLOTS:
    void accept() override;
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();

private:
    Ui::ConfigureDomainDialog *ui;
    const PlatformStyle *platformStyle;
    QString returnData;
    QString returnTransferTo;
    WalletModel *walletModel;
    const QString domain;
    const bool firstUpdate;
};

#endif // CONFIGUREDOMAINDIALOG_H