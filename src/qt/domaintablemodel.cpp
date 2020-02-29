#include "domaintablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"
#include "guiconstants.h"
#include "wallet/wallet.h"
#include "ui_interface.h"
#include "platformstyle.h"

#include "domains/common.h"
#include "util.h"
#include "protocol.h"
#include "rpc/server.h"
#include "validation.h" // cs_main

#include <univalue.h>

#include <QDebug>
#include <QObject>
#include <QTimer>

// ExpiresIn column is right-aligned as it contains numbers
namespace {
    int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter,     // Domain
        Qt::AlignLeft|Qt::AlignVCenter,     // Value
        Qt::AlignRight|Qt::AlignVCenter     // Expires in
    };
}

struct NameTableEntryLessThan
{
    bool operator()(const NameTableEntry &a, const NameTableEntry &b) const
    {
        return a.domain < b.domain;
    }
    bool operator()(const NameTableEntry &a, const QString &b) const
    {
        return a.domain < b;
    }
    bool operator()(const QString &a, const NameTableEntry &b) const
    {
        return a < b.domain;
    }
};

// Returns true if new height is better
bool NameTableEntry::CompareHeight(int nOldHeight, int nNewHeight)
{
    if(nOldHeight == DOMAIN_NON_EXISTING)
        return true;

    // We use optimistic way, assuming that unconfirmed transaction will eventually become confirmed,
    // so we update the domain in the table immediately. Ideally we need a separate way of displaying
    // unconfirmed domains (e.g. grayed out)
    if(nNewHeight == DOMAIN_UNCONFIRMED)
        return true;

    // Here we rely on the fact that dummy height values are always negative
    return nNewHeight > nOldHeight;
}

// Private implementation
class NameTablePriv
{
public:
    CWallet *wallet;
    QList<NameTableEntry> cachedNameTable;
    DomainTableModel *parent;

    NameTablePriv(CWallet *wallet, DomainTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshNameTable()
    {
        qDebug() << "DomainTableModel::refreshNameTable";
        cachedNameTable.clear();
        std::map< std::string, NameTableEntry > vNamesO;

        // unconfirmed (domain_pending) domains
        JSONRPCRequest domainPendingRequest;
        domainPendingRequest.strMethod = "domain_pending";
        domainPendingRequest.params = NullUniValue;
        domainPendingRequest.fHelp = false;
        UniValue pendingNames;

        try {
            pendingNames = tableRPC.execute(domainPendingRequest);
        } catch (const UniValue& e) {
            // although we shouldn't typically encounter error here, we
            // should continue and try to add confirmed domains and
            // pending domains. show error to user in case something
            // actually went wrong so they can potentially recover
            UniValue message = find_value( e, "message");
            LogPrintf ("domain_pending lookup error: %s\n", message.get_str().c_str());
        }

        if(pendingNames.isArray())
        {
            for (const auto& v : pendingNames.getValues())
            {
                std::string domain = find_value ( v, "domain").get_str();
                std::string data = find_value ( v, "value").get_str();
                vNamesO[domain] = NameTableEntry(domain, data, NameTableEntry::DOMAIN_UNCONFIRMED);
                LogPrintf("found pending domain: domain=%s\n", domain.c_str());
            }
        }

        // confirmed domains (domain_list)
        JSONRPCRequest domainListRequest;
        domainListRequest.strMethod = "domain_list";
        domainListRequest.params = NullUniValue;
        domainListRequest.fHelp = false;
        UniValue confirmedNames;

        try {
            confirmedNames = tableRPC.execute(domainListRequest);
        } catch (const UniValue& e) {
            // NOTE: see note for catch for above domain_pending call, the
            // same reasoning applies for continuing here
            UniValue message = find_value( e, "message");
            LogPrintf ("domain_list lookup error: %s\n", message.get_str().c_str());
        }

        // will be an object if domain_list command isn't available/other error
        if(confirmedNames.isArray())
        {
            for (const auto& v : confirmedNames.getValues())
            {
                //const UniValue& v = confirmedNames[idx];
                std::string domain = find_value ( v, "domain").get_str();
                std::string data = find_value ( v, "value").get_str();
                int height = find_value ( v, "height").get_int();
                vNamesO[name] = NameTableEntry(name, data, height);
                LogPrintf("found confirmed domain: name=%s height=%i\n", name.c_str(), height);
            }
        }

        // Add existing names
        for (const auto& item : vNamesO)
            cachedNameTable.append(item.second);

        // Add pending names (name_new)
        LOCK(wallet->cs_wallet);
        for (const auto& item : wallet->namePendingMap)
        {
            CNamePendingData npd = CNamePendingData(item.second);
            cachedNameTable.append(
                NameTableEntry(item.first,
                               npd.getData(),
                               NameTableEntry::DOMAIN_NEW));
        }

        // qLowerBound() and qUpperBound() require our cachedNameTable list to be sorted in asc order
        qSort(cachedNameTable.begin(), cachedNameTable.end(), NameTableEntryLessThan());
    }

    bool findInModel(const QString &name, int *lowerIndex=nullptr, int *upperIndex=nullptr)
    {
        // Find name in model
        QList<NameTableEntry>::iterator lower = qLowerBound(
            cachedNameTable.begin(), cachedNameTable.end(), name, NameTableEntryLessThan());
        QList<NameTableEntry>::iterator upper = qUpperBound(
            cachedNameTable.begin(), cachedNameTable.end(), name, NameTableEntryLessThan());
        if (lowerIndex)
            *lowerIndex = (lower - cachedNameTable.begin());
        if (upperIndex)
            *upperIndex = (upper - cachedNameTable.begin());
        return lower != upper;
    }

    void refreshName(const valtype &inName)
    {
        LOCK(cs_main);

        NameTableEntry nameObj(ValtypeToString(inName), "", NameTableEntry::DOMAIN_NON_EXISTING);
        std::string strName = ValtypeToString(inName);

        UniValue params (UniValue::VOBJ);
        params.pushKV ("domain", strName);

        JSONRPCRequest jsonRequest;
        jsonRequest.strMethod = "domain_show";
        jsonRequest.params = params;
        jsonRequest.fHelp = false;

        UniValue res;
        try {
            res = tableRPC.execute(jsonRequest);
        } catch (const UniValue& e) {
            UniValue message = find_value(e, "message");
            std::string errorStr = message.get_str();
            LogPrintf ("unexpected domain_show response on refreshName=%s: %s\n",
                    strName.c_str(), errorStr.c_str());
            return;
        }

        UniValue heightResult = find_value(res, "height");
        if (!heightResult.isNum())
        {
            LogPrintf ("No height for domain %s\n", strName.c_str());
            return;
        }
        const int height = heightResult.get_int();

        UniValue valResult = find_value(res, "value");
        if (!valResult.isStr())
        {
            LogPrintf ("No value for domain %s\n", strName.c_str());
            return;
        }

        std::string data = valResult.get_str();

        nameObj = NameTableEntry(strName, data, height);

        if(findInModel(domainObj.domain))
        {
            // In model - update or delete
            if(nameObj.nHeight != NameTableEntry::DOMAIN_NON_EXISTING)
            {
                LogPrintf ("refreshName result : %s - refreshed in the table\n", qPrintable(nameObj.domain));
                updateEntry(domainObj.name, domainObj.value, nameObj.nHeight, CT_UPDATED);
            }
            else
            {
                LogPrintf("refreshName result : %s - deleted from the table\n", qPrintable(nameObj.domain));
                updateEntry(nameObj.name, nameObj.value, nameObj.nHeight, CT_DELETED);
            }
        }
        else
        {
            // Not in model - add or do nothing
            if(nameObj.nHeight != NameTableEntry::DOMAIN_NON_EXISTING)
            {
                LogPrintf("refreshName result : %s - added to the table\n", qPrintable(nameObj.domain));
                updateEntry(nameObj.name, nameObj.value, nameObj.nHeight, CT_NEW);
            }
            else
            {
                LogPrintf("refreshName result : %s - ignored (not in the table)\n", qPrintable(nameObj.domain));
            }
        }
    }

    void updateEntry(const QString &domain, const QString &value, int nHeight, int status, int *outNewRowIndex=nullptr)
    {
        int lowerIndex, upperIndex;
        bool inModel = findInModel(domain, &lowerIndex, &upperIndex);
        QList<NameTableEntry>::iterator lower = (cachedNameTable.begin() + lowerIndex);
        QList<NameTableEntry>::iterator upper = (cachedNameTable.begin() + upperIndex);

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                if(outNewRowIndex)
                {
                    *outNewRowIndex = parent->index(lowerIndex, 0).row();
                    // HACK: ManageNamesPage uses this to ensure updating and get selected row,
                    // so we do not write warning into the log in this case
                }
                else {
                    LogPrintf ("Warning: NameTablePriv::updateEntry: Got CT_NEW, but entry is already in model\n");
                }
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedNameTable.insert(lowerIndex, NameTableEntry(domain, value, nHeight));
            parent->endInsertRows();
            if(outNewRowIndex)
                *outNewRowIndex = parent->index(lowerIndex, 0).row();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                LogPrintf ("Warning: NameTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->domain = domain;
            lower->value = value;
            lower->nHeight = nHeight;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                LogPrintf ("Warning: NameTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex - 1);
            cachedNameTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedNameTable.size();
    }

    NameTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedNameTable.size())
        {
            return &cachedNameTable[idx];
        }
        else
        {
            return nullptr;
        }
    }
};

DomainTableModel::DomainTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent):
        QAbstractTableModel(parent),
        wallet(wallet),
        walletModel(parent),
        priv(new NameTablePriv(wallet, this)),
        platformStyle(platformStyle)
{
    columns << tr("Name") << tr("Value") << tr("Expires in");
    priv->refreshNameTable();

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateExpiration()));
    timer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

DomainTableModel::~DomainTableModel()
{
    unsubscribeFromCoreSignals();
}

void DomainTableModel::updateExpiration()
{
    int nBestHeight = chainActive.Height();
    if(nBestHeight != cachedNumBlocks)
    {
        LOCK(cs_main);

        cachedNumBlocks = nBestHeight;
        std::vector<NameTableEntry*> expired;
        // Blocks came in since last poll.
        // Delete expired domains
        for (int i = 0, n = priv->size(); i < n; i++)
        {
            NameTableEntry *item = priv->index(i);
            if(!item->HeightValid())
                continue; // Currently, unconfirmed domains do not expire in the table

            const Consensus::Params& params = Params().GetConsensus();
            int nHeight = item->nHeight;
            int expirationDepth = params.rules->NameExpirationDepth(nHeight);

            if(nHeight + expirationDepth <= nBestHeight)
            {
                expired.push_back(item);
            }

        }

        // process all expirations in bulk (don't mutate table while iterating
        for (NameTableEntry *item : expired)
            priv->updateEntry(item->domain, item->value, item->nHeight, CT_DELETED);

        // Invalidate expiration counter for all rows.
        // Qt is smart enough to only actually request the data for the
        // visible rows.
        //emit
        dataChanged(index(0, ExpiresIn), index(priv->size()-1, ExpiresIn));
    }
}

void DomainTableModel::updateTransaction(const QString &hash, int status)
{
    uint256 hash256;
    std::string strHash = hash.toStdString();
    hash256.SetHex(strHash);

    LOCK(wallet->cs_wallet);
    // Find transaction in wallet
    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash256);
    if(mi == wallet->mapWallet.end())
    {
        LogPrintf ("tx %s has no domain in wallet\n", strHash);
        return;
    }
    CTransaction tx = mi->second;

    const auto &vout = tx.vout;
    for (const auto it : vout)
    {
        if(!CNameScript::isNameScript(it.scriptPubKey))
        {
            continue;
        }

        CNameScript domainScript(it.scriptPubKey);
        switch (domainScript.getNameOp())
        {
            case OP_DOMAIN_NEW:
                break;

            case OP_DOMAIN_FIRSTUPDATE:
            case OP_DOMAIN_UPDATE:
                priv->refreshName(domainScript.getOpName());
                break;

            default:
                assert (false);
        }
    }

}

int DomainTableModel::rowCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int DomainTableModel::columnCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant DomainTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    NameTableEntry *rec = static_cast<NameTableEntry*>(index.internalPointer());

    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Name:
            return rec->domain;
        case Value:
            return rec->value;
        case ExpiresIn:
            if(!rec->HeightValid()) {
                return QVariant();
            }
            int nBestHeight = chainActive.Height();
            const Consensus::Params& params = Params().GetConsensus();
            return rec->nHeight + params.rules->NameExpirationDepth(rec->nHeight) - nBestHeight;
        }
    }
    return QVariant();
}

QVariant DomainTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation != Qt::Horizontal)
        return QVariant();

    if(role == Qt::DisplayRole)
        return columns[section];

    if(role == Qt::TextAlignmentRole)
        return column_alignments[section];

    if(role == Qt::ToolTipRole)
    {
        switch(section)
        {
        case Name:
            return tr("Name registered using Namecoin.");
        case Value:
            return tr("Data associated with the name.");
        case ExpiresIn:
            return tr("Number of blocks, after which the name will expire. Update name to renew it.\nEmpty cell means pending(awaiting automatic name_firstupdate or awaiting network confirmation).");
        }
    }
    return QVariant();
}

Qt::ItemFlags DomainTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;

    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

QModelIndex DomainTableModel::index(int row, int column, const QModelIndex &parent /* = QModelIndex()*/) const
{
    Q_UNUSED(parent);
    if(priv->index(row))
        return createIndex(row, column, priv->index(row));

    return QModelIndex();
}

// queue notifications to show a non freezing progress dialog e.g. for rescan
struct TransactionNotification
{
public:
    TransactionNotification() {}
    TransactionNotification(const uint256 hash, const ChangeType status, const bool showTransaction):
        hash(hash), status(status), showTransaction(showTransaction) {}

    void invoke(DomainTableModel *ntm)
    {
        QString strHash = QString::fromStdString(hash.GetHex());
        QMetaObject::invokeMethod(ntm, "updateTransaction", Qt::QueuedConnection,
                                  Q_ARG(QString, strHash),
                                  Q_ARG(int, status));
    }
private:
    const uint256 hash;
    ChangeType status;
    bool showTransaction;
};

static bool fQueueNotifications = false;
static std::vector< TransactionNotification > vQueueNotifications;

static void NotifyTransactionChanged(DomainTableModel *ntm, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    // Find transaction in wallet
    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash);
    bool inWallet = mi != wallet->mapWallet.end();

    TransactionNotification notification(hash, status, inWallet);

    if(fQueueNotifications)
    {
        vQueueNotifications.push_back(notification);
        return;
    }
    notification.invoke(ntm);
}


void
DomainTableModel::updateEntry(const QString &name, const QString &value,
                            int nHeight, int status, int *outNewRowIndex /*= NULL*/)
{
    priv->updateEntry(name, value, nHeight, status, outNewRowIndex);
}

void
DomainTableModel::emitDataChanged(int idx)
{
    //emit
    dataChanged(index(idx, 0), index(idx, columns.length()-1));
}

void
DomainTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    // wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
}

void
DomainTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    // wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
}