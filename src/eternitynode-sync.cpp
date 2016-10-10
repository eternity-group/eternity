// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "activeeternitynode.h"
#include "eternitynode-sync.h"
#include "eternitynode-payments.h"
#include "eternitynode-evolution.h"
#include "eternitynode.h"
#include "eternitynodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"

class CEternitynodeSync;
CEternitynodeSync eternitynodeSync;

CEternitynodeSync::CEternitynodeSync()
{
    Reset();
}

bool CEternitynodeSync::IsSynced()
{
    return RequestedEternitynodeAssets == ETERNITYNODE_SYNC_FINISHED;
}

bool CEternitynodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if(GetTime() - lastProcess > 60*60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if(fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if(!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if(pindex == NULL) return false;


    if(pindex->nTime + 60*60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CEternitynodeSync::Reset()
{   
    lastEternitynodeList = 0;
    lastEternitynodeWinner = 0;
    lastEvolutionItem = 0;
    mapSeenSyncENB.clear();
    mapSeenSyncENW.clear();
    mapSeenSyncEvolution.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumEternitynodeList = 0;
    sumEternitynodeWinner = 0;
    sumEvolutionItemProp = 0;
    sumEvolutionItemFin = 0;
    countEternitynodeList = 0;
    countEternitynodeWinner = 0;
    countEvolutionItemProp = 0;
    countEvolutionItemFin = 0;
    RequestedEternitynodeAssets = ETERNITYNODE_SYNC_INITIAL;
    RequestedEternitynodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CEternitynodeSync::AddedEternitynodeList(uint256 hash)
{
    if(enodeman.mapSeenEternitynodeBroadcast.count(hash)) {
        if(mapSeenSyncENB[hash] < ETERNITYNODE_SYNC_THRESHOLD) {
            lastEternitynodeList = GetTime();
            mapSeenSyncENB[hash]++;
        }
    } else {
        lastEternitynodeList = GetTime();
        mapSeenSyncENB.insert(make_pair(hash, 1));
    }
}

void CEternitynodeSync::AddedEternitynodeWinner(uint256 hash)
{
    if(eternitynodePayments.mapEternitynodePayeeVotes.count(hash)) {
        if(mapSeenSyncENW[hash] < ETERNITYNODE_SYNC_THRESHOLD) {
            lastEternitynodeWinner = GetTime();
            mapSeenSyncENW[hash]++;
        }
    } else {
        lastEternitynodeWinner = GetTime();
        mapSeenSyncENW.insert(make_pair(hash, 1));
    }
}

void CEternitynodeSync::AddedEvolutionItem(uint256 hash)
{
    if(evolution.mapSeenEternitynodeEvolutionProposals.count(hash) || evolution.mapSeenEternitynodeEvolutionVotes.count(hash) ||
            evolution.mapSeenFinalizedEvolutions.count(hash) || evolution.mapSeenFinalizedEvolutionVotes.count(hash)) {
        if(mapSeenSyncEvolution[hash] < ETERNITYNODE_SYNC_THRESHOLD) {
            lastEvolutionItem = GetTime();
            mapSeenSyncEvolution[hash]++;
        }
    } else {
        lastEvolutionItem = GetTime();
        mapSeenSyncEvolution.insert(make_pair(hash, 1));
    }
}

bool CEternitynodeSync::IsEvolutionPropEmpty()
{
    return sumEvolutionItemProp==0 && countEvolutionItemProp>0;
}

bool CEternitynodeSync::IsEvolutionFinEmpty()
{
    return sumEvolutionItemFin==0 && countEvolutionItemFin>0;
}

void CEternitynodeSync::GetNextAsset()
{
    switch(RequestedEternitynodeAssets)
    {
        case(ETERNITYNODE_SYNC_INITIAL):
        case(ETERNITYNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
            ClearFulfilledRequest();
            RequestedEternitynodeAssets = ETERNITYNODE_SYNC_SPORKS;
            break;
        case(ETERNITYNODE_SYNC_SPORKS):
            RequestedEternitynodeAssets = ETERNITYNODE_SYNC_LIST;
            break;
        case(ETERNITYNODE_SYNC_LIST):
            RequestedEternitynodeAssets = ETERNITYNODE_SYNC_ENW;
            break;
        case(ETERNITYNODE_SYNC_ENW):
            RequestedEternitynodeAssets = ETERNITYNODE_SYNC_EVOLUTION;
            break;
        case(ETERNITYNODE_SYNC_EVOLUTION):
            LogPrintf("CEternitynodeSync::GetNextAsset - Sync has finished\n");
            RequestedEternitynodeAssets = ETERNITYNODE_SYNC_FINISHED;
            break;
    }
    RequestedEternitynodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CEternitynodeSync::GetSyncStatus()
{
    switch (eternitynodeSync.RequestedEternitynodeAssets) {
        case ETERNITYNODE_SYNC_INITIAL: return _("Synchronization pending...");
        case ETERNITYNODE_SYNC_SPORKS: return _("Synchronizing sporks...");
        case ETERNITYNODE_SYNC_LIST: return _("Synchronizing Eternitynodes...");
        case ETERNITYNODE_SYNC_ENW: return _("Synchronizing Eternitynode winners...");
        case ETERNITYNODE_SYNC_EVOLUTION: return _("Synchronizing Evolutions...");
        case ETERNITYNODE_SYNC_FAILED: return _("Synchronization failed");
        case ETERNITYNODE_SYNC_FINISHED: return _("Synchronization finished");
    }
    return "";
}

void CEternitynodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if(RequestedEternitynodeAssets >= ETERNITYNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch(nItemID)
        {
            case(ETERNITYNODE_SYNC_LIST):
                if(nItemID != RequestedEternitynodeAssets) return;
                sumEternitynodeList += nCount;
                countEternitynodeList++;
                break;
            case(ETERNITYNODE_SYNC_ENW):
                if(nItemID != RequestedEternitynodeAssets) return;
                sumEternitynodeWinner += nCount;
                countEternitynodeWinner++;
                break;
            case(ETERNITYNODE_SYNC_EVOLUTION_PROP):
                if(RequestedEternitynodeAssets != ETERNITYNODE_SYNC_EVOLUTION) return;
                sumEvolutionItemProp += nCount;
                countEvolutionItemProp++;
                break;
            case(ETERNITYNODE_SYNC_EVOLUTION_FIN):
                if(RequestedEternitynodeAssets != ETERNITYNODE_SYNC_EVOLUTION) return;
                sumEvolutionItemFin += nCount;
                countEvolutionItemFin++;
                break;
        }
        
        LogPrintf("CEternitynodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CEternitynodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CEternitynodeSync::Process()
{
    static int tick = 0;

    if(tick++ % ETERNITYNODE_SYNC_TIMEOUT != 0) return;

    if(IsSynced()) {
        /* 
            Resync if we lose all eternitynodes from sleep/wake or failure to sync originally
        */
        if(enodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if(RequestedEternitynodeAssets == ETERNITYNODE_SYNC_FAILED && lastFailure + (1*60) < GetTime()) {
        Reset();
    } else if (RequestedEternitynodeAssets == ETERNITYNODE_SYNC_FAILED) {
        return;
    }

    if(fDebug) LogPrintf("CEternitynodeSync::Process() - tick %d RequestedEternitynodeAssets %d\n", tick, RequestedEternitynodeAssets);

    if(RequestedEternitynodeAssets == ETERNITYNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if(Params().NetworkID() != CBaseChainParams::REGTEST &&
            !IsBlockchainSynced() && RequestedEternitynodeAssets > ETERNITYNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(Params().NetworkID() == CBaseChainParams::REGTEST){
            if(RequestedEternitynodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if(RequestedEternitynodeAttempt < 4) {
                enodeman.DsegUpdate(pnode); 
            } else if(RequestedEternitynodeAttempt < 6) {
                int nMnCount = enodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("envs", n); //sync eternitynode votes
            } else {
                RequestedEternitynodeAssets = ETERNITYNODE_SYNC_FINISHED;
            }
            RequestedEternitynodeAttempt++;
            return;
        }

        //set to synced
        if(RequestedEternitynodeAssets == ETERNITYNODE_SYNC_SPORKS){
            if(pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if(RequestedEternitynodeAttempt >= 2) GetNextAsset();
            RequestedEternitynodeAttempt++;
            
            return;
        }

        if (pnode->nVersion >= eternitynodePayments.GetMinEternitynodePaymentsProto()) {

            if(RequestedEternitynodeAssets == ETERNITYNODE_SYNC_LIST) {
                if(fDebug) LogPrintf("CEternitynodeSync::Process() - lastEternitynodeList %lld (GetTime() - ETERNITYNODE_SYNC_TIMEOUT) %lld\n", lastEternitynodeList, GetTime() - ETERNITYNODE_SYNC_TIMEOUT);
                if(lastEternitynodeList > 0 && lastEternitynodeList < GetTime() - ETERNITYNODE_SYNC_TIMEOUT*2 && RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if(pnode->HasFulfilledRequest("mnsync")) continue;
                pnode->FulfilledRequest("mnsync");

                // timeout
                if(lastEternitynodeList == 0 &&
                (RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > ETERNITYNODE_SYNC_TIMEOUT*5)) {
                    if(IsSporkActive(SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CEternitynodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedEternitynodeAssets = ETERNITYNODE_SYNC_FAILED;
                        RequestedEternitynodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if(RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD*3) return;

                enodeman.DsegUpdate(pnode);
                RequestedEternitynodeAttempt++;
                return;
            }

            if(RequestedEternitynodeAssets == ETERNITYNODE_SYNC_ENW) {
                if(lastEternitynodeWinner > 0 && lastEternitynodeWinner < GetTime() - ETERNITYNODE_SYNC_TIMEOUT*2 && RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if(pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if(lastEternitynodeWinner == 0 &&
                (RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > ETERNITYNODE_SYNC_TIMEOUT*5)) {
                    if(IsSporkActive(SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CEternitynodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedEternitynodeAssets = ETERNITYNODE_SYNC_FAILED;
                        RequestedEternitynodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if(RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD*3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if(pindexPrev == NULL) return;

                int nMnCount = enodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedEternitynodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= MIN_EVOLUTION_PEER_PROTO_VERSION) {

            if(RequestedEternitynodeAssets == ETERNITYNODE_SYNC_EVOLUTION){
                //we'll start rejecting votes if we accidentally get set as synced too soon
                if(lastEvolutionItem > 0 && lastEvolutionItem < GetTime() - ETERNITYNODE_SYNC_TIMEOUT*2 && RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD){ //hasn't received a new item in the last five seconds, so we'll move to the
                    //LogPrintf("CEternitynodeSync::Process - HasNextFinalizedEvolution %d nCountFailures %d IsEvolutionPropEmpty %d\n", evolution.HasNextFinalizedEvolution(), nCountFailures, IsEvolutionPropEmpty());
                    //if(evolution.HasNextFinalizedEvolution() || nCountFailures >= 2 || IsEvolutionPropEmpty()) {
                        GetNextAsset();

                        //try to activate our eternitynode if possible
                        activeEternitynode.ManageStatus();
                    // } else { //we've failed to sync, this state will reject the next evolution block
                    //     LogPrintf("CEternitynodeSync::Process - ERROR - Sync has failed, will retry later\n");
                    //     RequestedEternitynodeAssets = ETERNITYNODE_SYNC_FAILED;
                    //     RequestedEternitynodeAttempt = 0;
                    //     lastFailure = GetTime();
                    //     nCountFailures++;
                    // }
                    return;
                }

                // timeout
                if(lastEvolutionItem == 0 &&
                (RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD*3 || GetTime() - nAssetSyncStarted > ETERNITYNODE_SYNC_TIMEOUT*5)) {
                    // maybe there is no evolutions at all, so just finish syncing
                    GetNextAsset();
                    activeEternitynode.ManageStatus();
                    return;
                }

                if(pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if(RequestedEternitynodeAttempt >= ETERNITYNODE_SYNC_THRESHOLD*3) return;

                uint256 n = 0;
                pnode->PushMessage("envs", n); //sync eternitynode votes
                RequestedEternitynodeAttempt++;
                
                return;
            }

        }
    }
}
