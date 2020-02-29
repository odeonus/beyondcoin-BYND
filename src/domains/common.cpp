// Copyright (c) 2020 Beyondtoshi
// Copyright (c) 2020 The Beyondcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "domains/common.h"

#include "script/domains.h"

bool fDomainHistory = false;

/* ************************************************************************** */
/* CDomainData.  */

void
CDomainData::fromScript (unsigned h, const COutPoint& out,
                       const CDomainScript& script)
{
  assert (script.isAnyUpdate ());
  value = script.getOpValue ();
  nHeight = h;
  prevout = out;
  addr = script.getAddress ();
}

/* ************************************************************************** */
/* CDomainIterator.  */

CDomainIterator::~CDomainIterator ()
{
  /* Nothing to be done here.  This may be overwritten by
     subclasses if they need a destructor.  */
}

/* ************************************************************************** */
/* CDomainCacheDomainIterator.  */

class CCacheDomainIterator : public CDomainIterator
{

private:

  /** Reference to cache object that is used.  */
  const CDomainCache& cache;

  /** Base iterator to combine with the cache.  */
  CDomainIterator* base;

  /** Whether or not the base iterator has more entries.  */
  bool baseHasMore;
  /** "Next" domain of the base iterator.  */
  valtype baseDomain;
  /** "Next" data of the base iterator.  */
  CDomainData baseData;

  /** Iterator of the cache's entries.  */
  CDomainCache::EntryMap::const_iterator cacheIter;

  /* Call the base iterator's next() routine to fill in the internal
     "cache" for the next entry.  This already skips entries that are
     marked as deleted in the cache.  */
  void advanceBaseIterator ();

public:

  /**
   * Construct the iterator.  This takes ownership of the base iterator.
   * @param c The cache object to use.
   * @param b The base iterator.
   */
  CCacheDomainIterator (const CDomainCache& c, CDomainIterator* b);

  /* Destruct, this deletes also the base iterator.  */
  ~CCacheDomainIterator ();

  /* Implement iterator methods.  */
  void seek (const valtype& domain);
  bool next (valtype& domain, CDomainData& data);

};

CCacheDomainIterator::CCacheDomainIterator (const CDomainCache& c, CDomainIterator* b)
  : cache(c), base(b)
{
  /* Add a seek-to-start to ensure that everything is consistent.  This call
     may be superfluous if we seek to another position afterwards anyway,
     but it should also not hurt too much.  */
  seek (valtype ());
}

CCacheDomainIterator::~CCacheDomainIterator ()
{
  delete base;
}

void
CCacheDomainIterator::advanceBaseIterator ()
{
  assert (baseHasMore);
  do
    baseHasMore = base->next (baseDomain, baseData);
  while (baseHasMore && cache.isDeleted (baseDomain));
}

void
CCacheDomainIterator::seek (const valtype& start)
{
  cacheIter = cache.entries.lower_bound (start);
  base->seek (start);

  baseHasMore = true;
  advanceBaseIterator ();
}

bool
CCacheDomainIterator::next (valtype& domain, CDomainData& data)
{
  /* Exit early if no more data is available in either the cache
     nor the base iterator.  */
  if (!baseHasMore && cacheIter == cache.entries.end ())
    return false;

  /* Determine which source to use for the next.  */
  bool useBase;
  if (!baseHasMore)
    useBase = false;
  else if (cacheIter == cache.entries.end ())
    useBase = true;
  else
    {
      /* A special case is when both iterators are equal.  In this case,
         we want to use the cached version.  We also have to advance
         the base iterator.  */
      if (baseDomain == cacheIter->first)
        advanceBaseIterator ();

      /* Due to advancing the base iterator above, it may happen that
         no more base entries are present.  Handle this gracefully.  */
      if (!baseHasMore)
        useBase = false;
      else
        {
          assert (baseDomain != cacheIter->first);

          CDomainCache::DomainComparator cmp;
          useBase = cmp (baseDomain, cacheIter->first);
        }
    }

  /* Use the correct source now and advance it.  */
  if (useBase)
    {
      domain = baseDomain;
      data = baseData;
      advanceBaseIterator ();
    }
  else
    {
      domain = cacheIter->first;
      data = cacheIter->second;
      ++cacheIter;
    }

  return true;
}

/* ************************************************************************** */
/* CDomainCache.  */

bool
CDomainCache::get (const valtype& domain, CDomainData& data) const
{
  const EntryMap::const_iterator i = entries.find (domain);
  if (i == entries.end ())
    return false;

  data = i->second;
  return true;
}

void
CDomainCache::set (const valtype& domain, const CDomainData& data)
{
  const std::set<valtype>::iterator di = deleted.find (domain);
  if (di != deleted.end ())
    deleted.erase (di);

  const EntryMap::iterator ei = entries.find (domain);
  if (ei != entries.end ())
    ei->second = data;
  else
    entries.insert (std::make_pair (domain, data));
}

void
CDomainCache::remove (const valtype& domain)
{
  const EntryMap::iterator ei = entries.find (domain);
  if (ei != entries.end ())
    entries.erase (ei);

  deleted.insert (domain);
}

CDomainIterator*
CDomainCache::iterateDomains (CDomainIterator* base) const
{
  return new CCacheDomainIterator (*this, base);
}

bool
CDomainCache::getHistory (const valtype& domain, CDomainHistory& res) const
{
  assert (fDomainHistory);

  const std::map<valtype, CDomainHistory>::const_iterator i = history.find (domain);
  if (i == history.end ())
    return false;

  res = i->second;
  return true;
}

void
CDomainCache::setHistory (const valtype& domain, const CDomainHistory& data)
{
  assert (fDomainHistory);

  const std::map<valtype, CDomainHistory>::iterator ei = history.find (domain);
  if (ei != history.end ())
    ei->second = data;
  else
    history.insert (std::make_pair (domain, data));
}

void
CDomainCache::updateDomainsForHeight (unsigned nHeight,
                                  std::set<valtype>& domains) const
{
  /* Seek in the map of cached entries to the first one corresponding
     to our height.  */

  const ExpireEntry seekEntry(nHeight, valtype ());
  std::map<ExpireEntry, bool>::const_iterator it;

  for (it = expireIndex.lower_bound (seekEntry); it != expireIndex.end (); ++it)
    {
      const ExpireEntry& cur = it->first;
      assert (cur.nHeight >= nHeight);
      if (cur.nHeight > nHeight)
        break;

      if (it->second)
        domains.insert (cur.domain);
      else
        domains.erase (cur.domain);
    }
}

void
CDomainCache::addExpireIndex (const valtype& domain, unsigned height)
{
  const ExpireEntry entry(height, domain);
  expireIndex[entry] = true;
}

void
CDomainCache::removeExpireIndex (const valtype& domain, unsigned height)
{
  const ExpireEntry entry(height, domain);
  expireIndex[entry] = false;
}

void
CDomainCache::apply (const CDomainCache& cache)
{
  for (EntryMap::const_iterator i = cache.entries.begin ();
       i != cache.entries.end (); ++i)
    set (i->first, i->second);

  for (std::set<valtype>::const_iterator i = cache.deleted.begin ();
       i != cache.deleted.end (); ++i)
    remove (*i);

  for (std::map<valtype, CDomainHistory>::const_iterator i
        = cache.history.begin (); i != cache.history.end (); ++i)
    setHistory (i->first, i->second);

  for (std::map<ExpireEntry, bool>::const_iterator i
        = cache.expireIndex.begin (); i != cache.expireIndex.end (); ++i)
    expireIndex[i->first] = i->second;
}