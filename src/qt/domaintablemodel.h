#ifndef DOMAINTABLEMODEL_H
#define DOMAINTABLEMODEL_H

#include "bitcoinunits.h"

#include <QAbstractTableModel>
#include <QStringList>

#include <memory>

class PlatformStyle;
class DomainTablePriv;
class CWallet;
class WalletModel;

/**
   Qt model for "Manage Domains" page.
 */
class DomainTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit DomainTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent=nullptr);
    virtual ~DomainTableModel();

    enum ColumnIndex {
        Domain = 0,
        Value = 1,
        ExpiresIn = 2
    };

    /** @name Methods overridden from QAbstractTableModel
        @{*/
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
    /*@}*/

private:
    CWallet *wallet;
    WalletModel *walletModel;
    QStringList columns;
    std::unique_ptr<DomainTablePriv> priv;
    const PlatformStyle *platformStyle;
    int cachedNumBlocks;

    /** Notify listeners that data changed. */
    void emitDataChanged(int index);

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

public Q_SLOTS:
    void updateEntry(const QString &domain, const QString &value, int nHeight, int status, int *outNewRowIndex=nullptr);
    void updateExpiration();
    void updateTransaction(const QString &hash, int status);

    friend class DomainTablePriv;
};

struct DomainTableEntry
{
    QString domain;
    QString value;
    int nHeight;

    static const int DOMAIN_NEW = -1;             // Dummy nHeight value for not-yet-created domain
    static const int DOMAIN_NON_EXISTING = -2;    // Dummy nHeight value for unitinialized entries
    static const int DOMAIN_UNCONFIRMED = -3;     // Dummy nHeight value for unconfirmed domain transactions

    // NOTE: making this const throws warning indicating it will not be const
    bool HeightValid() { return nHeight >= 0; }
    static bool CompareHeight(int nOldHeight, int nNewHeight);    // Returns true if new height is better

    DomainTableEntry() : nHeight(DOMAIN_NON_EXISTING) {}
    DomainTableEntry(const QString &domain, const QString &value, int nHeight):
        domain(domain), value(value), nHeight(nHeight) {}
    DomainTableEntry(const std::string &domain, const std::string &value, int nHeight):
        domain(QString::fromStdString(domain)), value(QString::fromStdString(value)), nHeight(nHeight) {}
};

#endif // DOMAINTABLEMODEL_H