#include "managedomainstests.h"

#include "qt/bitcoinamountfield.h"
#include "qt/callback.h"
#include "qt/configuredomaindialog.h"
#include "qt/managedomainspage.h"
#include "qt/domaintablemodel.h"
#include "qt/optionsmodel.h"
#include "qt/overviewpage.h"
#include "qt/platformstyle.h"
#include "qt/qvalidatedlineedit.h"
#include "qt/receivecoinsdialog.h"
#include "qt/receiverequestdialog.h"
#include "qt/recentrequeststablemodel.h"
#include "qt/sendcoinsdialog.h"
#include "qt/sendcoinsentry.h"
#include "qt/transactiontablemodel.h"
#include "qt/transactionview.h"
#include "qt/walletmodel.h"
#include "rpc/server.h"
#include "test/test_bitcoin.h"
#include "validation.h"
#include "wallet/test/wallet_test_fixture.h"
#include "wallet/wallet.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>


// NOTE: This gets created by the WalletTestingSetup class
// from the regular bitcoin wallet tests
extern CWallet* pwalletMain;

namespace
{

//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmMsgBox(QString* text = nullptr, bool cancel = false)
{
    qDebug("setting singleShot callback ConfirmMsgBox");
    QTimer::singleShot(0, makeCallback([text, cancel](Callback* callback) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (!widget->inherits("QMessageBox")) continue;
            QMessageBox * box = qobject_cast<QMessageBox*>(widget);
            if (text) *text = box->text();
            box->button(cancel ? QMessageBox::Cancel : QMessageBox::Yes)->click();
        }
        delete callback;
    }), SLOT(call()));
}

//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfDomainsDialog(const QString &data, bool cancel = false)
{
    qDebug("setting singleShot callback ConfDomainsDialog");
    QTimer::singleShot(1000, makeCallback([data, cancel](Callback* callback) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (!widget->inherits("ConfigureDomainDialog")) continue;
            ConfigureDomainDialog * dlg = qobject_cast<ConfigureDomainDialog*>(widget);
            QLineEdit* dataEdit = dlg->findChild<QLineEdit*>("dataEdit");
            dataEdit->setText(data);
            dlg->accept(); //button(QMessageBox::OK)->click();
        }
        delete callback;
    }), SLOT(call()));
}

void GenerateCoins(int nblocks)
{
    UniValue params (UniValue::VOBJ);
    params.pushKV ("nblocks", nblocks);

    JSONRPCRequest jsonRequest;
    jsonRequest.strMethod = "generate";
    jsonRequest.params = params;
    jsonRequest.fHelp = false;

    UniValue res = tableRPC.execute(jsonRequest);
}

//! Find index of domain in domains list.
QModelIndex FindTx(const QAbstractItemModel& model, const QString& domain)
{
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, DomainTableModel::Domain) == domain) {
            return index;
        }
    }
    return {};
}


void TestManageDomainsGUI()
{
    // Utilize the normal testsuite setup (we have no fixtures in Qt tests
    // so we have to do it like this).
    WalletTestingSetup testSetup(CBaseChainParams::REGTEST);

    // The Qt/wallet testing manifolds don't appear to instantiate the wallets
    // correctly for multi-wallet bitcoin so this is a hack in place until that
    // happens
    vpwallets.insert(vpwallets.begin(), pwalletMain);

    bool firstRun;
    pwalletMain->LoadWallet(firstRun);

    // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
    GenerateCoins(105);
    CWalletDB(pwalletMain->GetDBHandle()).LoadWallet(pwalletMain);
    RegisterWalletRPCCommands(tableRPC);

    // Create widgets for interacting with the domains UI
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    ManageDomainsPage manageDomainsPage(platformStyle.get());
    OptionsModel optionsModel;
    WalletModel walletModel(platformStyle.get(), pwalletMain, &optionsModel);
    manageDomainsPage.setModel(&walletModel);

    const QString& domain = "test/domain1";
    const QString& data = "{\"key\": \"value\"}";

    // make sure we have no domains
    DomainTableModel* domainTableModel = walletModel.getDomainTableModel();
    QCOMPARE(domainTableModel->rowCount(), 0);

    // register a domain via the UI (register domain_new)
    QValidatedLineEdit* registerDomain = manageDomainsPage.findChild<QValidatedLineEdit*>("registerDomain");
    registerDomain->setText(domain);
    QCOMPARE(registerDomain->text(), domain);

    // queue click on the warning dialog
    ConfirmMsgBox();
    // queue filling out the configure domains dialog with data
    ConfDomainsDialog(data);

    // click the OK button to finalize domain_new & wallet domainPendingData write
    QPushButton* submitDomainButton = manageDomainsPage.findChild<QPushButton*>("submitDomainButton");
    submitDomainButton->click();

    ConfirmMsgBox();
    QEventLoop().processEvents();

    // check domaintablemodel for domain
    {
        QCOMPARE(domainTableModel->rowCount(), 1);
        QModelIndex domainIx = FindTx(*domainTableModel, domain);
        QVERIFY(domainIx.isValid());
    }

    // make sure the expires is blank (pending)
    {
        QModelIndex domainIx = FindTx(*domainTableModel, domain);
        QVERIFY(domainIx.isValid());
        QModelIndex expIx = domainTableModel->index(domainIx.row(), DomainTableModel::ExpiresIn);
        QVERIFY(expIx.isValid());
        QCOMPARE(domainTableModel->data(expIx, 0).toString(),QString(""));
    }

    // make sure data is there
    {
        QModelIndex domainIx = FindTx(*domainTableModel, domain);
        QVERIFY(domainIx.isValid());
        QModelIndex valIx = domainTableModel->index(domainIx.row(), DomainTableModel::Value);
        QVERIFY(valIx.isValid());
        QCOMPARE(domainTableModel->data(valIx, 0).toString(),data);
    }

    // make sure the pending data is in the wallet
    {
      QVERIFY(walletModel.pendingDomainFirstUpdateExists(domain.toStdString()));
    }

    // TODO: need to refactor the DomainTableModel so the slots and emitters
    // for updating expirations etc work properly in the testsuite
    // // confirm both operations
    // {
    //     GenerateCoins(12 * 5);
    //     ConfirmMsgBox();
    //     domainTableModel->updateExpiration();
    //     Q_EMIT(domainTableModel->updateExpiration());
    //     QEventLoop().processEvents();
    //     qDebug("processed events?");

    //     // now make sure expires has a value post-confirm of both ops
    //     QModelIndex domainIx = FindTx(*domainTableModel, domain);
    //     QVERIFY(domainIx.isValid());
    //     QModelIndex expIx = domainTableModel->index(0, DomainTableModel::ExpiresIn);
    //     QVERIFY(expIx.isValid());
    //     QCOMPARE(domainTableModel->data(expIx, DomainTableModel::ExpiresIn).toInt(),31);
    // }
}

}

void ManageDomainsTests::manageDomainsTests()
{
    TestManageDomainsGUI();
}