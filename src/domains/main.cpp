// Copyright (c) 2020 Beyondtoshi
// Copyright (c) 2020 The Beyondcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "domains/main.h"

#include "chainparams.h"
#include "coins.h"
#include "consensus/validation.h"
#include "leveldbwrapper.h"
#include "../main.h"
#include "script/domains.h"
#include "txmempool.h"
#include "undo.h"
#include "util.h"

/**
 * Check whether a domain at nPrevHeight is expired at nHeight.  Also
 * heights of MEMPOOL_HEIGHT are supported.  For nHeight == MEMPOOL_HEIGHT,
 * we check at the current best tip's height.
 * @param nPrevHeight The domain's output.
 * @param nHeight The height at which it should be updated.
 * @return True iff the domain is expired.
 */
static bool
isExpired (unsigned nPrevHeight, unsigned nHeight)
{
  assert (nHeight != MEMPOOL_HEIGHT);
  if (nPrevHeight == MEMPOOL_HEIGHT)
    return false;

  const Consensus::Params& params = Params ().GetConsensus ();
  return nPrevHeight + params.rules->DomainExpirationDepth (nHeight) <= nHeight;
}

/* ************************************************************************** */
/* CDomainData.  */

bool
CDomainData::isExpired () const
{
  return isExpired (chainActive.Height ());
}

bool
CDomainData::isExpired (unsigned h) const
{
  return ::isExpired (nHeight, h);
}

/* ************************************************************************** */
/* CDomainTxUndo.  */

void
CDomainTxUndo::fromOldState (const valtype& nm, const CCoinsView& view)
{
  domain = nm;
  isNew = !view.GetDomain (domain, oldData);
}

void
CDomainTxUndo::apply (CCoinsViewCache& view) const
{
  if (isNew)
    view.DeleteDomain (domain);
  else
    view.SetDomain (domain, oldData, true);
}

/* ************************************************************************** */
/* CDomainMemPool.  */

void
CDomainMemPool::addUnchecked (const uint256& hash, const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);

  if (entry.isDomainNew ())
    {
      const valtype& newHash = entry.getDomainNewHash ();
      const std::map<valtype, uint256>::const_iterator mit
        = mapDomainNews.find (newHash);
      if (mit != mapDomainNews.end ())
        assert (mit->second == hash);
      else
        mapDomainNews.insert (std::make_pair (newHash, hash));
    }

  if (entry.isDomainRegistration ())
    {
      const valtype& domain = entry.getDomain ();
      assert (mapDomainRegs.count (domain) == 0);
      mapDomainRegs.insert (std::make_pair (domain, hash));
    }

  if (entry.isDomainUpdate ())
    {
      const valtype& name = entry.getDomain ();
      assert (mapDomainUpdates.count (domain) == 0);
      mapDomainUpdates.insert (std::make_pair (domain, hash));
    }
}

void
CDomainMemPool::remove (const CTxMemPoolEntry& entry)
{
  AssertLockHeld (pool.cs);

  if (entry.isDomainRegistration ())
    {
      const std::map<valtype, uint256>::iterator mit
        = mapDomainRegs.find (entry.getDomain ());
      assert (mit != mapDomainRegs.end ());
      mapDomainRegs.erase (mit);
    }
  if (entry.isDomainUpdate ())
    {
      const std::map<valtype, uint256>::iterator mit
        = mapDomainUpdates.find (entry.getDomain ());
      assert (mit != mapDomainUpdates.end ());
      mapDomainUpdates.erase (mit);
    }
}

void
CDomainMemPool::removeConflicts (const CTransaction& tx,
                               std::list<CTransaction>& removed)
{
  AssertLockHeld (pool.cs);

  if (!tx.IsBeyondcoin ())
    return;

  BOOST_FOREACH (const CTxOut& txout, tx.vout)
    {
      const CDomainScript domainOp(txout.scriptPubKey);
      if (domainOp.isDomainOp () && domainOp.getDomainOp () == OP_DOMAIN_FIRSTUPDATE)
        {
          const valtype& domain = domainOp.getOpDomain ();
          const std::map<valtype, uint256>::const_iterator mit
            = mapDomainRegs.find (domain);
          if (mit != mapDomainRegs.end ())
            {
              const std::map<uint256, CTxMemPoolEntry>::const_iterator mit2
                = pool.mapTx.find (mit->second);
              assert (mit2 != pool.mapTx.end ());
              pool.remove (mit2->second.GetTx (), removed, true);
            }
        }
    }
}

void
CDomainMemPool::removeUnexpireConflicts (const std::set<valtype>& unexpired,
                                       std::list<CTransaction>& removed)
{
  AssertLockHeld (pool.cs);

  BOOST_FOREACH (const valtype& domain, unexpired)
    {
      const std::map<valtype, uint256>::const_iterator mit
        = mapDomainRegs.find (domain);
      if (mit != mapDomainRegs.end ())
        {
          const std::map<uint256, CTxMemPoolEntry>::const_iterator mit2
            = pool.mapTx.find (mit->second);
          assert (mit2 != pool.mapTx.end ());
          pool.remove (mit2->second.GetTx (), removed, true);
        }
    }
}

void
CDomainMemPool::removeExpireConflicts (const std::set<valtype>& expired,
                                     std::list<CTransaction>& removed)
{
  AssertLockHeld (pool.cs);

  BOOST_FOREACH (const valtype& domain, expired)
    {
      const std::map<valtype, uint256>::const_iterator mit
        = mapDomainUpdates.find (domain);
      if (mit != mapDomainUpdates.end ())
        {
          const std::map<uint256, CTxMemPoolEntry>::const_iterator mit2
            = pool.mapTx.find (mit->second);
          assert (mit2 != pool.mapTx.end ());
          pool.remove (mit2->second.GetTx (), removed, true);
        }
    }
}

void
CDomainMemPool::check (const CCoinsView& coins) const
{
  AssertLockHeld (pool.cs);

  const uint256 blockHash = coins.GetBestBlock ();
  int nHeight;
  if (blockHash.IsNull())
    nHeight = 0;
  else
    nHeight = mapBlockIndex.find (blockHash)->second->nHeight;

  std::set<valtype> domainRegs;
  std::set<valtype> domainUpdates;
  BOOST_FOREACH (const PAIRTYPE(const uint256, CTxMemPoolEntry)& entry,
                 pool.mapTx)
    {
      if (entry.second.isDomainNew ())
        {
          const valtype& newHash = entry.second.getDomainNewHash ();
          const std::map<valtype, uint256>::const_iterator mit
            = mapDomainNews.find (newHash);

          assert (mit != mapDomainNews.end ());
          assert (mit->second == entry.first);
        }

      if (entry.second.isDomainRegistration ())
        {
          const valtype& domain = entry.second.getDomain ();

          const std::map<valtype, uint256>::const_iterator mit
            = mapDomainRegs.find (domain);
          assert (mit != mapDomainRegs.end ());
          assert (mit->second == entry.first);

          assert (domainRegs.count (domain) == 0);
          domainRegs.insert (domain);

          /* The old domain should be expired already.  Note that we use
             nHeight+1 for the check, because that's the height at which
             the mempool tx will actually be mined.  */
          CDomainData data;
          if (coins.GetDomain (domain, data))
            assert (data.isExpired (nHeight + 1));
        }

      if (entry.second.isDomainUpdate ())
        {
          const valtype& domain = entry.second.getDomain ();

          const std::map<valtype, uint256>::const_iterator mit
            = mapDomainUpdates.find (domain);
          assert (mit != mapDomainUpdates.end ());
          assert (mit->second == entry.first);

          assert (domainUpdates.count (domain) == 0);
          domainUpdates.insert (domain);

          /* As above, use nHeight+1 for the expiration check.  */
          CDomainData data;
          if (!coins.GetDomain (domain, data))
            assert (false);
          assert (!data.isExpired (nHeight + 1));
        }
    }

  assert (domainRegs.size () == mapDomainRegs.size ());
  assert (domainUpdates.size () == mapDomainUpdates.size ());
}

bool
CDomainMemPool::checkTx (const CTransaction& tx) const
{
  AssertLockHeld (pool.cs);

  if (!tx.IsBeyondcoin ())
    return true;

  /* In principle, multiple domain_updates could be performed within the
     mempool at once (building upon each other).  This is disallowed, though,
     since the current mempool implementation does not like it.  (We keep
     track of only a single update tx for each domain.)  */

  BOOST_FOREACH (const CTxOut& txout, tx.vout)
    {
      const CDomainScript domainOp(txout.scriptPubKey);
      if (!domainOp.isDomainOp ())
        continue;

      switch (domainOp.getDomainOp ())
        {
        case OP_DOMAIN_NEW:
          {
            const valtype& newHash = domainOp.getOpHash ();
            std::map<valtype, uint256>::const_iterator mi;
            mi = mapDomainNews.find (newHash);
            if (mi != mapDomainNews.end () && mi->second != tx.GetHash ())
              return false;
            break;
          }

        case OP_DOMAIN_FIRSTUPDATE:
          {
            const valtype& domain = domainOp.getOpDomain ();
            if (registersDomain (domain))
              return false;
            break;
          }

        case OP_DOMAIN_UPDATE:
          {
            const valtype& domain = domainOp.getOpDomain ();
            if (updatesDomain (domain))
              return false;
            break;
          }

        default:
          assert (false);
        }
    }

  return true;
}

/* ************************************************************************** */

bool
CheckDomainTransaction (const CTransaction& tx, unsigned nHeight,
                      const CCoinsView& view,
                      CValidationState& state, unsigned flags)
{
  const std::string strTxid = tx.GetHash ().GetHex ();
  const char* txid = strTxid.c_str ();
  const bool fMempool = (flags & SCRIPT_VERIFY_DOMAINS_MEMPOOL);

  /* Ignore historic bugs.  */
  CChainParams::BugType type;
  if (Params ().IsHistoricBug (tx.GetHash (), nHeight, type))
    return true;

  /* As a first step, try to locate inputs and outputs of the transaction
     that are domain scripts.  At most one input and output should be
     a domain operation.  */

  int domainIn = -1;
  CDomainScript domainOpIn;
  CCoins coinsIn;
  for (unsigned i = 0; i < tx.vin.size (); ++i)
    {
      const COutPoint& prevout = tx.vin[i].prevout;
      CCoins coins;
      if (!view.GetCoins (prevout.hash, coins))
        return error ("%s: failed to fetch input coins for %s", __func__, txid);

      const CDomainScript op(coins.vout[prevout.n].scriptPubKey);
      if (op.isDomainOp ())
        {
          if (domainIn != -1)
            return state.Invalid (error ("%s: multiple domain inputs into"
                                         " transaction %s", __func__, txid));
          domainIn = i;
          domainOpIn = op;
          coinsIn = coins;
        }
    }

  int domainOut = -1;
  CDomainScript domainOpOut;
  for (unsigned i = 0; i < tx.vout.size (); ++i)
    {
      const CDomainScript op(tx.vout[i].scriptPubKey);
      if (op.isDomainOp ())
        {
          if (domainOut != -1)
            return state.Invalid (error ("%s: multiple domain outputs from"
                                         " transaction %s", __func__, txid));
          domainOut = i;
          domainOpOut = op;
        }
    }

  /* Check that no domain inputs/outputs are present for a non-Beyondcoin tx.
     If that's the case, all is fine. For a Beyondcoin tx instead, there
     should be at least an output (for DOMAIN_NEW, no inputs are expected).  */

  if (!tx.IsBeyondcoin ())
    {
      if (domainIn != -1)
        return state.Invalid (error ("%s: non-Beyondcoin tx %s has domain inputs",
                                     __func__, txid));
      if (domainOut != -1)
        return state.Invalid (error ("%s: non-Beyondcoin tx %s at height %u"
                                     " has domain outputs",
                                     __func__, txid, nHeight));

      return true;
    }

  assert (tx.IsBeyondcoin ());
  if (domainOut == -1)
    return state.Invalid (error ("%s: Beyondcoin tx %s has no domain outputs",
                                 __func__, txid));

  /* Reject "greedy domains".  */
  const Consensus::Params& params = Params ().GetConsensus ();
  if (tx.vout[domainOut].nValue < params.rules->MinBeyondCoinAmount(nHeight))
    return state.Invalid (error ("%s: greedy domain", __func__));

  /* Handle DOMAIN_NEW now, since this is easy and different from the other
     operations.  */

  if (domainOpOut.getDomainOp () == OP_DOMAIN_NEW)
    {
      if (domainIn != -1)
        return state.Invalid (error ("CheckDomainTransaction: DOMAIN_NEW with"
                                     " previous domain input"));

      if (domainOpOut.getOpHash ().size () != 20)
        return state.Invalid (error ("CheckDomainTransaction: DOMAIN_NEW's hash"
                                     " has wrong size"));

      return true;
    }

  /* Now that we have ruled out DOMAIN_NEW, check that we have a previous
     domain input that is being updated.  */

  assert (domainOpOut.isAnyUpdate ());
  if (nameIn == -1)
    return state.Invalid (error ("CheckDomainTransaction: update without"
                                 " previous domain input"));
  const valtype& name = nameOpOut.getOpDomain ();

  if (name.size () > MAX_DOMAIN_LENGTH)
    return state.Invalid (error ("CheckDomainTransaction: domain too long"));
  if (nameOpOut.getOpValue ().size () > MAX_VALUE_LENGTH)
    return state.Invalid (error ("CheckDomainTransaction: value too long"));

  /* Process DOMAIN_UPDATE next.  */

  if (nameOpOut.getDomainOp () == OP_DOMAIN_UPDATE)
    {
      if (!nameOpIn.isAnyUpdate ())
        return state.Invalid (error ("CheckDomainTransaction: DOMAIN_UPDATE with"
                                     " prev input that is no update"));

      if (name != nameOpIn.getOpDomain ())
        return state.Invalid (error ("%s: DOMAIN_UPDATE domain mismatch to prev tx"
                                     " found in %s", __func__, txid));

      /* This is actually redundant, since expired domains are removed
         from the UTXO set and thus not available to be spent anyway.
         But it does not hurt to enforce this here, too.  It is also
         exercised by the unit tests.  */
      if (isExpired (coinsIn.nHeight, nHeight))
        return state.Invalid (error ("CheckDomainTransaction: trying to update"
                                     " expired domain"));

      return true;
    }

  /* Finally, DOMAIN_FIRSTUPDATE.  */

  assert (nameOpOut.getDomainOp () == OP_DOMAIN_FIRSTUPDATE);
  if (nameOpIn.getDomainOp () != OP_DOMAIN_NEW)
    return state.Invalid (error ("CheckDomainTransaction: DOMAIN_FIRSTUPDATE"
                                 " with non-DOMAIN_NEW prev tx"));

  /* Maturity of DOMAIN_NEW is checked only if we're not adding
     to the mempool.  */
  if (!fMempool)
    {
      assert (static_cast<unsigned> (coinsIn.nHeight) != MEMPOOL_HEIGHT);
      if (coinsIn.nHeight + MIN_FIRSTUPDATE_DEPTH > nHeight)
        return state.Invalid (error ("CheckDomainTransaction: DOMAIN_NEW"
                                     " is not mature for FIRST_UPDATE"));
    }

  if (nameOpOut.getOpRand ().size () > 20)
    return state.Invalid (error ("CheckDomainTransaction: DOMAIN_FIRSTUPDATE"
                                 " rand too large, %d bytes",
                                 nameOpOut.getOpRand ().size ()));

  {
    valtype toHash(nameOpOut.getOpRand ());
    toHash.insert (toHash.end (), name.begin (), name.end ());
    const uint160 hash = Hash160 (toHash);
    if (hash != uint160 (nameOpIn.getOpHash ()))
      return state.Invalid (error ("CheckDomainTransaction: DOMAIN_FIRSTUPDATE"
                                   " hash mismatch"));
  }

  CDomainData oldDomain;
  if (view.GetDomain (name, oldDomain) && !oldDomain.isExpired (nHeight))
    return state.Invalid (error ("CheckDomainTransaction: DOMAIN_FIRSTUPDATE"
                                 " on an unexpired name"));

  /* We don't have to specifically check that miners don't create blocks with
     conflicting DOMAIN_FIRSTUPDATE's, since the mining's CCoinsViewCache
     takes care of this with the check above already.  */

  return true;
}

void
ApplyDomainTransaction (const CTransaction& tx, unsigned nHeight,
                      CCoinsViewCache& view, CBlockUndo& undo)
{
  assert (nHeight != MEMPOOL_HEIGHT);

  /* Handle historic bugs that should *not* be applied.  Domains that are
     outputs should be marked as unspendable in this case.  Otherwise,
     we get an inconsistency between the UTXO set and the name database.  */
  CChainParams::BugType type;
  const uint256 txHash = tx.GetHash ();
  if (Params ().IsHistoricBug (txHash, nHeight, type)
      && type != CChainParams::BUG_FULLY_APPLY)
    {
      if (type == CChainParams::BUG_FULLY_IGNORE)
        {
          CCoinsModifier coins = view.ModifyCoins (txHash);
          for (unsigned i = 0; i < tx.vout.size (); ++i)
            {
              const CDomainScript op(tx.vout[i].scriptPubKey);
              if (op.isDomainOp () && op.isAnyUpdate ())
                {
                  if (!coins->IsAvailable (i) || !coins->Spend (i))
                    LogPrintf ("ERROR: %s : spending buggy name output failed",
                               __func__);
                }
            }
        }

      return;
    }

  /* This check must be done *after* the historic bug fixing above!  Some
     of the names that must be handled above are actually produced by
     transactions *not* marked as Beyondcoin tx.  */
  if (!tx.IsBeyondcoin ())
    return;

  /* Changes are encoded in the outputs.  We don't have to do any checks,
     so simply apply all these.  */

  for (unsigned i = 0; i < tx.vout.size (); ++i)
    {
      const CDomainScript op(tx.vout[i].scriptPubKey);
      if (op.isDomainOp () && op.isAnyUpdate ())
        {
          const valtype& name = op.getOpDomain ();
          if (fDebug)
            LogPrintf ("Updating name at height %d: %s\n",
                       nHeight, ValtypeToString (name).c_str ());

          CDomainTxUndo opUndo;
          opUndo.fromOldState (name, view);
          undo.vnameundo.push_back (opUndo);

          CDomainData data;
          data.fromScript (nHeight, COutPoint (tx.GetHash (), i), op);
          view.SetDomain (name, data, false);
        }
    }
}

bool
ExpireDomains (unsigned nHeight, CCoinsViewCache& view, CBlockUndo& undo,
             std::set<valtype>& names)
{
  names.clear ();

  /* The genesis block contains no name expirations.  */
  if (nHeight == 0)
    return true;

  /* Otherwise, find out at which update heights names have expired
     since the last block.  If the expiration depth changes, this could
     be multiple heights at once.  */

  const Consensus::Params& params = Params ().GetConsensus ();
  const unsigned expDepthOld = params.rules->DomainExpirationDepth (nHeight - 1);
  const unsigned expDepthNow = params.rules->DomainExpirationDepth (nHeight);

  if (expDepthNow > nHeight)
    return true;

  /* Both are inclusive!  The last expireTo was nHeight - 1 - expDepthOld,
     now we start at this value + 1.  */
  const unsigned expireFrom = nHeight - expDepthOld;
  const unsigned expireTo = nHeight - expDepthNow;

  /* It is possible that expireFrom = expireTo + 1, in case that the
     expiration period is raised together with the block height.  In this
     case, no names expire in the current step.  This case means that
     the absolute expiration height "n - expirationDepth(n)" is
     flat -- which is fine.  */
  assert (expireFrom <= expireTo + 1);

  /* Find all names that expire at those depths.  Note that GetDomainsForHeight
     clears the output set, to we union all sets here.  */
  for (unsigned h = expireFrom; h <= expireTo; ++h)
    {
      std::set<valtype> newDomains;
      view.GetDomainsForHeight (h, newDomains);
      names.insert (newDomains.begin (), newDomains.end ());
    }

  /* Expire all those names.  */
  for (std::set<valtype>::const_iterator i = names.begin ();
       i != names.end (); ++i)
    {
      const std::string nameStr = ValtypeToString (*i);

      CDomainData data;
      if (!view.GetDomain (*i, data))
        return error ("%s : name '%s' not found in the database",
                      __func__, nameStr.c_str ());
      if (!data.isExpired (nHeight))
        return error ("%s : name '%s' is not actually expired",
                      __func__, nameStr.c_str ());

      /* Special rule:  When d/postmortem expires (the name used by
         libcoin in the name-stealing demonstration), it's coin
         is already spent.  Ignore.  */
      if (nHeight == 175868 && nameStr == "d/postmortem")
        continue;

      const COutPoint& out = data.getUpdateOutpoint ();
      CCoinsModifier coins = view.ModifyCoins (out.hash);

      if (!coins->IsAvailable (out.n))
        return error ("%s : name coin for '%s' is not available",
                      __func__, nameStr.c_str ());
      const CDomainScript nameOp(coins->vout[out.n].scriptPubKey);
      if (!nameOp.isDomainOp () || !nameOp.isAnyUpdate ()
          || nameOp.getOpDomain () != *i)
        return error ("%s : name coin to be expired is wrong script", __func__);

      CTxInUndo txUndo;
      if (!coins->Spend (out.n, &txUndo))
        return error ("%s : failed to spend name coin for '%s'",
                      __func__, nameStr.c_str ());
      undo.vexpired.push_back (txUndo);
    }

  return true;
}

bool
UnexpireDomains (unsigned nHeight, const CBlockUndo& undo, CCoinsViewCache& view,
               std::set<valtype>& names)
{
  names.clear ();

  /* The genesis block contains no name expirations.  */
  if (nHeight == 0)
    return true;

  std::vector<CTxInUndo>::const_reverse_iterator i;
  for (i = undo.vexpired.rbegin (); i != undo.vexpired.rend (); ++i)
    {
      const CDomainScript nameOp(i->txout.scriptPubKey);
      if (!nameOp.isDomainOp () || !nameOp.isAnyUpdate ())
        return error ("%s : wrong script to be unexpired", __func__);

      const valtype& name = nameOp.getOpDomain ();
      if (names.count (name) > 0)
        return error ("%s : name '%s' unexpired twice",
                      __func__, ValtypeToString (name).c_str ());
      names.insert (name);

      CDomainData data;
      if (!view.GetDomain (nameOp.getOpDomain (), data))
        return error ("%s : no data for name '%s' to be unexpired",
                      __func__, ValtypeToString (name).c_str ());
      if (!data.isExpired (nHeight) || data.isExpired (nHeight - 1))
        return error ("%s : name '%s' to be unexpired is not expired in the DB"
                      " or it was already expired before the current height",
                      __func__, ValtypeToString (name).c_str ());

      if (!ApplyTxInUndo (*i, view, data.getUpdateOutpoint ()))
        return error ("%s : failed to undo name coin spending", __func__);
    }

  return true;
}

void
CheckDomainDB (bool disconnect)
{
  const int option = GetArg ("-checknamedb", Params ().DefaultCheckDomainDB ());

  if (option == -1)
    return;

  assert (option >= 0);
  if (option != 0)
    {
      if (disconnect || chainActive.Height () % option != 0)
        return;
    }

  pcoinsTip->Flush ();
  const bool ok = pcoinsTip->ValidateDomainDB ();

  /* The DB is inconsistent (mismatch between UTXO set and names DB) between
     (roughly) blocks 139,000 and 180,000.  This is caused by libcoin's
     "name stealing" bug.  For instance, d/postmortem is removed from
     the UTXO set shortly after registration (when it is used to steal
     names), but it remains in the name DB until it expires.  */
  if (!ok)
    {
      const unsigned nHeight = chainActive.Height ();
      LogPrintf ("ERROR: %s : name database is inconsistent\n", __func__);
      if (nHeight >= 139000 && nHeight <= 180000)
        LogPrintf ("This is expected due to 'name stealing'.\n");
      else
        assert (false);
    }
}