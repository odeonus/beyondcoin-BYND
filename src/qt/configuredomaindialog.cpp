#include "configuredomaindialog.h"
#include "ui_configuredomaindialog.h"

#include "addressbookpage.h"
#include "guiutil.h"
#include "domains/main.h"
#include "platformstyle.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <QMessageBox>
#include <QClipboard>

ConfigureDomainDialog::ConfigureDomainDialog(const PlatformStyle *platformStyle,
                                         const QString &_domain, const QString &data,
                                         bool _firstUpdate, QWidget *parent) :
    QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint),
    ui(new Ui::ConfigureDomainDialog),
    platformStyle(platformStyle),
    domain(_domain),
    firstUpdate(_firstUpdate)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC
    ui->transferToLayout->setSpacing(4);
#endif

    GUIUtil::setupAddressWidget(ui->transferTo, this);

    ui->labelDomain->setText(domain);
    ui->dataEdit->setText(data);

    returnData = data;

    if (domain.startsWith("d/"))
        ui->labelDomain->setText(domain.mid(2) + ".bit");
    else
        ui->labelDomain->setText(tr("(not a domain domain)"));

    if (firstUpdate)
    {
        ui->labelTransferTo->hide();
        ui->labelTransferToHint->hide();
        ui->transferTo->hide();
        ui->addressBookButton->hide();
        ui->pasteButton->hide();
        ui->labelSubmitHint->setText(
            tr("domain_firstupdate transaction will be queued and broadcasted when corresponding domain_new is %1 blocks old")
            .arg(MIN_FIRSTUPDATE_DEPTH));
    }
    else
    {
        ui->labelSubmitHint->setText(tr("domain_update transaction will be issued immediately"));
        setWindowTitle(tr("Update Domain"));
    }
}


ConfigureDomainDialog::~ConfigureDomainDialog()
{
    delete ui;
}

void ConfigureDomainDialog::accept()
{
    if (!walletModel)
        return;

    QString addr;
    if (!firstUpdate)
    {
        if (!ui->transferTo->text().isEmpty() && !ui->transferTo->hasAcceptableInput())
        {
            ui->transferTo->setValid(false);
            return;
        }

        addr = ui->transferTo->text();

        if (addr != "" && !walletModel->validateAddress(addr))
        {
            ui->transferTo->setValid(false);
            return;
        }

    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid())
        return;

    returnData = ui->dataEdit->text();
    if (!firstUpdate)
        returnTransferTo = ui->transferTo->text();

    QDialog::accept();
}

void ConfigureDomainDialog::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
}

void ConfigureDomainDialog::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->transferTo->setText(QApplication::clipboard()->text());
}

void ConfigureDomainDialog::on_addressBookButton_clicked()
{
    if (!walletModel)
        return;

    AddressBookPage dlg(
        // platformStyle
        platformStyle,
        // mode
        AddressBookPage::ForSelection,
        // tab
        AddressBookPage::SendingTab,
        // *parent
        this);
    dlg.setModel(walletModel->getAddressTableModel());
    if (dlg.exec())
        ui->transferTo->setText(dlg.getReturnValue());
}