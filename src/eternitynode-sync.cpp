// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeeternitynode.h"
#include "checkpoints.h"
#include "governance.h"
#include "main.h"
#include "eternitynode.h"
#include "eternitynode-payments.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

class CEternitynodeSync;
CEternitynodeSync eternitynodeSync;

bool CEternitynodeSync::CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes)
{
    CNodeStateStats stats;
    if(!GetNodeStateStats(pnode->id, stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if(pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if(fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CEternitynodeSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                        pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        } else {
            LogPrintf("CEternitynodeSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                        pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        }
        return false;
    }
    else if(pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer announced more headers than we have blocks currently
        LogPrintf("CEternitynodeSync::CheckNodeHeight -- skipping peer, who announced more headers than we have blocks currently, nHeight=%d, nSyncHeight=%d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->id);
        return false;
    }

    return true;
}

bool CEternitynodeSync::IsBlockchainSynced(bool fBlockAccepted)
{
    static bool fBlockchainSynced = false;
    static int64_t nTimeLastProcess = GetTime();
    static int nSkipped = 0;
    static bool fFirstBlockAccepted = false;

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if(GetTime() - nTimeLastProcess > 60*60) {
        Reset();
        fBlockchainSynced = false;
    }

    if(!pCurrentBlockIndex || !pindexBestHeader || fImporting || fReindex) return false;

    if(fBlockAccepted) {
        // this should be only triggered while we are still syncing
        if(!IsSynced()) {
            // we are trying to download smth, reset blockchain sync status
            if(fDebug) LogPrintf("CEternitynodeSync::IsBlockchainSynced -- reset\n");
            fFirstBlockAccepted = true;
            fBlockchainSynced = false;
            nTimeLastProcess = GetTime();
            return false;
        }
    } else {
        // skip if we already checked less than 1 tick ago
        if(GetTime() - nTimeLastProcess < ETERNITYNODE_SYNC_TICK_SECONDS) {
            nSkipped++;
            return fBlockchainSynced;
        }
    }

    if(fDebug) LogPrintf("CEternitynodeSync::IsBlockchainSynced -- state before check: %ssynced, skipped %d times\n", fBlockchainSynced ? "" : "not ", nSkipped);

    nTimeLastProcess = GetTime();
    nSkipped = 0;

    if(fBlockchainSynced) return true;

    if(fCheckpointsEnabled && pCurrentBlockIndex->nHeight < Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints()))
        return false;

    std::vector<CNode*> vNodesCopy = CopyNodeVector();

    // We have enough peers and assume most of them are synced
    if(vNodesCopy.size() >= ETERNITYNODE_SYNC_ENOUGH_PEERS) {
        // Check to see how many of our peers are (almost) at the same height as we are
        int nNodesAtSameHeight = 0;
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            // Make sure this peer is presumably at the same height
            if(!CheckNodeHeight(pnode)) continue;
            nNodesAtSameHeight++;
            // if we have decent number of such peers, most likely we are synced now
            if(nNodesAtSameHeight >= ETERNITYNODE_SYNC_ENOUGH_PEERS) {
                LogPrintf("CEternitynodeSync::IsBlockchainSynced -- found enough peers on the same height as we are, done\n");
                fBlockchainSynced = true;
                ReleaseNodeVector(vNodesCopy);
                return true;
            }
        }
    }
    ReleaseNodeVector(vNodesCopy);

    // wait for at least one new block to be accepted
    if(!fFirstBlockAccepted) return false;

    // same as !IsInitialBlockDownload() but no cs_main needed here
    int64_t nMaxBlockTime = std::max(pCurrentBlockIndex->GetBlockTime(), pindexBestHeader->GetBlockTime());
    fBlockchainSynced = pindexBestHeader->nHeight - pCurrentBlockIndex->nHeight < 24 * 6 &&
                        GetTime() - nMaxBlockTime < Params().MaxTipAge();

    return fBlockchainSynced;
}

void CEternitynodeSync::Fail()
{
    nTimeLastFailure = GetTime();
    nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_FAILED;
}

void CEternitynodeSync::Reset()
{
    nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_INITIAL;
    nRequestedEternitynodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastEternitynodeList = GetTime();
    nTimeLastPaymentVote = GetTime();
    nTimeLastGovernanceItem = GetTime();
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CEternitynodeSync::GetAssetName()
{
    switch(nRequestedEternitynodeAssets)
    {
        case(ETERNITYNODE_SYNC_INITIAL):      return "ETERNITYNODE_SYNC_INITIAL";
        case(ETERNITYNODE_SYNC_SPORKS):       return "ETERNITYNODE_SYNC_SPORKS";
        case(ETERNITYNODE_SYNC_LIST):         return "ETERNITYNODE_SYNC_LIST";
        case(ETERNITYNODE_SYNC_ENW):          return "ETERNITYNODE_SYNC_ENW";
        case(ETERNITYNODE_SYNC_GOVERNANCE):   return "ETERNITYNODE_SYNC_GOVERNANCE";
        case(ETERNITYNODE_SYNC_FAILED):       return "ETERNITYNODE_SYNC_FAILED";
        case ETERNITYNODE_SYNC_FINISHED:      return "ETERNITYNODE_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CEternitynodeSync::SwitchToNextAsset()
{
    switch(nRequestedEternitynodeAssets)
    {
        case(ETERNITYNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(ETERNITYNODE_SYNC_INITIAL):
            ClearFulfilledRequests();
            nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_SPORKS;
            LogPrintf("CEternitynodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(ETERNITYNODE_SYNC_SPORKS):
            nTimeLastEternitynodeList = GetTime();
            nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_LIST;
            LogPrintf("CEternitynodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(ETERNITYNODE_SYNC_LIST):
            nTimeLastPaymentVote = GetTime();
            nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_ENW;
            LogPrintf("CEternitynodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(ETERNITYNODE_SYNC_ENW):
            nTimeLastGovernanceItem = GetTime();
            nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_GOVERNANCE;
            LogPrintf("CEternitynodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(ETERNITYNODE_SYNC_GOVERNANCE):
            LogPrintf("CEternitynodeSync::SwitchToNextAsset -- Sync has finished\n");
            nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
            //try to activate our eternitynode if possible
            activeEternitynode.ManageState();

            TRY_LOCK(cs_vNodes, lockRecv);
            if(!lockRecv) return;

            BOOST_FOREACH(CNode* pnode, vNodes) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            }

            break;
    }
    nRequestedEternitynodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CEternitynodeSync::GetSyncStatus()
{
    switch (eternitynodeSync.nRequestedEternitynodeAssets) {
        case ETERNITYNODE_SYNC_INITIAL:       return _("Synchronization pending...");
        case ETERNITYNODE_SYNC_SPORKS:        return _("Synchronizing sporks...");
        case ETERNITYNODE_SYNC_LIST:          return _("Synchronizing eternitynodes...");
        case ETERNITYNODE_SYNC_ENW:           return _("Synchronizing eternitynode payments...");
        case ETERNITYNODE_SYNC_GOVERNANCE:    return _("Synchronizing governance objects...");
        case ETERNITYNODE_SYNC_FAILED:        return _("Synchronization failed");
        case ETERNITYNODE_SYNC_FINISHED:      return _("Synchronization finished");
        default:                            return "";
    }
}

void CEternitynodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CEternitynodeSync::ClearFulfilledRequests()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "eternitynode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "eternitynode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "governance-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    }
}

void CEternitynodeSync::ProcessTick()
{
    static int nTick = 0;
    if(nTick++ % ETERNITYNODE_SYNC_TICK_SECONDS != 0) return;
    if(!pCurrentBlockIndex) return;

    //the actual count of eternitynodes we have currently
    int nMnCount = mnodeman.CountEternitynodes();

    if(fDebug) LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nMnCount %d\n", nTick, nMnCount);

    // RESET SYNCING INCASE OF FAILURE
    {
        if(IsSynced()) {
            /*
                Resync if we lost all eternitynodes from sleep/wake or failed to sync originally
            */
            if(nMnCount == 0) {
                LogPrintf("CEternitynodeSync::ProcessTick -- WARNING: not enough data, restarting sync\n");
                Reset();
            } else {
                std::vector<CNode*> vNodesCopy = CopyNodeVector();
                governance.RequestGovernanceObjectVotes(vNodesCopy);
                ReleaseNodeVector(vNodesCopy);
                return;
            }
        }

        //try syncing again
        if(IsFailed()) {
            if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
                Reset();
            }
            return;
        }
    }

    // INITIAL SYNC SETUP / LOG REPORTING
    double nSyncProgress = double(nRequestedEternitynodeAttempt + (nRequestedEternitynodeAssets - 1) * 8) / (8*4);
    LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d nRequestedEternitynodeAttempt %d nSyncProgress %f\n", nTick, nRequestedEternitynodeAssets, nRequestedEternitynodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if(Params().NetworkIDString() != CBaseChainParams::REGTEST &&
            !IsBlockchainSynced() && nRequestedEternitynodeAssets > ETERNITYNODE_SYNC_SPORKS)
    {
        LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d nRequestedEternitynodeAttempt %d -- blockchain is not synced yet\n", nTick, nRequestedEternitynodeAssets, nRequestedEternitynodeAttempt);
        nTimeLastEternitynodeList = GetTime();
        nTimeLastPaymentVote = GetTime();
        nTimeLastGovernanceItem = GetTime();
        return;
    }

    if(nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_INITIAL ||
        (nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_SPORKS && IsBlockchainSynced()))
    {
        SwitchToNextAsset();
    }

    std::vector<CNode*> vNodesCopy = CopyNodeVector();

    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "eternitynode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "eternitynode" connection
        // initialted from another node, so skip it too.
        if(pnode->fEternitynode || (fEternityNode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if(nRequestedEternitynodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if(nRequestedEternitynodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if(nRequestedEternitynodeAttempt < 6) {
                int nMnCount = mnodeman.CountEternitynodes();
                pnode->PushMessage(NetMsgType::ETERNITYNODEPAYMENTSYNC, nMnCount); //sync payment votes
                SendGovernanceSyncRequest(pnode);
            } else {
                nRequestedEternitynodeAssets = ETERNITYNODE_SYNC_FINISHED;
            }
            nRequestedEternitynodeAttempt++;
            ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CEternitynodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC (we skip this mode now)

            if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                pnode->PushMessage(NetMsgType::GETSPORKS);
                LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d -- requesting sporks from peer %d\n", nTick, nRequestedEternitynodeAssets, pnode->id);
                continue; // always get sporks first, switch to the next node without waiting for the next tick
            }

            // MNLIST : SYNC ETERNITYNODE LIST FROM OTHER CONNECTED CLIENTS

            if(nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_LIST) {
                LogPrint("eternitynode", "CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d nTimeLastEternitynodeList %lld GetTime() %lld diff %lld\n", nTick, nRequestedEternitynodeAssets, nTimeLastEternitynodeList, GetTime(), GetTime() - nTimeLastEternitynodeList);
                // check for timeout first
                if(nTimeLastEternitynodeList < GetTime() - ETERNITYNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d -- timeout\n", nTick, nRequestedEternitynodeAssets);
                    if (nRequestedEternitynodeAttempt == 0) {
                        LogPrintf("CEternitynodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without eternitynode list, fail here and try later
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "eternitynode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "eternitynode-list-sync");

                if (pnode->nVersion < enpayments.GetMinEternitynodePaymentsProto()) continue;
                nRequestedEternitynodeAttempt++;

                mnodeman.DsegUpdate(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC ETERNITYNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_ENW) {
                LogPrint("enpayments", "CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedEternitynodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);
                // check for timeout first
                // This might take a lot longer than ETERNITYNODE_SYNC_TIMEOUT_SECONDS minutes due to new blocks,
                // but that should be OK and it should timeout eventually.
                if(nTimeLastPaymentVote < GetTime() - ETERNITYNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d -- timeout\n", nTick, nRequestedEternitynodeAssets);
                    if (nRequestedEternitynodeAttempt == 0) {
                        LogPrintf("CEternitynodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if enpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nRequestedEternitynodeAttempt > 1 && enpayments.IsEnoughData()) {
                    LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d -- found enough data\n", nTick, nRequestedEternitynodeAssets);
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "eternitynode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "eternitynode-payment-sync");

                if(pnode->nVersion < enpayments.GetMinEternitynodePaymentsProto()) continue;
                nRequestedEternitynodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::ETERNITYNODEPAYMENTSYNC, enpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                enpayments.RequestLowDataPaymentBlocks(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // GOVOBJ : SYNC GOVERNANCE ITEMS FROM OUR PEERS

            if(nRequestedEternitynodeAssets == ETERNITYNODE_SYNC_GOVERNANCE) {
                LogPrint("gobject", "CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d nTimeLastGovernanceItem %lld GetTime() %lld diff %lld\n", nTick, nRequestedEternitynodeAssets, nTimeLastGovernanceItem, GetTime(), GetTime() - nTimeLastGovernanceItem);

                // check for timeout first
                if(GetTime() - nTimeLastGovernanceItem > ETERNITYNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d -- timeout\n", nTick, nRequestedEternitynodeAssets);
                    if(nRequestedEternitynodeAttempt == 0) {
                        LogPrintf("CEternitynodeSync::ProcessTick -- WARNING: failed to sync %s\n", GetAssetName());
                        // it's kind of ok to skip this for now, hopefully we'll catch up later?
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request obj sync once from each peer, then request votes on per-obj basis
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "governance-sync")) {
                    int nObjsLeftToAsk = governance.RequestGovernanceObjectVotes(pnode);
                    static int64_t nTimeNoObjectsLeft = 0;
                    // check for data
                    if(nObjsLeftToAsk == 0) {
                        static int nLastTick = 0;
                        static int nLastVotes = 0;
                        if(nTimeNoObjectsLeft == 0) {
                            // asked all objects for votes for the first time
                            nTimeNoObjectsLeft = GetTime();
                        }
                        // make sure the condition below is checked only once per tick
                        if(nLastTick == nTick) continue;
                        if(GetTime() - nTimeNoObjectsLeft > ETERNITYNODE_SYNC_TIMEOUT_SECONDS &&
                            governance.GetVoteCount() - nLastVotes < std::max(int(0.0001 * nLastVotes), ETERNITYNODE_SYNC_TICK_SECONDS)
                        ) {
                            // We already asked for all objects, waited for ETERNITYNODE_SYNC_TIMEOUT_SECONDS
                            // after that and less then 0.01% or ETERNITYNODE_SYNC_TICK_SECONDS
                            // (i.e. 1 per second) votes were recieved during the last tick.
                            // We can be pretty sure that we are done syncing.
                            LogPrintf("CEternitynodeSync::ProcessTick -- nTick %d nRequestedEternitynodeAssets %d -- asked for all objects, nothing to do\n", nTick, nRequestedEternitynodeAssets);
                            // reset nTimeNoObjectsLeft to be able to use the same condition on resync
                            nTimeNoObjectsLeft = 0;
                            SwitchToNextAsset();
                            ReleaseNodeVector(vNodesCopy);
                            return;
                        }
                        nLastTick = nTick;
                        nLastVotes = governance.GetVoteCount();
                    }
                    continue;
                }
                netfulfilledman.AddFulfilledRequest(pnode->addr, "governance-sync");

                if (pnode->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) continue;
                nRequestedEternitynodeAttempt++;

                SendGovernanceSyncRequest(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
    // looped through all nodes, release them
    ReleaseNodeVector(vNodesCopy);
}

void CEternitynodeSync::SendGovernanceSyncRequest(CNode* pnode)
{
    if(pnode->nVersion >= GOVERNANCE_FILTER_PROTO_VERSION) {
        CBloomFilter filter;
        filter.clear();

        pnode->PushMessage(NetMsgType::MNGOVERNANCESYNC, uint256(), filter);
    }
    else {
        pnode->PushMessage(NetMsgType::MNGOVERNANCESYNC, uint256());
    }
}

void CEternitynodeSync::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
}
