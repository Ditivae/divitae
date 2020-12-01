// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activeprosperitynode.h"
#include "prosperitynode-sync.h"
#include "prosperitynode-payments.h"
#include "prosperitynode-budget.h"
#include "prosperitynode.h"
#include "prosperitynodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CProsperitynodeSync;
CProsperitynodeSync prosperitynodeSync;

CProsperitynodeSync::CProsperitynodeSync()
{
    Reset();
}

bool CProsperitynodeSync::IsSynced()
{
    return RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_FINISHED;
}

bool CProsperitynodeSync::IsBlockchainSynced()
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


    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CProsperitynodeSync::Reset()
{
    lastProsperitynodeList = 0;
    lastProsperitynodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumProsperitynodeList = 0;
    sumProsperitynodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countProsperitynodeList = 0;
    countProsperitynodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_INITIAL;
    RequestedProsperitynodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CProsperitynodeSync::AddedProsperitynodeList(uint256 hash)
{
    if (mnodeman.mapSeenProsperitynodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < PROSPERITYNODE_SYNC_THRESHOLD) {
            lastProsperitynodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastProsperitynodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CProsperitynodeSync::AddedProsperitynodeWinner(uint256 hash)
{
    if (prosperitynodePayments.mapProsperitynodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < PROSPERITYNODE_SYNC_THRESHOLD) {
            lastProsperitynodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastProsperitynodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CProsperitynodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenProsperitynodeBudgetProposals.count(hash) || budget.mapSeenProsperitynodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < PROSPERITYNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CProsperitynodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CProsperitynodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CProsperitynodeSync::GetNextAsset()
{
    switch (RequestedProsperitynodeAssets) {
    case (PROSPERITYNODE_SYNC_INITIAL):
    case (PROSPERITYNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_SPORKS;
        break;
    case (PROSPERITYNODE_SYNC_SPORKS):
        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_LIST;
        break;
    case (PROSPERITYNODE_SYNC_LIST):
        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_MNW;
        break;
    case (PROSPERITYNODE_SYNC_MNW):
        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_BUDGET;
        break;
    case (PROSPERITYNODE_SYNC_BUDGET):
        LogPrintf("CProsperitynodeSync::GetNextAsset - Sync has finished\n");
        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_FINISHED;
        break;
    }
    RequestedProsperitynodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CProsperitynodeSync::GetSyncStatus()
{
    switch (prosperitynodeSync.RequestedProsperitynodeAssets) {
    case PROSPERITYNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case PROSPERITYNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case PROSPERITYNODE_SYNC_LIST:
        return _("Synchronizing prosperitynodes...");
    case PROSPERITYNODE_SYNC_MNW:
        return _("Synchronizing prosperitynode winners...");
    case PROSPERITYNODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case PROSPERITYNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case PROSPERITYNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CProsperitynodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedProsperitynodeAssets >= PROSPERITYNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (PROSPERITYNODE_SYNC_LIST):
            if (nItemID != RequestedProsperitynodeAssets) return;
            sumProsperitynodeList += nCount;
            countProsperitynodeList++;
            break;
        case (PROSPERITYNODE_SYNC_MNW):
            if (nItemID != RequestedProsperitynodeAssets) return;
            sumProsperitynodeWinner += nCount;
            countProsperitynodeWinner++;
            break;
        case (PROSPERITYNODE_SYNC_BUDGET_PROP):
            if (RequestedProsperitynodeAssets != PROSPERITYNODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (PROSPERITYNODE_SYNC_BUDGET_FIN):
            if (RequestedProsperitynodeAssets != PROSPERITYNODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        }

        LogPrint("prosperitynode", "CProsperitynodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CProsperitynodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("fnsync");
        pnode->ClearFulfilledRequest("fnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CProsperitynodeSync::Process()
{
    static int tick = 0;

    if (tick++ % PROSPERITYNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /*
            Resync if we lose all prosperitynodes from sleep/wake or failure to sync originally
        */
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("prosperitynode", "CProsperitynodeSync::Process() - tick %d RequestedProsperitynodeAssets %d\n", tick, RequestedProsperitynodeAssets);

    if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedProsperitynodeAssets > PROSPERITYNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedProsperitynodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedProsperitynodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedProsperitynodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("fnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("fnvs", n); //sync prosperitynode votes
            } else {
                RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_FINISHED;
            }
            RequestedProsperitynodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedProsperitynodeAttempt >= 2) GetNextAsset();
            RequestedProsperitynodeAttempt++;

            return;
        }

        if (pnode->nVersion >= prosperitynodePayments.GetMinProsperitynodePaymentsProto()) {
            if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_LIST) {
                LogPrint("prosperitynode", "CProsperitynodeSync::Process() - lastProsperitynodeList %lld (GetTime() - PROSPERITYNODE_SYNC_TIMEOUT) %lld\n", lastProsperitynodeList, GetTime() - PROSPERITYNODE_SYNC_TIMEOUT);
                if (lastProsperitynodeList > 0 && lastProsperitynodeList < GetTime() - PROSPERITYNODE_SYNC_TIMEOUT * 2 && RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("fnsync")) continue;
                pnode->FulfilledRequest("fnsync");

                // timeout
                if (lastProsperitynodeList == 0 &&
                    (RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PROSPERITYNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_PROSPERITYNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CProsperitynodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_FAILED;
                        RequestedProsperitynodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedProsperitynodeAttempt++;
                return;
            }

            if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_MNW) {
                if (lastProsperitynodeWinner > 0 && lastProsperitynodeWinner < GetTime() - PROSPERITYNODE_SYNC_TIMEOUT * 2 && RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("fnwsync")) continue;
                pnode->FulfilledRequest("fnwsync");

                // timeout
                if (lastProsperitynodeWinner == 0 &&
                    (RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PROSPERITYNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_PROSPERITYNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CProsperitynodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedProsperitynodeAssets = PROSPERITYNODE_SYNC_FAILED;
                        RequestedProsperitynodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("fnget", nMnCount); //sync payees
                RequestedProsperitynodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedProsperitynodeAssets == PROSPERITYNODE_SYNC_BUDGET) {

                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - PROSPERITYNODE_SYNC_TIMEOUT * 2 && RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD) {

                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our prosperitynode if possible
                    activeProsperitynode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PROSPERITYNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeProsperitynode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedProsperitynodeAttempt >= PROSPERITYNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("fnvs", n); //sync prosperitynode votes
                RequestedProsperitynodeAttempt++;

                return;
            }
        }
    }
}
