// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PROSPERITYNODE_SYNC_H
#define PROSPERITYNODE_SYNC_H

#define PROSPERITYNODE_SYNC_INITIAL 0
#define PROSPERITYNODE_SYNC_SPORKS 1
#define PROSPERITYNODE_SYNC_LIST 2
#define PROSPERITYNODE_SYNC_MNW 3
#define PROSPERITYNODE_SYNC_BUDGET 4
#define PROSPERITYNODE_SYNC_BUDGET_PROP 10
#define PROSPERITYNODE_SYNC_BUDGET_FIN 11
#define PROSPERITYNODE_SYNC_FAILED 998
#define PROSPERITYNODE_SYNC_FINISHED 999

#define PROSPERITYNODE_SYNC_TIMEOUT 5
#define PROSPERITYNODE_SYNC_THRESHOLD 2

class CProsperitynodeSync;
extern CProsperitynodeSync prosperitynodeSync;

//
// CProsperitynodeSync : Sync prosperitynode assets in stages
//

class CProsperitynodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastProsperitynodeList;
    int64_t lastProsperitynodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumProsperitynodeList;
    int sumProsperitynodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countProsperitynodeList;
    int countProsperitynodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedProsperitynodeAssets;
    int RequestedProsperitynodeAttempt;

    // Time when current prosperitynode asset sync started
    int64_t nAssetSyncStarted;

    CProsperitynodeSync();

    void AddedProsperitynodeList(uint256 hash);
    void AddedProsperitynodeWinner(uint256 hash);
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
    bool IsProsperitynodeListSynced() { return RequestedProsperitynodeAssets > PROSPERITYNODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
