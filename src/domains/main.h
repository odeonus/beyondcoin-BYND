// Copyright (c) 2020 Beyondtoshi
// Copyright (c) 2020 The Beyondcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_DOMAINS_MAIN
#define H_BITCOIN_DOMAINS_MAIN

#include "amount.h"
#include "domains/common.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <list>
#include <map>
#include <set>
#include <string>

class CBlockUndo;
class CCoinsView;
class CCoinsViewCache;
class CTxMemPool;
class CTxMemPoolEntry;
class CValidationState;

/* Some constants defining domain limits.  */
static const unsigned MAX_VALUE_LENGTH = 1023;
static const unsigned MAX_DOMAIN_LENGTH = 255;
static const unsigned MIN_FIRSTUPDATE_DEPTH = 12;
static const unsigned MAX_VALUE_LENGTH_UI = 520;

/** The amount of coins to lock in created transactions.  */
static const CAmount DOMAIN_LOCKED_AMOUNT = COIN / 100;

/* ************************************************************************** */
/* CDomainTxUndo.  */

/**
 * Undo information for one domain operation.  This contains either the
 * information that the domain was newly created (and should thus be
 * deleted entirely) or that it was updated including the old value.
 */
class CDomainTxUndo
{

private:

  /** The domain this concerns.  */
  valtype domain;

  /** Whether this was an entirely new domain (no update).  */
  bool isNew;

  /** The old domain value that was overwritten by the operation.  */
  CDomainData oldData;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action,
                                 int nType, int nVersion)
  {
    READWRITE (domain);
    READWRITE (isNew);
    if (!isNew)
      READWRITE (oldData);
  }

  /**
   * Set the data for an update/registration of the given domain. The CCoinsView
   * is used to find out all the necessary information.
   * @param nm The domain that is being updated.
   * @param view The (old!) chain state.
   */
  void fromOldState (const valtype& nm, const CCoinsView& view);

  /**
   * Apply the undo to the chain state given.
   * @param view The chain state to update ("undo").
   */
  void apply (CCoinsViewCache& view) const;

};

/* ************************************************************************** */
/* CDomainMemPool.  */

/**
 * Handle the domain component of the transaction mempool.  This keeps track
 * of domain operations that are in the mempool and ensures that all transactions
 * kept are consistent.  E. g., no two transactions are allowed to register
 * the same domain, and domain registration transactions are removed if a
 * conflicting registration makes it into a block.
 */
class CDomainMemPool
{

private:

  /** The parent mempool object.  Used to, e. g., remove conflicting tx.  */
  CTxMemPool& pool;

  /**
   * Keep track of domains that are registered by transactions in the pool.
   * Map domain to registering transaction.
   */
  std::map<valtype, uint256> mapDomainRegs;

  /** Map pending domain updates to transaction IDs.  */
  std::map<valtype, uint256> mapDomainUpdates;

  /**
   * Map DOMAIN_NEW hashes to the corresponding transaction IDs.  This is
   * data that is kept only in memory but never cleared (until a restart).
   * It is used to prevent "domain_new stealing", at least in a "soft" way.
   */
  std::map<valtype, uint256> mapDomainNews;

public:

  /**
   * Construct with reference to parent mempool.
   * @param p The parent pool.
   */
  explicit inline CDomainMemPool (CTxMemPool& p)
    : pool(p), mapDomainRegs(), mapDomainUpdates(), mapDomainNews()
  {}

  /**
   * Check whether a particular domain is being registered by
   * some transaction in the mempool.  Does not lock, this is
   * done by the parent mempool (which calls through afterwards).
   * @param domain The domain to check for.
   * @return True iff there's a matching domain registration in the pool.
   */
  inline bool
  registersDomain (const valtype& domain) const
  {
    return mapDomainRegs.count (domain) > 0;
  }

  /**
   * Check whether a particular domain has a pending update.  Does not lock.
   * @param domain The domain to check for.
   * @return True iff there's a matching domain update in the pool.
   */
  inline bool
  updatesDomain (const valtype& domain) const
  {
    return mapDomainUpdates.count (domain) > 0;
  }

  /**
   * Clear all data.
   */
  inline void
  clear ()
  {
    mapDomainRegs.clear ();
    mapDomainUpdates.clear ();
    mapDomainNews.clear ();
  }

  /**
   * Add an entry without checking it.  It should have been checked
   * already.  If this conflicts with the mempool, it may throw.
   * @param hash The tx hash.
   * @param entry The new mempool entry.
   */
  void addUnchecked (const uint256& hash, const CTxMemPoolEntry& entry);

  /**
   * Remove the given mempool entry.  It is assumed that it is present.
   * @param entry The entry to remove.
   */
  void remove (const CTxMemPoolEntry& entry);

  /**
   * Remove conflicts for the given tx, based on domain operations.  I. e.,
   * if the tx registers a domain that conflicts with another registration
   * in the mempool, detect this and remove the mempool tx accordingly.
   * @param tx The transaction for which we look for conflicts.
   * @param removed Put removed tx here.
   */
  void removeConflicts (const CTransaction& tx,
                        std::list<CTransaction>& removed);

  /**
   * Remove conflicts in the mempool due to unexpired domains.  This removes
   * conflicting domain registrations that are no longer possible.
   * @param unexpired The set of unexpired domains.
   * @param removed Put removed tx here.
   */
  void removeUnexpireConflicts (const std::set<valtype>& unexpired,
                                std::list<CTransaction>& removed);
  /**
   * Remove conflicts in the mempool due to expired domains.  This removes
   * conflicting domain updates that are no longer possible.
   * @param expired The set of expired domains.
   * @param removed Put removed tx here.
   */
  void removeExpireConflicts (const std::set<valtype>& expired,
                              std::list<CTransaction>& removed);

  /**
   * Perform sanity checks.  Throws if it fails.
   * @param coins The coins view this represents.
   */
  void check (const CCoinsView& coins) const;

  /**
   * Check if a tx can be added (based on domain criteria) without
   * causing a conflict.
   * @param tx The transaction to check.
   * @return True if it doesn't conflict.
   */
  bool checkTx (const CTransaction& tx) const;

};

/* ************************************************************************** */

/**
 * Check a transaction according to the additional Beyondcoin rules.  This
 * ensures that all domain operations (if any) are valid and that it has
 * domain operations if it is marked as a Beyondcoin tx by its version.
 * @param tx The transaction to check.
 * @param nHeight Height at which the tx will be.  May be MEMPOOL_HEIGHT.
 * @param view The current chain state.
 * @param state Resulting validation state.
 * @param flags Verification flags.
 * @return True in case of success.
 */
bool CheckDomainTransaction (const CTransaction& tx, unsigned nHeight,
                           const CCoinsView& view,
                           CValidationState& state, unsigned flags);

/**
 * Apply the changes of a domain transaction to the domain database.
 * @param tx The transaction to apply.
 * @param nHeight Height at which the tx is.  Used for CDomainData.
 * @param view The chain state to update.
 * @param undo Record undo information here.
 */
void ApplyDomainTransaction (const CTransaction& tx, unsigned nHeight,
                           CCoinsViewCache& view, CBlockUndo& undo);

/**
 * Expire all domains at the given height.  This removes their coins
 * from the UTXO set.
 * @param height The new block height.
 * @param view The coins view to update.
 * @param undo The block undo object to record undo information.
 * @param domains List all expired domains here.
 * @return True if successful.
 */
bool ExpireDomains (unsigned nHeight, CCoinsViewCache& view, CBlockUndo& undo,
                  std::set<valtype>& domains);

/**
 * Undo domain coin expirations.  This also does some checks verifying
 * that all is fine.
 * @param nHeight The height at which the domains were expired.
 * @param undo The block undo object to use.
 * @param view The coins view to update.
 * @param domains List all unexpired domains here.
 * @return True if successful.
 */
bool UnexpireDomains (unsigned nHeight, const CBlockUndo& undo,
                    CCoinsViewCache& view, std::set<valtype>& domains);

/**
 * Check the domain database consistency.  This calls CCoinsView::ValidateDomainDB,
 * but only if applicable depending on the -checkdomaindb setting.  If it fails,
 * this throws an assertion failure.
 * @param disconnect Whether we are disconnecting blocks.
 */
void CheckDomainDB (bool disconnect);

#endif // H_BITCOIN_DOMAINS_MAIN