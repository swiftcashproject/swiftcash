// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018-2020 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "swiftnodeman.h"
#include "activeswiftnode.h"
#include "swiftnode-payments.h"
#include "swiftnode-helpers.h"
#include "addrman.h"
#include "swiftnode.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > SWIFTNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Swiftnode manager */
CSwiftnodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CSwiftnode>& t1,
        const pair<int64_t, CSwiftnode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CSwiftnodeDB
//

CSwiftnodeDB::CSwiftnodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "SwiftnodeCache";
}

bool CSwiftnodeDB::Write(const CSwiftnodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssSwiftnodes(SER_DISK, CLIENT_VERSION);
    ssSwiftnodes << strMagicMessage;                   // swiftnode cache file specific magic message
    ssSwiftnodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssSwiftnodes << mnodemanToSave;
    uint256 hash = Hash(ssSwiftnodes.begin(), ssSwiftnodes.end());
    ssSwiftnodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssSwiftnodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("swiftnode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("swiftnode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CSwiftnodeDB::ReadResult CSwiftnodeDB::Read(CSwiftnodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssSwiftnodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssSwiftnodes.begin(), ssSwiftnodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (swiftnode cache file specific magic message) and ..

        ssSwiftnodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid swiftnode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssSwiftnodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CSwiftnodeMan object
        ssSwiftnodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("swiftnode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("swiftnode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("swiftnode","Swiftnode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("swiftnode","Swiftnode manager - result:\n");
        LogPrint("swiftnode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpSwiftnodes()
{
    int64_t nStart = GetTimeMillis();

    CSwiftnodeDB mndb;
    CSwiftnodeMan tempMnodeman;

    LogPrint("swiftnode","Verifying mncache.dat format...\n");
    CSwiftnodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CSwiftnodeDB::FileError)
        LogPrint("swiftnode","Missing swiftnode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CSwiftnodeDB::Ok) {
        LogPrint("swiftnode","Error reading mncache.dat: ");
        if (readResult == CSwiftnodeDB::IncorrectFormat)
            LogPrint("swiftnode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("swiftnode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("swiftnode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("swiftnode","Swiftnode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CSwiftnodeMan::CSwiftnodeMan()
{
}

bool CSwiftnodeMan::Add(CSwiftnode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CSwiftnode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("swiftnode", "CSwiftnodeMan: Adding new Swiftnode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vSwiftnodes.push_back(mn);
        return true;
    }

    return false;
}

void CSwiftnodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForSwiftnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForSwiftnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("swiftnode", "CSwiftnodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + SWIFTNODE_MIN_MNP_SECONDS;
    mWeAskedForSwiftnodeListEntry[vin.prevout] = askAgain;
}

void CSwiftnodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        mn.Check();
    }
}

void CSwiftnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CSwiftnode>::iterator it = vSwiftnodes.begin();
    while (it != vSwiftnodes.end()) {
        if ((*it).activeState == CSwiftnode::SWIFTNODE_REMOVE ||
            (*it).activeState == CSwiftnode::SWIFTNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CSwiftnode::SWIFTNODE_EXPIRED) ||
            (*it).protocolVersion < swiftnodePayments.GetMinSwiftnodePaymentsProto()) {
            LogPrint("swiftnode", "CSwiftnodeMan: Removing inactive Swiftnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            map<uint256, CSwiftnodeBroadcast>::iterator it3 = mapSeenSwiftnodeBroadcast.begin();
            while (it3 != mapSeenSwiftnodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    swiftnodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenSwiftnodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this swiftnode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForSwiftnodeListEntry.begin();
            while (it2 != mWeAskedForSwiftnodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForSwiftnodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vSwiftnodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Swiftnode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForSwiftnodeList.begin();
    while (it1 != mAskedUsForSwiftnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForSwiftnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Swiftnode list
    it1 = mWeAskedForSwiftnodeList.begin();
    while (it1 != mWeAskedForSwiftnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForSwiftnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Swiftnodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForSwiftnodeListEntry.begin();
    while (it2 != mWeAskedForSwiftnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForSwiftnodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenSwiftnodeBroadcast
    map<uint256, CSwiftnodeBroadcast>::iterator it3 = mapSeenSwiftnodeBroadcast.begin();
    while (it3 != mapSeenSwiftnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (SWIFTNODE_REMOVAL_SECONDS * 2)) {
            mapSeenSwiftnodeBroadcast.erase(it3++);
            swiftnodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenSwiftnodePing
    map<uint256, CSwiftnodePing>::iterator it4 = mapSeenSwiftnodePing.begin();
    while (it4 != mapSeenSwiftnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (SWIFTNODE_REMOVAL_SECONDS * 2)) {
            mapSeenSwiftnodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CSwiftnodeMan::Clear()
{
    LOCK(cs);
    vSwiftnodes.clear();
    mAskedUsForSwiftnodeList.clear();
    mWeAskedForSwiftnodeList.clear();
    mWeAskedForSwiftnodeListEntry.clear();
    mapSeenSwiftnodeBroadcast.clear();
    mapSeenSwiftnodePing.clear();
}

int CSwiftnodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nSwiftnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nSwiftnode_Age = 0;

    for (CSwiftnode& mn : vSwiftnodes) {
        if (mn.protocolVersion < nMinProtocol)
            continue; // Skip obsolete versions

        if (IsSporkActive (SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT)) {
            nSwiftnode_Age = GetAdjustedTime() - mn.sigTime;
            if (nSwiftnode_Age < nSwiftnode_Min_Age)
                continue; // Skip swiftnodes younger than (default) 8000 sec (MUST be > SWIFTNODE_REMOVAL_SECONDS)
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled swiftnodes

        nStable_size++;
    }

    return nStable_size;
}

int CSwiftnodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? swiftnodePayments.GetMinSwiftnodePaymentsProto() : protocolVersion;

    for (CSwiftnode& mn : vSwiftnodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CSwiftnodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? swiftnodePayments.GetMinSwiftnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CSwiftnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForSwiftnodeList.find(pnode->addr);
            if (it != mWeAskedForSwiftnodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("swiftnode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + SWIFTNODES_DSEG_SECONDS;
    mWeAskedForSwiftnodeList[pnode->addr] = askAgain;
}

CSwiftnode* CSwiftnodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CSwiftnode* CSwiftnodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CSwiftnode* CSwiftnodeMan::Find(const CPubKey& pubKeySwiftnode)
{
    LOCK(cs);

    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        if (mn.pubKeySwiftnode == pubKeySwiftnode)
            return &mn;
    }
    return NULL;
}

int CSwiftnodeMan::Count(const CService& addr)
{
    LOCK(cs);
    int count = 0;

    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        if (mn.addr.ToString() == addr.ToString())
            count++;
    }
    return count;
}

//
// Deterministically select the oldest/best swiftnode to pay on the network
//
CSwiftnode* CSwiftnodeMan::GetNextSwiftnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CSwiftnode* pBestSwiftnode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecSwiftnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    for (CSwiftnode& mn : vSwiftnodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        //check protocol version
        if (mn.protocolVersion < swiftnodePayments.GetMinSwiftnodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (swiftnodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * Params().TargetSpacing()) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are swiftnodes
        if (mn.GetSwiftnodeInputAge() < nMnCount) continue;

        vecSwiftnodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecSwiftnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextSwiftnodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecSwiftnodeLastPaid.rbegin(), vecSwiftnodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecSwiftnodeLastPaid) {
        CSwiftnode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestSwiftnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestSwiftnode;
}

CSwiftnode* CSwiftnodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? swiftnodePayments.GetMinSwiftnodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("swiftnode", "CSwiftnodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("swiftnode", "CSwiftnodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    for (CSwiftnode& mn : vSwiftnodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        for (CTxIn& usedVin : vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CSwiftnode* CSwiftnodeMan::GetCurrentSwiftNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CSwiftnode* winner = NULL;

    // scan for winner
    for (CSwiftnode& mn : vSwiftnodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Swiftnode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CSwiftnodeMan::GetSwiftnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecSwiftnodeScores;
    int64_t nSwiftnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nSwiftnode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    for (CSwiftnode& mn : vSwiftnodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("swiftnode","Skipping Swiftnode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT)) {
            nSwiftnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nSwiftnode_Age) < nSwiftnode_Min_Age) {
                if (fDebug) LogPrint("swiftnode","Skipping just activated Swiftnode. Age: %ld\n", nSwiftnode_Age);
                continue;                                                   // Skip swiftnodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecSwiftnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecSwiftnodeScores.rbegin(), vecSwiftnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    for (PAIRTYPE(int64_t, CTxIn) & s : vecSwiftnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout)
            return rank;
    }

    return -1;
}

std::vector<pair<int, CSwiftnode> > CSwiftnodeMan::GetSwiftnodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CSwiftnode> > vecSwiftnodeScores;
    std::vector<pair<int, CSwiftnode> > vecSwiftnodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecSwiftnodeRanks;

    // scan for winner
    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecSwiftnodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecSwiftnodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecSwiftnodeScores.rbegin(), vecSwiftnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CSwiftnode) & s, vecSwiftnodeScores) {
        rank++;
        vecSwiftnodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecSwiftnodeRanks;
}

CSwiftnode* CSwiftnodeMan::GetSwiftnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecSwiftnodeScores;

    // scan for winner
    BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecSwiftnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecSwiftnodeScores.rbegin(), vecSwiftnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecSwiftnodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CSwiftnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Swiftnode related functionality
    if (!swiftnodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Swiftnode Broadcast
        CSwiftnodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenSwiftnodeBroadcast.count(mnb.GetHash())) { //seen
            swiftnodeSync.AddedSwiftnodeList(mnb.GetHash());
            return;
        }
        mapSeenSwiftnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Swiftnode
        //  - this is expensive, so it's only done once per Swiftnode
        if (!swiftnodeSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrintf("CSwiftnodeMan::ProcessMessage() : mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            swiftnodeSync.AddedSwiftnodeList(mnb.GetHash());
        } else {
            LogPrint("swiftnode","mnb - Rejected Swiftnode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //Swiftnode Ping
        CSwiftnodePing mnp;
        vRecv >> mnp;

        LogPrint("swiftnode", "mnp - Swiftnode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenSwiftnodePing.count(mnp.GetHash())) return; //seen
        mapSeenSwiftnodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Swiftnode list
            CSwiftnode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a swiftnode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Swiftnode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForSwiftnodeList.find(pfrom->addr);
                if (i != mAskedUsForSwiftnodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        LogPrintf("CSwiftnodeMan::ProcessMessage() : dseg - peer already asked me for the list\n");
                        Misbehaving(pfrom->GetId(), 34);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + SWIFTNODES_DSEG_SECONDS;
                mAskedUsForSwiftnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CSwiftnode& mn, vSwiftnodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("swiftnode", "dseg - Sending Swiftnode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CSwiftnodeBroadcast mnb = CSwiftnodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_SWIFTNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenSwiftnodeBroadcast.count(hash)) mapSeenSwiftnodeBroadcast.insert(make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("swiftnode", "dseg - Sent 1 Swiftnode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", SWIFTNODE_SYNC_LIST, nInvCount);
            LogPrint("swiftnode", "dseg - Sent %d Swiftnode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
}

void CSwiftnodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CSwiftnode>::iterator it = vSwiftnodes.begin();
    while (it != vSwiftnodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("swiftnode", "CSwiftnodeMan: Removing Swiftnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vSwiftnodes.erase(it);
            break;
        }
        ++it;
    }
}

void CSwiftnodeMan::UpdateSwiftnodeList(CSwiftnodeBroadcast mnb)
{
    mapSeenSwiftnodePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenSwiftnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    swiftnodeSync.AddedSwiftnodeList(mnb.GetHash());

    LogPrint("swiftnode","CSwiftnodeMan::UpdateSwiftnodeList() -- swiftnode=%s\n", mnb.vin.prevout.ToString());

    CSwiftnode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CSwiftnode mn(mnb);
        Add(mn);
    } else {
    	pmn->UpdateFromNewBroadcast(mnb);
    }
}

std::string CSwiftnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Swiftnodes: " << (int)vSwiftnodes.size() << ", peers who asked us for Swiftnode list: " << (int)mAskedUsForSwiftnodeList.size() << ", peers we asked for Swiftnode list: " << (int)mWeAskedForSwiftnodeList.size() << ", entries in Swiftnode list we asked for: " << (int)mWeAskedForSwiftnodeListEntry.size();

    return info.str();
}
