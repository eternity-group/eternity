// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ETERNITYNODE_SYNC_H
#define ETERNITYNODE_SYNC_H

#include "chain.h"
#include "net.h"

#include <univalue.h>

class CEternitynodeSync;

static const int ETERNITYNODE_SYNC_FAILED          = -1;
static const int ETERNITYNODE_SYNC_INITIAL         = 0;
static const int ETERNITYNODE_SYNC_SPORKS          = 1;
static const int ETERNITYNODE_SYNC_LIST            = 2;
static const int ETERNITYNODE_SYNC_ENW             = 3;
static const int ETERNITYNODE_SYNC_GOVERNANCE      = 4;
static const int ETERNITYNODE_SYNC_GOVOBJ          = 10;
static const int ETERNITYNODE_SYNC_GOVOBJ_VOTE     = 11;
static const int ETERNITYNODE_SYNC_FINISHED        = 999;

static const int ETERNITYNODE_SYNC_TICK_SECONDS    = 6;
static const int ETERNITYNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int ETERNITYNODE_SYNC_ENOUGH_PEERS    = 6;

extern CEternitynodeSync eternitynodeSync;

//
// CEternitynodeSync : Sync eternitynode assets in stages
//

class CEternitynodeSync
{
private:
    // Keep track of current asset
    int nRequestedEternitynodeAssets;
    // Count peers we've requested the asset from
    int nRequestedEternitynodeAttempt;

    // Time when current eternitynode asset sync started
    int64_t nTimeAssetSyncStarted;

    // Last time when we received some eternitynode asset ...
    int64_t nTimeLastEternitynodeList;
    int64_t nTimeLastPaymentVote;
    int64_t nTimeLastGovernanceItem;
    // ... or failed
    int64_t nTimeLastFailure;

    // How many times we failed
    int nCountFailures;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    bool CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes = false);
    void Fail();
    void ClearFulfilledRequests();

public:
    CEternitynodeSync() { Reset(); }

    void AddedEternitynodeList() { nTimeLastEternitynodeList = GetTime(); }
    void AddedPaymentVote() { nTimeLastPaymentVote = GetTime(); }
    void AddedGovernanceItem() { nTimeLastGovernanceItem = GetTime(); };

    void SendGovernanceSyncRequest(CNode* pnode);

    bool IsFailed() { return nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_FAILED; }
    bool IsBlockchainSynced(bool fBlockAccepted = false);
    bool IsEternitynodeListSynced() { return nRequestedEternitynodeAssets > ETERNITYNODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedEternitynodeAssets > ETERNITYNODE_SYNC_ENW; }
    bool IsSynced() { return nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedEternitynodeAssets; }
    int GetAttempt() { return nRequestedEternitynodeAttempt; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
