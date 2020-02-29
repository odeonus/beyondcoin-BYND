// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_DOMAINS_COMMON
#define H_BITCOIN_DOMAINS_COMMON

#include <base58.h>
#include <compat/endian.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <map>
#include <set>

class CDomainScript;
class CDBBatch;

/** Whether or not domain history is enabled.  */
extern bool fDomainHistory;

/**
 * Construct a valtype (e. g., domain) from a string.
 * @param str The string input.
 * @return The corresponding valtype.
 */
inline valtype
ValtypeFromString (const std::string& str)
{
  return valtype (str.begin (), str.end ());
}

/**
 * Convert a valtype to a string.
 * @param val The valtype value.
 * @return Corresponding string.
 */
inline std::string
ValtypeToString (const valtype& val)
{
  return std::string (val.begin (), val.end ());
}

/* ************************************************************************** */
/* CDomainData.  */

/**
 * Information stored for a domain in the database.
 */
class CDomainData
{

private:

  /** The domain's value.  */
  valtype value;

  /** The transaction's height.  Used for expiry.  */
  unsigned nHeight;

  /** The domain's last update outpoint.  */
  COutPoint prevout;

  /**
   * The domain's address (as script).  This is kept here also, because
   * that information is useful to extract on demand (e. g., in domain_show).
   */
  CScript addr;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action)
  {
    READWRITE (value);
    READWRITE (nHeight);
    READWRITE (prevout);
    READWRITE (*(CScriptBase*)(&addr));
  }

  /* Compare for equality.  */
  friend inline bool
  operator== (const CDomainData& a, const CDomainData& b)
  {
    return a.value == b.value && a.nHeight == b.nHeight
            && a.prevout == b.prevout && a.addr == b.addr;
  }
  friend inline bool
  operator!= (const CDomainData& a, const CDomainData& b)
  {
    return !(a == b);
  }

  /**
   * Get the height.
   * @return The domain's update height.
   */
  inline unsigned
  getHeight () const
  {
    return nHeight;
  }

  /**
   * Get the value.
   * @return The domain's value.
   */
  inline const valtype&
  getValue () const
  {
    return value;
  }

  /**
   * Get the domain's update outpoint.
   * @return The update outpoint.
   */
  inline const COutPoint&
  getUpdateOutpoint () const
  {
    return prevout;
  }

  /**
   * Get the address.
   * @return The domain's address.
   */
  inline const CScript&
  getAddress () const
  {
    return addr;
  }

  /**
   * Check if the domain is expired at the current chain height.
   * @return True iff the domain is expired.
   */
  bool isExpired () const;

  /**
   * Check if the domain is expired at the given height.
   * @param h The height at which to check.
   * @return True iff the domain is expired at height h.
   */
  bool isExpired (unsigned h) const;

  /**
   * Set from a domain update operation.
   * @param h The height (not available from script).
   * @param out The update outpoint.
   * @param script The domain script.  Should be a domain (first) update.
   */
  void fromScript (unsigned h, const COutPoint& out, const CDomainScript& script);

};

/* ************************************************************************** */
/* CDomainHistory.  */

/**
 * Keep track of a domain's history.  This is a stack of old CDomainData
 * objects that have been obsoleted.
 */
class CDomainHistory
{

private:

  /** The actual data.  */
  std::vector<CDomainData> data;

public:

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action)
  {
    READWRITE (data);
  }

  /**
   * Check if the stack is empty.  This is used to decide when to fully
   * delete an entry in the database.
   * @return True iff the data stack is empty.
   */
  inline bool
  empty () const
  {
    return data.empty ();
  }

  /**
   * Access the data in a read-only way.
   * @return The data stack.
   */
  inline const std::vector<CDomainData>&
  getData () const
  {
    return data;
  }

  /**
   * Push a new entry onto the data stack.  The new entry's height should
   * be at least as high as the stack top entry's.  If not, fail.
   * @param entry The new entry to push onto the stack.
   */
  inline void
  push (const CDomainData& entry)
  {
    assert (data.empty () || data.back ().getHeight () <= entry.getHeight ());
    data.push_back (entry);
  }

  /**
   * Pop the top entry off the stack.  This is used when undoing domain
   * changes.  The domain's new value is passed as argument and should
   * match the removed entry.  If not, fail.
   * @param entry The domain's value after undoing.
   */
  inline void
  pop (const CDomainData& entry)
  {
    assert (!data.empty () && data.back () == entry);
    data.pop_back ();
  }

};

/* ************************************************************************** */
/* CDomainIterator.  */

/**
 * Interface for iterators over the domain database.
 */
class CDomainIterator
{

public:

  // Virtual destructor in case subclasses need them.
  virtual ~CDomainIterator ();

  /**
   * Seek to a given lower bound.
   * @param start The domain to seek to.
   */
  virtual void seek (const valtype& domain) = 0;

  /**
   * Get the next domain.  Returns false if no more domains are available.
   * @param domain Put the domain here.
   * @param data Put the domain's data here.
   * @return True if successful, false if no more domains.
   */
  virtual bool next (valtype& domain, CDomainData& data) = 0;

};

/* ************************************************************************** */
/* CDomainCache.  */

/**
 * Cache / record of updates to the domain database.  In addition to
 * new domains (or updates to them), this also keeps track of deleted domains
 * (when rolling back changes).
 */
class CDomainCache
{

private:

  /**
   * Special comparator class for domains that compares by length first.
   * This is used to sort the cache entry map in the same way as the
   * database is sorted.
   */
  class DomainComparator
  {
  public:
    inline bool
    operator() (const valtype& a, const valtype& b) const
    {
      if (a.size () != b.size ())
        return a.size () < b.size ();

      return a < b;
    }
  };

public:

  /**
   * Type for expire-index entries.  We have to make sure that
   * it is serialised in such a way that ordering is done correctly
   * by height.  This is not true if we use a std::pair, since then
   * the height is serialised as byte-array with little-endian order,
   * which does not correspond to the ordering by actual value.
   */
  class ExpireEntry
  {
  public:

    unsigned nHeight;
    valtype domain;

    inline ExpireEntry ()
      : nHeight(0), domain()
    {}

    inline ExpireEntry (unsigned h, const valtype& n)
      : nHeight(h), domain(n)
    {}

    /* Default copy and assignment.  */

    template<typename Stream>
      inline void
      Serialize (Stream& s) const
    {
      /* Flip the byte order of nHeight to big endian.  */
      const uint32_t nHeightFlipped = htobe32 (nHeight);

      ::Serialize (s, nHeightFlipped);
      ::Serialize (s, domain);
    }

    template<typename Stream>
      inline void
      Unserialize (Stream& s)
    {
      uint32_t nHeightFlipped;

      ::Unserialize (s, nHeightFlipped);
      ::Unserialize (s, domain);

      /* Unflip the byte order.  */
      nHeight = be32toh (nHeightFlipped);
    }

    friend inline bool
    operator== (const ExpireEntry& a, const ExpireEntry& b)
    {
      return a.nHeight == b.nHeight && a.domain == b.domain;
    }

    friend inline bool
    operator!= (const ExpireEntry& a, const ExpireEntry& b)
    {
      return !(a == b);
    }

    friend inline bool
    operator< (const ExpireEntry& a, const ExpireEntry& b)
    {
      if (a.nHeight != b.nHeight)
        return a.nHeight < b.nHeight;

      return a.domain < b.domain;
    }

  };

  /**
   * Type of domain entry map.  This is public because it is also used
   * by the unit tests.
   */
  typedef std::map<valtype, CDomainData, DomainComparator> EntryMap;

private:

  /** New or updated domains.  */
  EntryMap entries;
  /** Deleted domains.  */
  std::set<valtype> deleted;

  /**
   * New or updated history stacks.  If they are empty, the corresponding
   * database entry is deleted instead.
   */
  std::map<valtype, CDomainHistory> history;

  /**
   * Changes to be performed to the expire index.  The entry is mapped
   * to either "true" (meaning to add it) or "false" (delete).
   */
  std::map<ExpireEntry, bool> expireIndex;

  friend class CCacheDomainIterator;

public:

  inline void
  clear ()
  {
    entries.clear ();
    deleted.clear ();
    history.clear ();
    expireIndex.clear ();
  }

  /**
   * Check if the cache is "clean" (no cached changes).  This also
   * performs internal checks and fails with an assertion if the
   * internal state is inconsistent.
   * @return True iff no changes are cached.
   */
  inline bool
  empty () const
  {
    if (entries.empty () && deleted.empty ())
      {
        assert (history.empty () && expireIndex.empty ());
        return true;
      }

    return false;
  }

  /* See if the given domain is marked as deleted.  */
  inline bool
  isDeleted (const valtype& domain) const
  {
    return (deleted.count (domain) > 0); 
  }

  /* Try to get a domain's associated data.  This looks only
     in entries, and doesn't care about deleted data.  */
  bool get (const valtype& domain, CDomainData& data) const;

  /* Insert (or update) a domain.  If it is marked as "deleted", this also
     removes the "deleted" mark.  */
  void set (const valtype& domain, const CDomainData& data);

  /* Delete a domain.  If it is in the "entries" set also, remove it there.  */
  void remove (const valtype& domain);

  /* Return a domain iterator that combines a "base" iterator with the changes
     made to it according to the cache.  The base iterator is taken
     ownership of.  */
  CDomainIterator* iterateDomains (CDomainIterator* base) const;

  /**
   * Query for an history entry.
   * @param domain The domain to look up.
   * @param res Put the resulting history entry here.
   * @return True iff the domain was found in the cache.
   */
  bool getHistory (const valtype& domain, CDomainHistory& res) const;

  /**
   * Set a domain history entry.
   * @param domain The domain to modify.
   * @param data The new history entry.
   */
  void setHistory (const valtype& domain, const CDomainHistory& data);

  /* Query the cached changes to the expire index.  In particular,
     for a given height and a given set of domains that were indexed to
     this update height, apply possible changes to the set that
     are represented by the cached expire index changes.  */
  void updateDomainsForHeight (unsigned nHeight, std::set<valtype>& domains) const;

  /* Add an expire-index entry.  */
  void addExpireIndex (const valtype& domain, unsigned height);

  /* Remove an expire-index entry.  */
  void removeExpireIndex (const valtype& domain, unsigned height);

  /* Apply all the changes in the passed-in record on top of this one.  */
  void apply (const CDomainCache& cache);

  /* Write all cached changes to a database batch update object.  */
  void writeBatch (CDBBatch& batch) const;

};

/* ************************************************************************** */
/* CDomainPendingData.  */

/**
 * Keeps track of domain_new data for a pending domain_firstupdate. This is
 * serialized to the wallet so that the firstupdate can be broadcast
 * between client runs.
 */
class CDomainPendingData
{
private:
    CScript toAddress;
    uint256 hex;
    uint160 rand;
    valtype vchData;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE (*(CScriptBase*)(&toAddress));
        READWRITE(hex);
        READWRITE(rand);
        READWRITE(vchData);
    }

    inline const std::string getToAddress() { return EncodeDestination(toAddress);}
    inline const std::string getHex() { return hex.GetHex(); }
    inline const std::string getRand() { return rand.GetHex(); }
    inline const std::string getData() { return ValtypeToString(vchData); }

    inline void setToAddress(const std::string &strToAddress) {
        toAddress = GetScriptForDestination(DecodeDestination(strToAddress));
    }
    inline void setHex(const std::string &strHex) { hex.SetHex(strHex); }
    inline void setRand(const std::string &strRand) { rand.SetHex(strRand); }
    inline void setData(const std::string &strData) { vchData = ValtypeFromString(strData); }
};

#endif // H_BITCOIN_DOMAINS_COMMON