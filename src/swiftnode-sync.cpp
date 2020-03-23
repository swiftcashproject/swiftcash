// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activeswiftnode.h"
#include "swiftnode-sync.h"
#include "swiftnode-payments.h"
#include "swiftnode-budget.h"
#include "swiftnode.h"
#include "swiftnodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CSwiftnodeSync;
CSwiftnodeSync swiftnodeSync;

CSwiftnodeSync::CSwiftnodeSync()
{
    Reset();
}

bool CSwiftnodeSync::IsSynced()
{
    return RequestedSwiftnodeAssets == SWIFTNODE_SYNC_FINISHED;
}

bool CSwiftnodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 5 * 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CSwiftnodeSync::Reset()
{
    lastSwiftnodeList = 0;
    lastSwiftnodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumSwiftnodeList = 0;
    sumSwiftnodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countSwiftnodeList = 0;
    countSwiftnodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedSwiftnodeAssets = SWIFTNODE_SYNC_INITIAL;
    RequestedSwiftnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CSwiftnodeSync::AddedSwiftnodeList(uint256 hash)
{
    if (mnodeman.mapSeenSwiftnodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < SWIFTNODE_SYNC_THRESHOLD) {
            lastSwiftnodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastSwiftnodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CSwiftnodeSync::AddedSwiftnodeWinner(uint256 hash)
{
    if (swiftnodePayments.mapSwiftnodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < SWIFTNODE_SYNC_THRESHOLD) {
            lastSwiftnodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastSwiftnodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CSwiftnodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenSwiftnodeBudgetProposals.count(hash) || budget.mapSeenSwiftnodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < SWIFTNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CSwiftnodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CSwiftnodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CSwiftnodeSync::GetNextAsset()
{
    switch (RequestedSwiftnodeAssets) {
    case (SWIFTNODE_SYNC_INITIAL):
    case (SWIFTNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_SPORKS;
        break;
    case (SWIFTNODE_SYNC_SPORKS):
        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_LIST;
        break;
    case (SWIFTNODE_SYNC_LIST):
        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_MNW;
        break;
    case (SWIFTNODE_SYNC_MNW):
        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_BUDGET;
        break;
    case (SWIFTNODE_SYNC_BUDGET):
        LogPrintf("CSwiftnodeSync::GetNextAsset - Sync has finished\n");
        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_FINISHED;
        break;
    }
    RequestedSwiftnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CSwiftnodeSync::GetSyncStatus()
{
    switch (swiftnodeSync.RequestedSwiftnodeAssets) {
    case SWIFTNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case SWIFTNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case SWIFTNODE_SYNC_LIST:
        return _("Synchronizing swiftnodes...");
    case SWIFTNODE_SYNC_MNW:
        return _("Synchronizing swiftnode winners...");
    case SWIFTNODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case SWIFTNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case SWIFTNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CSwiftnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedSwiftnodeAssets >= SWIFTNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (SWIFTNODE_SYNC_LIST):
            if (nItemID != RequestedSwiftnodeAssets) return;
            sumSwiftnodeList += nCount;
            countSwiftnodeList++;
            break;
        case (SWIFTNODE_SYNC_MNW):
            if (nItemID != RequestedSwiftnodeAssets) return;
            sumSwiftnodeWinner += nCount;
            countSwiftnodeWinner++;
            break;
        case (SWIFTNODE_SYNC_BUDGET_PROP):
            if (RequestedSwiftnodeAssets != SWIFTNODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (SWIFTNODE_SYNC_BUDGET_FIN):
            if (RequestedSwiftnodeAssets != SWIFTNODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
	}

        LogPrint("swiftnode", "CSwiftnodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CSwiftnodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    for (CNode* pnode : vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
        pnode->ClearFulfilledRequest("comsync");
    }
}

void CSwiftnodeSync::Process()
{
    static int tick = 0;
    static int64_t lastCheck = GetTime();

    if (tick++ % SWIFTNODE_SYNC_TIMEOUT != 0) return;

    // Check every half an hour
    if (IsSynced() && GetTime() - lastCheck > 30 * 60) {
        /*
            Resync if we lose all swiftnodes from sleep/wake or failure to sync originally
        */
	lastCheck = GetTime();
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("swiftnode", "CSwiftnodeSync::Process() - tick %d RequestedSwiftnodeAssets %d\n", tick, RequestedSwiftnodeAssets);

    if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedSwiftnodeAssets > SWIFTNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedSwiftnodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedSwiftnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedSwiftnodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync swiftnode votes
            } else {
                RequestedSwiftnodeAssets = SWIFTNODE_SYNC_FINISHED;
            }
            RequestedSwiftnodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedSwiftnodeAttempt >= 2) GetNextAsset();
            RequestedSwiftnodeAttempt++;

            return;
        }

        if (pnode->nVersion >= swiftnodePayments.GetMinSwiftnodePaymentsProto()) {
            if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_LIST) {
                LogPrint("swiftnode", "CSwiftnodeSync::Process() - lastSwiftnodeList %lld (GetTime() - SWIFTNODE_SYNC_TIMEOUT) %lld\n", lastSwiftnodeList, GetTime() - SWIFTNODE_SYNC_TIMEOUT);
                if (lastSwiftnodeList > 0 && lastSwiftnodeList < GetTime() - SWIFTNODE_SYNC_TIMEOUT * 2 && RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnsync")) continue;
                pnode->FulfilledRequest("mnsync");

                // timeout
                if (lastSwiftnodeList == 0 &&
                    (RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > SWIFTNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CSwiftnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_FAILED;
                        RequestedSwiftnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedSwiftnodeAttempt++;
                return;
            }

            if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_MNW) {
                if (lastSwiftnodeWinner > 0 && lastSwiftnodeWinner < GetTime() - SWIFTNODE_SYNC_TIMEOUT * 2 && RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if (lastSwiftnodeWinner == 0 &&
                    (RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > SWIFTNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CSwiftnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedSwiftnodeAssets = SWIFTNODE_SYNC_FAILED;
                        RequestedSwiftnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedSwiftnodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedSwiftnodeAssets == SWIFTNODE_SYNC_BUDGET) {

                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - SWIFTNODE_SYNC_TIMEOUT * 2 && RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD) {

                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our swiftnode if possible
                    activeSwiftnode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > SWIFTNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeSwiftnode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedSwiftnodeAttempt >= SWIFTNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync swiftnode votes
                RequestedSwiftnodeAttempt++;

                return;
            }

        }
    }
}
