// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "domains/common.h"
#include "wallet/wallet.h"
#include "dbwrapper.h"

#include "test/test_bitcoin.h"
#include "wallet/test/wallet_test_fixture.h"

#include <boost/test/unit_test.hpp>
#include <univalue.h>

extern CWallet* pwalletMain;

BOOST_FIXTURE_TEST_SUITE(wallet_domain_pending_tests, WalletTestingSetup)

BOOST_AUTO_TEST_CASE(wallet_domain_pending_tests)
{
    const std::string domain1 = "test/domain1";
    const std::string domain2 = "test/domain2";
    const std::string txid = "9f73e1dfa3cbae23d008307e42e72beb8c010546ea2a7b9ff32619676a9c64a6";
    const std::string rand = "092abbca8a938103abcc";
    const std::string data = "{\"foo\": \"bar\"}";
    const std::string toAddress = "N5e1vXUUL3KfhPyVjQZSes1qQ7eyarDbUU";

    CDomainPendingData domainData;
    domainData.setHex(txid);
    domainData.setRand(rand);
    domainData.setData(data);

    CDomainPendingData domainDataWithAddr;
    domainDataWithAddr.setHex(txid);
    domainDataWithAddr.setRand(rand);
    domainDataWithAddr.setData(data);
    domainDataWithAddr.setToAddress(toAddress);

    CWalletDBWrapper& dbw = pwalletMain->GetDBHandle();

    {
        // ensure pending domains is blank to start
        LOCK(pwalletMain->cs_wallet);
        BOOST_CHECK(pwalletMain->domainPendingMap.size() == 0);
    }

    {
        // write a valid pending domain_update to wallet
        LOCK(pwalletMain->cs_wallet);
        BOOST_CHECK(CWalletDB(dbw).WriteDomainFirstUpdate(domain1, domainData));
        BOOST_CHECK(CWalletDB(dbw).WriteDomainFirstUpdate(domain2, domainDataWithAddr));
    }

    {
        // load the wallet and see if we get our pending domain loaded
        LOCK(pwalletMain->cs_wallet);
        bool fFirstRun;
        BOOST_CHECK_NO_THROW(pwalletMain->LoadWallet(fFirstRun));
    }

    {
        // make sure we've added our pending domain
        LOCK(pwalletMain->cs_wallet);
        BOOST_CHECK(pwalletMain->domainPendingMap.size() > 0);
        BOOST_CHECK(pwalletMain->domainPendingMap.find(domain1) != pwalletMain->domainPendingMap.end());
        BOOST_CHECK(pwalletMain->domainPendingMap.find(domain2) != pwalletMain->domainPendingMap.end());
    }

    {
        // test removing the domains
        LOCK(pwalletMain->cs_wallet);
        BOOST_CHECK(CWalletDB(dbw).EraseDomainFirstUpdate(domain1));
        BOOST_CHECK(CWalletDB(dbw).EraseDomainFirstUpdate(domain2));
    }
}

BOOST_AUTO_TEST_SUITE_END()