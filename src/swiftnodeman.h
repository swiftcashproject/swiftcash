// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018-2020 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTNODEMAN_H
#define SWIFTNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "swiftnode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define SWIFTNODES_DUMP_SECONDS (15 * 60)
#define SWIFTNODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CSwiftnodeMan;

extern CSwiftnodeMan mnodeman;
void DumpSwiftnodes();

/** Access to the MN database (mncache.dat)
 */
class CSwiftnodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CSwiftnodeDB();
    bool Write(const CSwiftnodeMan& mnodemanToSave);
    ReadResult Read(CSwiftnodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CSwiftnodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CSwiftnode> vSwiftnodes;
    // who's asked for the Swiftnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForSwiftnodeList;
    // who we asked for the Swiftnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForSwiftnodeList;
    // which Swiftnodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForSwiftnodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CSwiftnodeBroadcast> mapSeenSwiftnodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CSwiftnodePing> mapSeenSwiftnodePing;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vSwiftnodes);
        READWRITE(mAskedUsForSwiftnodeList);
        READWRITE(mWeAskedForSwiftnodeList);
        READWRITE(mWeAskedForSwiftnodeListEntry);

        READWRITE(mapSeenSwiftnodeBroadcast);
        READWRITE(mapSeenSwiftnodePing);
    }

    CSwiftnodeMan();
    CSwiftnodeMan(CSwiftnodeMan& other);

    /// Add an entry
    bool Add(CSwiftnode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Swiftnodes
    void Check();

    /// Check all Swiftnodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Swiftnode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CSwiftnode* Find(const CScript& payee);
    CSwiftnode* Find(const CTxIn& vin);
    CSwiftnode* Find(const CPubKey& pubKeySwiftnode);

    // Count addresses
    int Count(const CService& addr);

    /// Find an entry in the swiftnode list that is next to be paid
    CSwiftnode* GetNextSwiftnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CSwiftnode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CSwiftnode* GetCurrentSwiftNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CSwiftnode> GetFullSwiftnodeVector()
    {
        Check();
        return vSwiftnodes;
    }

    std::vector<pair<int, CSwiftnode> > GetSwiftnodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetSwiftnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CSwiftnode* GetSwiftnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Swiftnodes
    int size() { return vSwiftnodes.size(); }

    /// Return the number of Swiftnodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update swiftnode list and maps using provided CSwiftnodeBroadcast
    void UpdateSwiftnodeList(CSwiftnodeBroadcast mnb);
};

#endif
