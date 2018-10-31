// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTNODE_SYNC_H
#define SWIFTNODE_SYNC_H

#define SWIFTNODE_SYNC_INITIAL 0
#define SWIFTNODE_SYNC_SPORKS 1
#define SWIFTNODE_SYNC_LIST 2
#define SWIFTNODE_SYNC_MNW 3
#define SWIFTNODE_SYNC_BUDGET 4
#define SWIFTNODE_SYNC_BUDGET_PROP 10
#define SWIFTNODE_SYNC_BUDGET_FIN 11
#define SWIFTNODE_SYNC_FAILED 998
#define SWIFTNODE_SYNC_FINISHED 999

#define SWIFTNODE_SYNC_TIMEOUT 5
#define SWIFTNODE_SYNC_THRESHOLD 2

class CSwiftnodeSync;
extern CSwiftnodeSync swiftnodeSync;

//
// CSwiftnodeSync : Sync swiftnode assets in stages
//

class CSwiftnodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastSwiftnodeList;
    int64_t lastSwiftnodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumSwiftnodeList;
    int sumSwiftnodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countSwiftnodeList;
    int countSwiftnodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedSwiftnodeAssets;
    int RequestedSwiftnodeAttempt;

    // Time when current swiftnode asset sync started
    int64_t nAssetSyncStarted;

    CSwiftnodeSync();

    void AddedSwiftnodeList(uint256 hash);
    void AddedSwiftnodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    bool IsSwiftnodeListSynced() { return RequestedSwiftnodeAssets > SWIFTNODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
