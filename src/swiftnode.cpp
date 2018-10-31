// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "swiftnode.h"
#include "addrman.h"
#include "swiftnodeman.h"
#include "swiftnode-payments.h"
#include "swiftnode-helpers.h"
#include "sync.h"
#include "util.h"
#include "init.h"
#include "wallet.h"
#include "activeswiftnode.h"

#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenSwiftnodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CSwiftnode::CSwiftnode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeySwiftnode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = SWIFTNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CSwiftnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

CSwiftnode::CSwiftnode(const CSwiftnode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeySwiftnode = other.pubKeySwiftnode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nLastDsee = other.nLastDsee;   // temporary, do not save. Remove after migration to v12
    nLastDseep = other.nLastDseep; // temporary, do not save. Remove after migration to v12
}

CSwiftnode::CSwiftnode(const CSwiftnodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeySwiftnode = mnb.pubKeySwiftnode;
    sig = mnb.sig;
    activeState = SWIFTNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = mnb.protocolVersion;
    nLastDsq = mnb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

//
// When a new swiftnode broadcast is sent, update our information
//
bool CSwiftnode::UpdateFromNewBroadcast(CSwiftnodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeySwiftnode = mnb.pubKeySwiftnode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CSwiftnodePing() || (mnb.lastPing != CSwiftnodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenSwiftnodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Swiftnode depending on how close its hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CSwiftnode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrint("swiftnode","CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CSwiftnode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < SWIFTNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == SWIFTNODE_VIN_SPENT) return;

    if (!IsValidNetAddr()) {
        activeState = SWIFTNODE_POSE_BAN;
        return;
    }

    if (!IsPingedWithin(SWIFTNODE_REMOVAL_SECONDS)) {
        activeState = SWIFTNODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(SWIFTNODE_EXPIRATION_SECONDS)) {
        activeState = SWIFTNODE_EXPIRED;
        return;
    }

    if (lastPing.sigTime - sigTime < SWIFTNODE_MIN_MNP_SECONDS){
    	activeState = SWIFTNODE_PRE_ENABLED;
    	return;
    }

    if (!unitTest) {
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut((SWIFTNODE_COLLATERAL-0.01) * COIN, swiftnodeSigner.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = SWIFTNODE_VIN_SPENT;
                return;
            }
        }
    }

    activeState = SWIFTNODE_ENABLED; // OK
}

int64_t CSwiftnode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CSwiftnode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    // CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    // ss << vin;
    // ss << sigTime;
    // uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 1 minute
    // int64_t nOffset = hash.GetCompact(false) % Params().TargetSpacing();

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();
    int nMnCount = mnodeman.CountEnabled() * 2.5;
    int n = 0;

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (swiftnodePayments.mapSwiftnodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (swiftnodePayments.mapSwiftnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime; // + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CSwiftnode::GetStatus()
{
    switch (activeState) {
    case CSwiftnode::SWIFTNODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CSwiftnode::SWIFTNODE_ENABLED:
        return "ENABLED";
    case CSwiftnode::SWIFTNODE_EXPIRED:
        return "EXPIRED";
    case CSwiftnode::SWIFTNODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CSwiftnode::SWIFTNODE_VIN_SPENT:
        return "VIN_SPENT";
    case CSwiftnode::SWIFTNODE_REMOVE:
        return "REMOVE";
    case CSwiftnode::SWIFTNODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CSwiftnode::SWIFTNODE_POSE_BAN:
        return "POSE_BAN";
    case CSwiftnode::SWIFTNODE_POS_ERROR:
        return "POS_ERROR";
    default:
        return "UNKNOWN";
    }
}

bool CSwiftnode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
           (addr.IsIPv4() && IsReachable(addr) && addr.IsRoutable());
}

CSwiftnodeBroadcast::CSwiftnodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeySwiftnode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = SWIFTNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CSwiftnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CSwiftnodeBroadcast::CSwiftnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeySwiftnodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeySwiftnode = pubKeySwiftnodeNew;
    sig = std::vector<unsigned char>();
    activeState = SWIFTNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CSwiftnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CSwiftnodeBroadcast::CSwiftnodeBroadcast(const CSwiftnode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeySwiftnode = mn.pubKeySwiftnode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CSwiftnodeBroadcast::Create(std::string strService, std::string strKeySwiftnode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CSwiftnodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeySwiftnodeNew;
    CKey keySwiftnodeNew;

    //need correct blocks to send ping
    if (!fOffline && !swiftnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Swiftnode";
        LogPrint("swiftnode","CSwiftnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!swiftnodeSigner.GetKeysFromSecret(strKeySwiftnode, keySwiftnodeNew, pubKeySwiftnodeNew)) {
        strErrorRet = strprintf("Invalid swiftnode key %s", strKeySwiftnode);
        LogPrint("swiftnode","CSwiftnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetSwiftnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for swiftnode %s", strTxHash, strOutputIndex, strService);
        LogPrint("swiftnode","CSwiftnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    // The service needs the correct default port to work properly
    if (!CheckDefaultPort(strService, strErrorRet, "CSwiftnodeBroadcast::Create"))
        return false;

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keySwiftnodeNew, pubKeySwiftnodeNew, strErrorRet, mnbRet);
}

bool CSwiftnodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keySwiftnodeNew, CPubKey pubKeySwiftnodeNew, std::string& strErrorRet, CSwiftnodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("swiftnode", "CSwiftnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeySwiftnodeNew.GetID() = %s\n",
        CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
        pubKeySwiftnodeNew.GetID().ToString());

    CSwiftnodePing mnp(txin);
    if (!mnp.Sign(keySwiftnodeNew, pubKeySwiftnodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, swiftnode=%s", txin.prevout.hash.ToString());
        LogPrint("swiftnode","CSwiftnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CSwiftnodeBroadcast();
        return false;
    }

    mnbRet = CSwiftnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeySwiftnodeNew, PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr()) {
         strErrorRet = strprintf("Invalid IP address %s, swiftnode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
         LogPrint("swiftnode","CSwiftnodeBroadcast::Create -- %s\n", strErrorRet);
         mnbRet = CSwiftnodeBroadcast();
         return false;
    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, swiftnode=%s", txin.prevout.hash.ToString());
        LogPrint("swiftnode","CSwiftnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CSwiftnodeBroadcast();
        return false;
    }

    return true;
}

bool CSwiftnodeBroadcast::CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext)
{
    CService service = CService(strService);
    int nDefaultPort = Params().GetDefaultPort();

    if (service.GetPort() != nDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for swiftnode %s, only %d is supported on %s-net.",
                                service.GetPort(), strService, nDefaultPort, Params().NetworkIDString());
        LogPrint("swiftnode", "%s - %s\n", strContext, strErrorRet);
        return false;
    }

    return true;
}

bool CSwiftnodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure addr is valid
    if (!IsValidNetAddr()) {
        LogPrint("swiftnode", "mnb - Invalid addr, rejected: swiftnode=%s addr=%s\n",  vin.prevout.hash.ToString(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("swiftnode","mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    // incorrect ping or its sigTime
    if (lastPing == CSwiftnodePing() || !lastPing.CheckAndUpdate(nDos, false, true))
        return false;

    if (protocolVersion < swiftnodePayments.GetMinSwiftnodePaymentsProto()) {
        LogPrint("swiftnode","mnb - ignoring outdated Swiftnode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrint("swiftnode","mnb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeySwiftnode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrint("swiftnode","mnb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("swiftnode","mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!swiftnodeSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetStrMessage(), errorMessage)) {
        nDos = 100;
        return error("CSwiftnodeBroadcast::CheckAndUpdate - Got bad Swiftnode address signature : %s", errorMessage);
    }

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 8544) return false;
    } else if (addr.GetPort() == 8544)
        return false;

    //search existing Swiftnode list, this is where we update existing Swiftnodes with new mnb broadcasts
    CSwiftnode* pmn = mnodeman.Find(vin);

    // no such swiftnode, nothing to update
    if (pmn == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    // (mapSeenSwiftnodeBroadcast in CSwiftnodeMan::ProcessMessage should filter legit duplicates)
    if (pmn->sigTime >= sigTime)
        return error("CSwiftnodeBroadcast::CheckAndUpdate - Bad sigTime %d for Swiftnode %20s %105s (existing broadcast is at %d)",
					  sigTime, addr.ToString(), vin.ToString(), pmn->sigTime);

    // swiftnode is not enabled yet/already, nothing to update
    if (!pmn->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(SWIFTNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("swiftnode","mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        swiftnodeSync.AddedSwiftnodeList(GetHash());
    }

    return true;
}

bool CSwiftnodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a swiftnode with the same vin (i.e. already activated) and this mnb is ours (matches our Swiftnode privkey)
    // so nothing to do here for us
    if (fSwiftNode && vin.prevout == activeSwiftnode.vin.prevout && pubKeySwiftnode == activeSwiftnode.pubKeySwiftnode)
        return true;

    // incorrect ping or its sigTime
    if (lastPing == CSwiftnodePing() || !lastPing.CheckAndUpdate(nDoS, false, true)) return false;

    // search existing Swiftnode list
    CSwiftnode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this swiftnode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut((SWIFTNODE_COLLATERAL-0.01) * COIN, swiftnodeSigner.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            mnodeman.mapSeenSwiftnodeBroadcast.erase(GetHash());
            swiftnodeSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("swiftnode", "mnb - Accepted Swiftnode entry\n");

    if (GetInputAge(vin) < SWIFTNODE_MIN_CONFIRMATIONS) {
        LogPrint("swiftnode","mnb - Input must have at least %d confirmations\n", SWIFTNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenSwiftnodeBroadcast.erase(GetHash());
        swiftnodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when SWIFT collateral tx got SWIFTNODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 PIVX tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + SWIFTNODE_MIN_CONFIRMATIONS - 1]; // block where tx got SWIFTNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("swiftnode","mnb - Bad sigTime %d for Swiftnode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), SWIFTNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("swiftnode","mnb - Got NEW Swiftnode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CSwiftnode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Swiftnode privkey, then we've been remotely activated
    if (pubKeySwiftnode == activeSwiftnode.pubKeySwiftnode && protocolVersion == PROTOCOL_VERSION) {
        activeSwiftnode.EnableHotColdSwiftNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CSwiftnodeBroadcast::Relay()
{
    CInv inv(MSG_SWIFTNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CSwiftnodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;
    sigTime = GetAdjustedTime();

    std::string strMessage = GetStrMessage();

    if (!swiftnodeSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress))
        return error("CSwiftnodeBroadcast::Sign() - Error: %s", errorMessage);

    if (!swiftnodeSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage))
        return error("CSwiftnodeBroadcast::Sign() - Error: %s", errorMessage);

    return true;
}

bool CSwiftnodeBroadcast::VerifySignature()
{
    std::string errorMessage;

    if (!swiftnodeSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetStrMessage(), errorMessage))
        return error("CSwiftnodeBroadcast::VerifySignature() - Error: %s", errorMessage);

    return true;
}

std::string CSwiftnodeBroadcast::GetStrMessage()
{
    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeySwiftnode.begin(), pubKeySwiftnode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);
    return strMessage;
}

CSwiftnodePing::CSwiftnodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CSwiftnodePing::CSwiftnodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CSwiftnodePing::Sign(CKey& keySwiftnode, CPubKey& pubKeySwiftnode)
{
    std::string errorMessage;
    std::string strSwiftNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!swiftnodeSigner.SignMessage(strMessage, errorMessage, vchSig, keySwiftnode)) {
        LogPrint("swiftnode","CSwiftnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!swiftnodeSigner.VerifyMessage(pubKeySwiftnode, vchSig, strMessage, errorMessage)) {
        LogPrint("swiftnode","CSwiftnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CSwiftnodePing::VerifySignature(CPubKey& pubKeySwiftnode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string errorMessage = "";

    if (!swiftnodeSigner.VerifyMessage(pubKeySwiftnode, vchSig, strMessage, errorMessage)){
        nDos = 33;
        return error("CSwiftnodePing::VerifySignature - Got bad Swiftnode ping signature %s Error: %s", vin.ToString(), errorMessage);
    }
    return true;
}

bool CSwiftnodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("swiftnode","CSwiftnodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("swiftnode","CSwiftnodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if (fCheckSigTimeOnly) {
        CSwiftnode* pmn = mnodeman.Find(vin);
        if (pmn) return VerifySignature(pmn->pubKeySwiftnode, nDos);
        return true;
    }

    LogPrint("swiftnode", "CSwiftnodePing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this Swiftnode
    CSwiftnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= swiftnodePayments.GetMinSwiftnodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled() && pmn->activeState != CSwiftnode::SWIFTNODE_PRE_ENABLED) return false;

        // LogPrint("swiftnode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this swiftnode or
        // last ping was more than SWIFTNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(SWIFTNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        	if (!VerifySignature(pmn->pubKeySwiftnode, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("swiftnode","CSwiftnodePing::CheckAndUpdate - Swiftnode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Swiftnode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("swiftnode","CSwiftnodePing::CheckAndUpdate - Swiftnode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenSwiftnodeBroadcast.lastPing is probably outdated, so we'll update it
            CSwiftnodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenSwiftnodeBroadcast.count(hash)) {
                mnodeman.mapSeenSwiftnodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("swiftnode", "CSwiftnodePing::CheckAndUpdate - Swiftnode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("swiftnode", "CSwiftnodePing::CheckAndUpdate - Swiftnode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("swiftnode", "CSwiftnodePing::CheckAndUpdate - Couldn't find compatible Swiftnode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CSwiftnodePing::Relay()
{
    CInv inv(MSG_SWIFTNODE_PING, GetHash());
    RelayInv(inv);
}
