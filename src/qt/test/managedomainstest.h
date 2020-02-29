#ifndef BITCOIN_QT_TEST_MANAGEDOMAINSTESTS_H
#define BITCOIN_QT_TEST_MANAGEDOMAINSTESTS_H

#include <QObject>
#include <QTest>

class ManageDomainsTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void manageDomainsTests();
};

#endif // BITCOIN_QT_TEST_MANAGEDOMAINSTESTS_H
 5  src/qt/test/test_main.cpp 