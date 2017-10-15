// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeeternitynode.h"
#include "coincontrol.h"
#include "consensus/validation.h"
#include "spysend.h"
#include "governance.h"
#include "init.h"
#include "instantx.h"
#include "eternitynode-payments.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "script/sign.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"

#include <boost/lexical_cast.hpp>

int nSpySendRounds = DEFAULT_SPYSEND_ROUNDS;
int nSpySendAmount = DEFAULT_SPYSEND_AMOUNT;
int nLiquidityProvider = DEFAULT_SPYSEND_LIQUIDITY;
bool fEnableSpySend = false;
bool fSpySendMultiSession = DEFAULT_SPYSEND_MULTISESSION;

CSpysendPool spySendPool;
CSpySendSigner spySendSigner;
std::map<uint256, CSpysendBroadcastTx> mapSpysendBroadcastTxes;
std::vector<CAmount> vecSpySendDenominations;

void CSpysendPool::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(fLiteMode) return; // ignore all Eternity related functionality
    if(!eternitynodeSync.IsBlockchainSynced()) return;

    if(strCommand == NetMsgType::DSACCEPT) {

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrintf("DSACCEPT -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            PushStatus(pfrom, STATUS_REJECTED, ERR_VERSION);
            return;
        }

        if(!fEternityNode) {
            LogPrintf("DSACCEPT -- not a Eternitynode!\n");
            PushStatus(pfrom, STATUS_REJECTED, ERR_NOT_A_MN);
            return;
        }

        if(IsSessionReady()) {
            // too many users in this session already, reject new ones
            LogPrintf("DSACCEPT -- queue is already full!\n");
            PushStatus(pfrom, STATUS_ACCEPTED, ERR_QUEUE_FULL);
            return;
        }

        int nDenom;
        CTransaction txCollateral;
        vRecv >> nDenom >> txCollateral;

        LogPrint("spysend", "DSACCEPT -- nDenom %d (%s)  txCollateral %s", nDenom, GetDenominationsToString(nDenom), txCollateral.ToString());

        CEternitynode* pmn = mnodeman.Find(activeEternitynode.vin);
        if(pmn == NULL) {
            PushStatus(pfrom, STATUS_REJECTED, ERR_MN_LIST);
            return;
        }

        if(vecSessionCollaterals.size() == 0 && pmn->nLastDsq != 0 &&
            pmn->nLastDsq + mnodeman.CountEnabled(MIN_SPYSEND_PEER_PROTO_VERSION)/5 > mnodeman.nDsqCount)
        {
            LogPrintf("DSACCEPT -- last dsq too recent, must wait: addr=%s\n", pfrom->addr.ToString());
            PushStatus(pfrom, STATUS_REJECTED, ERR_RECENT);
            return;
        }

        PoolMessage nMessageID = MSG_NOERR;

        bool fResult = nSessionID == 0  ? CreateNewSession(nDenom, txCollateral, nMessageID)
                                        : AddUserToExistingSession(nDenom, txCollateral, nMessageID);
        if(fResult) {
            LogPrintf("DSACCEPT -- is compatible, please submit!\n");
            PushStatus(pfrom, STATUS_ACCEPTED, nMessageID);
            return;
        } else {
            LogPrintf("DSACCEPT -- not compatible with existing transactions!\n");
            PushStatus(pfrom, STATUS_REJECTED, nMessageID);
            return;
        }

    } else if(strCommand == NetMsgType::DSQUEUE) {
        TRY_LOCK(cs_spysend, lockRecv);
        if(!lockRecv) return;

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrint("spysend", "DSQUEUE -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        CSpysendQueue dsq;
        vRecv >> dsq;

        // process every dsq only once
        BOOST_FOREACH(CSpysendQueue q, vecSpysendQueue) {
            if(q == dsq) {
                // LogPrint("spysend", "DSQUEUE -- %s seen\n", dsq.ToString());
                return;
            }
        }

        LogPrint("spysend", "DSQUEUE -- %s new\n", dsq.ToString());

        if(dsq.IsExpired() || dsq.nTime > GetTime() + SPYSEND_QUEUE_TIMEOUT) return;

        CEternitynode* pmn = mnodeman.Find(dsq.vin);
        if(pmn == NULL) return;

        if(!dsq.CheckSignature(pmn->pubKeyEternitynode)) {
            // we probably have outdated info
            mnodeman.AskForEN(pfrom, dsq.vin);
            return;
        }

        // if the queue is ready, submit if we can
        if(dsq.fReady) {
            if(!pSubmittedToEternitynode) return;
            if((CNetAddr)pSubmittedToEternitynode->addr != (CNetAddr)pmn->addr) {
                LogPrintf("DSQUEUE -- message doesn't match current Eternitynode: pSubmittedToEternitynode=%s, addr=%s\n", pSubmittedToEternitynode->addr.ToString(), pmn->addr.ToString());
                return;
            }

            if(nState == POOL_STATE_QUEUE) {
                LogPrint("spysend", "DSQUEUE -- SpySend queue (%s) is ready on eternitynode %s\n", dsq.ToString(), pmn->addr.ToString());
                SubmitDenominate();
            }
        } else {
            BOOST_FOREACH(CSpysendQueue q, vecSpysendQueue) {
                if(q.vin == dsq.vin) {
                    // no way same mn can send another "not yet ready" dsq this soon
                    LogPrint("spysend", "DSQUEUE -- Eternitynode %s is sending WAY too many dsq messages\n", pmn->addr.ToString());
                    return;
                }
            }

            int nThreshold = pmn->nLastDsq + mnodeman.CountEnabled(MIN_SPYSEND_PEER_PROTO_VERSION)/5;
            LogPrint("spysend", "DSQUEUE -- nLastDsq: %d  threshold: %d  nDsqCount: %d\n", pmn->nLastDsq, nThreshold, mnodeman.nDsqCount);
            //don't allow a few nodes to dominate the queuing process
            if(pmn->nLastDsq != 0 && nThreshold > mnodeman.nDsqCount) {
                LogPrint("spysend", "DSQUEUE -- Eternitynode %s is sending too many dsq messages\n", pmn->addr.ToString());
                return;
            }
            mnodeman.nDsqCount++;
            pmn->nLastDsq = mnodeman.nDsqCount;
            pmn->fAllowMixingTx = true;

            LogPrint("spysend", "DSQUEUE -- new SpySend queue (%s) from eternitynode %s\n", dsq.ToString(), pmn->addr.ToString());
            if(pSubmittedToEternitynode && pSubmittedToEternitynode->vin.prevout == dsq.vin.prevout) {
                dsq.fTried = true;
            }
            vecSpysendQueue.push_back(dsq);
            dsq.Relay();
        }

    } else if(strCommand == NetMsgType::DSVIN) {

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrintf("DSVIN -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            PushStatus(pfrom, STATUS_REJECTED, ERR_VERSION);
            return;
        }

        if(!fEternityNode) {
            LogPrintf("DSVIN -- not a Eternitynode!\n");
            PushStatus(pfrom, STATUS_REJECTED, ERR_NOT_A_MN);
            return;
        }

        //do we have enough users in the current session?
        if(!IsSessionReady()) {
            LogPrintf("DSVIN -- session not complete!\n");
            PushStatus(pfrom, STATUS_REJECTED, ERR_SESSION);
            return;
        }

        CSpySendEntry entry;
        vRecv >> entry;

        LogPrint("spysend", "DSVIN -- txCollateral %s", entry.txCollateral.ToString());

        //do we have the same denominations as the current session?
        if(!IsOutputsCompatibleWithSessionDenom(entry.vecTxDSOut)) {
            LogPrintf("DSVIN -- not compatible with existing transactions!\n");
            PushStatus(pfrom, STATUS_REJECTED, ERR_EXISTING_TX);
            return;
        }

        //check it like a transaction
        {
            CAmount nValueIn = 0;
            CAmount nValueOut = 0;

            CMutableTransaction tx;

            BOOST_FOREACH(const CTxOut txout, entry.vecTxDSOut) {
                nValueOut += txout.nValue;
                tx.vout.push_back(txout);

                if(txout.scriptPubKey.size() != 25) {
                    LogPrintf("DSVIN -- non-standard pubkey detected! scriptPubKey=%s\n", ScriptToAsmStr(txout.scriptPubKey));
                    PushStatus(pfrom, STATUS_REJECTED, ERR_NON_STANDARD_PUBKEY);
                    return;
                }
                if(!txout.scriptPubKey.IsNormalPaymentScript()) {
                    LogPrintf("DSVIN -- invalid script! scriptPubKey=%s\n", ScriptToAsmStr(txout.scriptPubKey));
                    PushStatus(pfrom, STATUS_REJECTED, ERR_INVALID_SCRIPT);
                    return;
                }
            }

            BOOST_FOREACH(const CTxIn txin, entry.vecTxDSIn) {
                tx.vin.push_back(txin);

                LogPrint("spysend", "DSVIN -- txin=%s\n", txin.ToString());

                CTransaction txPrev;
                uint256 hash;
                if(GetTransaction(txin.prevout.hash, txPrev, Params().GetConsensus(), hash, true)) {
                    if(txPrev.vout.size() > txin.prevout.n)
                        nValueIn += txPrev.vout[txin.prevout.n].nValue;
                } else {
                    LogPrintf("DSVIN -- missing input! tx=%s", tx.ToString());
                    PushStatus(pfrom, STATUS_REJECTED, ERR_MISSING_TX);
                    return;
                }
            }

            if(nValueIn > SPYSEND_POOL_MAX) {
                LogPrintf("DSVIN -- more than SpySend pool max! nValueIn: %lld, tx=%s", nValueIn, tx.ToString());
                PushStatus(pfrom, STATUS_REJECTED, ERR_MAXIMUM);
                return;
            }

            // Allow lowest denom (at max) as a a fee. Normally shouldn't happen though.
            // TODO: Or do not allow fees at all?
            if(nValueIn - nValueOut > vecSpySendDenominations.back()) {
                LogPrintf("DSVIN -- fees are too high! fees: %lld, tx=%s", nValueIn - nValueOut, tx.ToString());
                PushStatus(pfrom, STATUS_REJECTED, ERR_FEES);
                return;
            }

            {
                LOCK(cs_main);
                CValidationState validationState;
                mempool.PrioritiseTransaction(tx.GetHash(), tx.GetHash().ToString(), 1000, 0.1*COIN);
                if(!AcceptToMemoryPool(mempool, validationState, CTransaction(tx), false, NULL, false, true, true)) {
                    LogPrintf("DSVIN -- transaction not valid! tx=%s", tx.ToString());
                    PushStatus(pfrom, STATUS_REJECTED, ERR_INVALID_TX);
                    return;
                }
            }
        }

        PoolMessage nMessageID = MSG_NOERR;

        if(AddEntry(entry, nMessageID)) {
            PushStatus(pfrom, STATUS_ACCEPTED, nMessageID);
            CheckPool();
            RelayStatus(STATUS_ACCEPTED);
        } else {
            PushStatus(pfrom, STATUS_REJECTED, nMessageID);
            SetNull();
        }

    } else if(strCommand == NetMsgType::DSSTATUSUPDATE) {

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrintf("DSSTATUSUPDATE -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        if(fEternityNode) {
            // LogPrintf("DSSTATUSUPDATE -- Can't run on a Eternitynode!\n");
            return;
        }

        if(!pSubmittedToEternitynode) return;
        if((CNetAddr)pSubmittedToEternitynode->addr != (CNetAddr)pfrom->addr) {
            //LogPrintf("DSSTATUSUPDATE -- message doesn't match current Eternitynode: pSubmittedToEternitynode %s addr %s\n", pSubmittedToEternitynode->addr.ToString(), pfrom->addr.ToString());
            return;
        }

        int nMsgSessionID;
        int nMsgState;
        int nMsgEntriesCount;
        int nMsgStatusUpdate;
        int nMsgMessageID;
        vRecv >> nMsgSessionID >> nMsgState >> nMsgEntriesCount >> nMsgStatusUpdate >> nMsgMessageID;

        LogPrint("spysend", "DSSTATUSUPDATE -- nMsgSessionID %d  nMsgState: %d  nEntriesCount: %d  nMsgStatusUpdate: %d  nMsgMessageID %d\n",
                nMsgSessionID, nMsgState, nEntriesCount, nMsgStatusUpdate, nMsgMessageID);

        if(nMsgState < POOL_STATE_MIN || nMsgState > POOL_STATE_MAX) {
            LogPrint("spysend", "DSSTATUSUPDATE -- nMsgState is out of bounds: %d\n", nMsgState);
            return;
        }

        if(nMsgStatusUpdate < STATUS_REJECTED || nMsgStatusUpdate > STATUS_ACCEPTED) {
            LogPrint("spysend", "DSSTATUSUPDATE -- nMsgStatusUpdate is out of bounds: %d\n", nMsgStatusUpdate);
            return;
        }

        if(nMsgMessageID < MSG_POOL_MIN || nMsgMessageID > MSG_POOL_MAX) {
            LogPrint("spysend", "DSSTATUSUPDATE -- nMsgMessageID is out of bounds: %d\n", nMsgMessageID);
            return;
        }

        LogPrint("spysend", "DSSTATUSUPDATE -- GetMessageByID: %s\n", GetMessageByID(PoolMessage(nMsgMessageID)));

        if(!CheckPoolStateUpdate(PoolState(nMsgState), nMsgEntriesCount, PoolStatusUpdate(nMsgStatusUpdate), PoolMessage(nMsgMessageID), nMsgSessionID)) {
            LogPrint("spysend", "DSSTATUSUPDATE -- CheckPoolStateUpdate failed\n");
        }

    } else if(strCommand == NetMsgType::DSSIGNFINALTX) {

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrintf("DSSIGNFINALTX -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        if(!fEternityNode) {
            LogPrintf("DSSIGNFINALTX -- not a Eternitynode!\n");
            return;
        }

        std::vector<CTxIn> vecTxIn;
        vRecv >> vecTxIn;

        LogPrint("spysend", "DSSIGNFINALTX -- vecTxIn.size() %s\n", vecTxIn.size());

        int nTxInIndex = 0;
        int nTxInsCount = (int)vecTxIn.size();

        BOOST_FOREACH(const CTxIn txin, vecTxIn) {
            nTxInIndex++;
            if(!AddScriptSig(txin)) {
                LogPrint("spysend", "DSSIGNFINALTX -- AddScriptSig() failed at %d/%d, session: %d\n", nTxInIndex, nTxInsCount, nSessionID);
                RelayStatus(STATUS_REJECTED);
                return;
            }
            LogPrint("spysend", "DSSIGNFINALTX -- AddScriptSig() %d/%d success\n", nTxInIndex, nTxInsCount);
        }
        // all is good
        CheckPool();

    } else if(strCommand == NetMsgType::DSFINALTX) {

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrintf("DSFINALTX -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        if(fEternityNode) {
            // LogPrintf("DSFINALTX -- Can't run on a Eternitynode!\n");
            return;
        }

        if(!pSubmittedToEternitynode) return;
        if((CNetAddr)pSubmittedToEternitynode->addr != (CNetAddr)pfrom->addr) {
            //LogPrintf("DSFINALTX -- message doesn't match current Eternitynode: pSubmittedToEternitynode %s addr %s\n", pSubmittedToEternitynode->addr.ToString(), pfrom->addr.ToString());
            return;
        }

        int nMsgSessionID;
        CTransaction txNew;
        vRecv >> nMsgSessionID >> txNew;

        if(nSessionID != nMsgSessionID) {
            LogPrint("spysend", "DSFINALTX -- message doesn't match current SpySend session: nSessionID: %d  nMsgSessionID: %d\n", nSessionID, nMsgSessionID);
            return;
        }

        LogPrint("spysend", "DSFINALTX -- txNew %s", txNew.ToString());

        //check to see if input is spent already? (and probably not confirmed)
        SignFinalTransaction(txNew, pfrom);

    } else if(strCommand == NetMsgType::DSCOMPLETE) {

        if(pfrom->nVersion < MIN_SPYSEND_PEER_PROTO_VERSION) {
            LogPrintf("DSCOMPLETE -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        if(fEternityNode) {
            // LogPrintf("DSCOMPLETE -- Can't run on a Eternitynode!\n");
            return;
        }

        if(!pSubmittedToEternitynode) return;
        if((CNetAddr)pSubmittedToEternitynode->addr != (CNetAddr)pfrom->addr) {
            LogPrint("spysend", "DSCOMPLETE -- message doesn't match current Eternitynode: pSubmittedToEternitynode=%s  addr=%s\n", pSubmittedToEternitynode->addr.ToString(), pfrom->addr.ToString());
            return;
        }

        int nMsgSessionID;
        int nMsgMessageID;
        vRecv >> nMsgSessionID >> nMsgMessageID;

        if(nMsgMessageID < MSG_POOL_MIN || nMsgMessageID > MSG_POOL_MAX) {
            LogPrint("spysend", "DSCOMPLETE -- nMsgMessageID is out of bounds: %d\n", nMsgMessageID);
            return;
        }

        if(nSessionID != nMsgSessionID) {
            LogPrint("spysend", "DSCOMPLETE -- message doesn't match current SpySend session: nSessionID: %d  nMsgSessionID: %d\n", spySendPool.nSessionID, nMsgSessionID);
            return;
        }

        LogPrint("spysend", "DSCOMPLETE -- nMsgSessionID %d  nMsgMessageID %d (%s)\n", nMsgSessionID, nMsgMessageID, GetMessageByID(PoolMessage(nMsgMessageID)));

        CompletedTransaction(PoolMessage(nMsgMessageID));
    }
}

void CSpysendPool::InitDenominations()
{
    vecSpySendDenominations.clear();
    /* Denominations

        A note about convertability. Within mixing pools, each denomination
        is convertable to another.

        For example:
        1DRK+1000 == (.1DRK+100)*10
        10DRK+10000 == (1DRK+1000)*10
    */
    /* Disabled
    vecSpySendDenominations.push_back( (100      * COIN)+100000 );
    */
    vecSpySendDenominations.push_back( (10       * COIN)+10000 );
    vecSpySendDenominations.push_back( (1        * COIN)+1000 );
    vecSpySendDenominations.push_back( (.1       * COIN)+100 );
    vecSpySendDenominations.push_back( (.01      * COIN)+10 );
    /* Disabled till we need them
    vecSpySendDenominations.push_back( (.001     * COIN)+1 );
    */
}

void CSpysendPool::ResetPool()
{
    nCachedLastSuccessBlock = 0;
    txMyCollateral = CMutableTransaction();
    vecEternitynodesUsed.clear();
    UnlockCoins();
    SetNull();
}

void CSpysendPool::SetNull()
{
    // MN side
    vecSessionCollaterals.clear();

    // Client side
    nEntriesCount = 0;
    fLastEntryAccepted = false;
    pSubmittedToEternitynode = NULL;

    // Both sides
    nState = POOL_STATE_IDLE;
    nSessionID = 0;
    nSessionDenom = 0;
    vecEntries.clear();
    finalMutableTransaction.vin.clear();
    finalMutableTransaction.vout.clear();
    nTimeLastSuccessfulStep = GetTimeMillis();
}

//
// Unlock coins after mixing fails or succeeds
//
void CSpysendPool::UnlockCoins()
{
    while(true) {
        TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
        if(!lockWallet) {MilliSleep(50); continue;}
        BOOST_FOREACH(COutPoint outpoint, vecOutPointLocked)
            pwalletMain->UnlockCoin(outpoint);
        break;
    }

    vecOutPointLocked.clear();
}

std::string CSpysendPool::GetStateString() const
{
    switch(nState) {
        case POOL_STATE_IDLE:                   return "IDLE";
        case POOL_STATE_QUEUE:                  return "QUEUE";
        case POOL_STATE_ACCEPTING_ENTRIES:      return "ACCEPTING_ENTRIES";
        case POOL_STATE_SIGNING:                return "SIGNING";
        case POOL_STATE_ERROR:                  return "ERROR";
        case POOL_STATE_SUCCESS:                return "SUCCESS";
        default:                                return "UNKNOWN";
    }
}

std::string CSpysendPool::GetStatus()
{
    static int nStatusMessageProgress = 0;
    nStatusMessageProgress += 10;
    std::string strSuffix = "";

    if((pCurrentBlockIndex && pCurrentBlockIndex->nHeight - nCachedLastSuccessBlock < nMinBlockSpacing) || !eternitynodeSync.IsBlockchainSynced())
        return strAutoDenomResult;

    switch(nState) {
        case POOL_STATE_IDLE:
            return _("SpySend is idle.");
        case POOL_STATE_QUEUE:
            if(     nStatusMessageProgress % 70 <= 30) strSuffix = ".";
            else if(nStatusMessageProgress % 70 <= 50) strSuffix = "..";
            else if(nStatusMessageProgress % 70 <= 70) strSuffix = "...";
            return strprintf(_("Submitted to eternitynode, waiting in queue %s"), strSuffix);;
        case POOL_STATE_ACCEPTING_ENTRIES:
            if(nEntriesCount == 0) {
                nStatusMessageProgress = 0;
                return strAutoDenomResult;
            } else if(fLastEntryAccepted) {
                if(nStatusMessageProgress % 10 > 8) {
                    fLastEntryAccepted = false;
                    nStatusMessageProgress = 0;
                }
                return _("SpySend request complete:") + " " + _("Your transaction was accepted into the pool!");
            } else {
                if(     nStatusMessageProgress % 70 <= 40) return strprintf(_("Submitted following entries to eternitynode: %u / %d"), nEntriesCount, GetMaxPoolTransactions());
                else if(nStatusMessageProgress % 70 <= 50) strSuffix = ".";
                else if(nStatusMessageProgress % 70 <= 60) strSuffix = "..";
                else if(nStatusMessageProgress % 70 <= 70) strSuffix = "...";
                return strprintf(_("Submitted to eternitynode, waiting for more entries ( %u / %d ) %s"), nEntriesCount, GetMaxPoolTransactions(), strSuffix);
            }
        case POOL_STATE_SIGNING:
            if(     nStatusMessageProgress % 70 <= 40) return _("Found enough users, signing ...");
            else if(nStatusMessageProgress % 70 <= 50) strSuffix = ".";
            else if(nStatusMessageProgress % 70 <= 60) strSuffix = "..";
            else if(nStatusMessageProgress % 70 <= 70) strSuffix = "...";
            return strprintf(_("Found enough users, signing ( waiting %s )"), strSuffix);
        case POOL_STATE_ERROR:
            return _("SpySend request incomplete:") + " " + strLastMessage + " " + _("Will retry...");
        case POOL_STATE_SUCCESS:
            return _("SpySend request complete:") + " " + strLastMessage;
       default:
            return strprintf(_("Unknown state: id = %u"), nState);
    }
}

//
// Check the mixing progress and send client updates if a Eternitynode
//
void CSpysendPool::CheckPool()
{
    if(fEternityNode) {
        LogPrint("spysend", "CSpysendPool::CheckPool -- entries count %lu\n", GetEntriesCount());

        // If entries are full, create finalized transaction
        if(nState == POOL_STATE_ACCEPTING_ENTRIES && GetEntriesCount() >= GetMaxPoolTransactions()) {
            LogPrint("spysend", "CSpysendPool::CheckPool -- FINALIZE TRANSACTIONS\n");
            CreateFinalTransaction();
            return;
        }

        // If we have all of the signatures, try to compile the transaction
        if(nState == POOL_STATE_SIGNING && IsSignaturesComplete()) {
            LogPrint("spysend", "CSpysendPool::CheckPool -- SIGNING\n");
            CommitFinalTransaction();
            return;
        }
    }

    // reset if we're here for 10 seconds
    if((nState == POOL_STATE_ERROR || nState == POOL_STATE_SUCCESS) && GetTimeMillis() - nTimeLastSuccessfulStep >= 10000) {
        LogPrint("spysend", "CSpysendPool::CheckPool -- timeout, RESETTING\n");
        UnlockCoins();
        SetNull();
    }
}

void CSpysendPool::CreateFinalTransaction()
{
    LogPrint("spysend", "CSpysendPool::CreateFinalTransaction -- FINALIZE TRANSACTIONS\n");

    CMutableTransaction txNew;

    // make our new transaction
    for(int i = 0; i < GetEntriesCount(); i++) {
        BOOST_FOREACH(const CTxDSOut& txdsout, vecEntries[i].vecTxDSOut)
            txNew.vout.push_back(txdsout);

        BOOST_FOREACH(const CTxDSIn& txdsin, vecEntries[i].vecTxDSIn)
            txNew.vin.push_back(txdsin);
    }

    // BIP69 https://github.com/kristovatlas/bips/blob/master/bip-0069.mediawiki
    sort(txNew.vin.begin(), txNew.vin.end());
    sort(txNew.vout.begin(), txNew.vout.end());

    finalMutableTransaction = txNew;
    LogPrint("spysend", "CSpysendPool::CreateFinalTransaction -- finalMutableTransaction=%s", txNew.ToString());

    // request signatures from clients
    RelayFinalTransaction(finalMutableTransaction);
    SetState(POOL_STATE_SIGNING);
}

void CSpysendPool::CommitFinalTransaction()
{
    if(!fEternityNode) return; // check and relay final tx only on eternitynode

    CTransaction finalTransaction = CTransaction(finalMutableTransaction);
    uint256 hashTx = finalTransaction.GetHash();

    LogPrint("spysend", "CSpysendPool::CommitFinalTransaction -- finalTransaction=%s", finalTransaction.ToString());

    {
        // See if the transaction is valid
        TRY_LOCK(cs_main, lockMain);
        CValidationState validationState;
        mempool.PrioritiseTransaction(hashTx, hashTx.ToString(), 1000, 0.1*COIN);
        if(!lockMain || !AcceptToMemoryPool(mempool, validationState, finalTransaction, false, NULL, false, true, true))
        {
            LogPrintf("CSpysendPool::CommitFinalTransaction -- AcceptToMemoryPool() error: Transaction not valid\n");
            SetNull();
            // not much we can do in this case, just notify clients
            RelayCompletedTransaction(ERR_INVALID_TX);
            return;
        }
    }

    LogPrintf("CSpysendPool::CommitFinalTransaction -- CREATING DSTX\n");

    // create and sign eternitynode dstx transaction
    if(!mapSpysendBroadcastTxes.count(hashTx)) {
        CSpysendBroadcastTx dstx(finalTransaction, activeEternitynode.vin, GetAdjustedTime());
        dstx.Sign();
        mapSpysendBroadcastTxes.insert(std::make_pair(hashTx, dstx));
    }

    LogPrintf("CSpysendPool::CommitFinalTransaction -- TRANSMITTING DSTX\n");

    CInv inv(MSG_DSTX, hashTx);
    RelayInv(inv);

    // Tell the clients it was successful
    RelayCompletedTransaction(MSG_SUCCESS);

    // Randomly charge clients
    ChargeRandomFees();

    // Reset
    LogPrint("spysend", "CSpysendPool::CommitFinalTransaction -- COMPLETED -- RESETTING\n");
    SetNull();
}

//
// Charge clients a fee if they're abusive
//
// Why bother? SpySend uses collateral to ensure abuse to the process is kept to a minimum.
// The submission and signing stages are completely separate. In the cases where
// a client submits a transaction then refused to sign, there must be a cost. Otherwise they
// would be able to do this over and over again and bring the mixing to a hault.
//
// How does this work? Messages to Eternitynodes come in via NetMsgType::DSVIN, these require a valid collateral
// transaction for the client to be able to enter the pool. This transaction is kept by the Eternitynode
// until the transaction is either complete or fails.
//
void CSpysendPool::ChargeFees()
{
    if(!fEternityNode) return;

    //we don't need to charge collateral for every offence.
    if(GetRandInt(100) > 33) return;

    std::vector<CTransaction> vecOffendersCollaterals;

    if(nState == POOL_STATE_ACCEPTING_ENTRIES) {
        BOOST_FOREACH(const CTransaction& txCollateral, vecSessionCollaterals) {
            bool fFound = false;
            BOOST_FOREACH(const CSpySendEntry& entry, vecEntries)
                if(entry.txCollateral == txCollateral)
                    fFound = true;

            // This queue entry didn't send us the promised transaction
            if(!fFound) {
                LogPrintf("CSpysendPool::ChargeFees -- found uncooperative node (didn't send transaction), found offence\n");
                vecOffendersCollaterals.push_back(txCollateral);
            }
        }
    }

    if(nState == POOL_STATE_SIGNING) {
        // who didn't sign?
        BOOST_FOREACH(const CSpySendEntry entry, vecEntries) {
            BOOST_FOREACH(const CTxDSIn txdsin, entry.vecTxDSIn) {
                if(!txdsin.fHasSig) {
                    LogPrintf("CSpysendPool::ChargeFees -- found uncooperative node (didn't sign), found offence\n");
                    vecOffendersCollaterals.push_back(entry.txCollateral);
                }
            }
        }
    }

    // no offences found
    if(vecOffendersCollaterals.empty()) return;

    //mostly offending? Charge sometimes
    if((int)vecOffendersCollaterals.size() >= Params().PoolMaxTransactions() - 1 && GetRandInt(100) > 33) return;

    //everyone is an offender? That's not right
    if((int)vecOffendersCollaterals.size() >= Params().PoolMaxTransactions()) return;

    //charge one of the offenders randomly
    std::random_shuffle(vecOffendersCollaterals.begin(), vecOffendersCollaterals.end());

    if(nState == POOL_STATE_ACCEPTING_ENTRIES || nState == POOL_STATE_SIGNING) {
        LogPrintf("CSpysendPool::ChargeFees -- found uncooperative node (didn't %s transaction), charging fees: %s\n",
                (nState == POOL_STATE_SIGNING) ? "sign" : "send", vecOffendersCollaterals[0].ToString());

        LOCK(cs_main);

        CValidationState state;
        bool fMissingInputs;
        if(!AcceptToMemoryPool(mempool, state, vecOffendersCollaterals[0], false, &fMissingInputs, false, true)) {
            // should never really happen
            LogPrintf("CSpysendPool::ChargeFees -- ERROR: AcceptToMemoryPool failed!\n");
        } else {
            RelayTransaction(vecOffendersCollaterals[0]);
        }
    }
}

/*
    Charge the collateral randomly.
    Mixing is completely free, to pay miners we randomly pay the collateral of users.

    Collateral Fee Charges:

    Being that mixing has "no fees" we need to have some kind of cost associated
    with using it to stop abuse. Otherwise it could serve as an attack vector and
    allow endless transaction that would bloat Eternity and make it unusable. To
    stop these kinds of attacks 1 in 10 successful transactions are charged. This
    adds up to a cost of 0.001DRK per transaction on average.
*/
void CSpysendPool::ChargeRandomFees()
{
    if(!fEternityNode) return;

    LOCK(cs_main);

    BOOST_FOREACH(const CTransaction& txCollateral, vecSessionCollaterals) {

        if(GetRandInt(100) > 10) return;

        LogPrintf("CSpysendPool::ChargeRandomFees -- charging random fees, txCollateral=%s", txCollateral.ToString());

        CValidationState state;
        bool fMissingInputs;
        if(!AcceptToMemoryPool(mempool, state, txCollateral, false, &fMissingInputs, false, true)) {
            // should never really happen
            LogPrintf("CSpysendPool::ChargeRandomFees -- ERROR: AcceptToMemoryPool failed!\n");
        } else {
            RelayTransaction(txCollateral);
        }
    }
}

//
// Check for various timeouts (queue objects, mixing, etc)
//
void CSpysendPool::CheckTimeout()
{
    {
        TRY_LOCK(cs_spysend, lockDS);
        if(!lockDS) return; // it's ok to fail here, we run this quite frequently

        // check mixing queue objects for timeouts
        std::vector<CSpysendQueue>::iterator it = vecSpysendQueue.begin();
        while(it != vecSpysendQueue.end()) {
            if((*it).IsExpired()) {
                LogPrint("spysend", "CSpysendPool::CheckTimeout -- Removing expired queue (%s)\n", (*it).ToString());
                it = vecSpysendQueue.erase(it);
            } else ++it;
        }
    }

    if(!fEnableSpySend && !fEternityNode) return;

    // catching hanging sessions
    if(!fEternityNode) {
        switch(nState) {
            case POOL_STATE_ERROR:
                LogPrint("spysend", "CSpysendPool::CheckTimeout -- Pool error -- Running CheckPool\n");
                CheckPool();
                break;
            case POOL_STATE_SUCCESS:
                LogPrint("spysend", "CSpysendPool::CheckTimeout -- Pool success -- Running CheckPool\n");
                CheckPool();
                break;
            default:
                break;
        }
    }

    int nLagTime = fEternityNode ? 0 : 10000; // if we're the client, give the server a few extra seconds before resetting.
    int nTimeout = (nState == POOL_STATE_SIGNING) ? SPYSEND_SIGNING_TIMEOUT : SPYSEND_QUEUE_TIMEOUT;
    bool fTimeout = GetTimeMillis() - nTimeLastSuccessfulStep >= nTimeout*1000 + nLagTime;

    if(nState != POOL_STATE_IDLE && fTimeout) {
        LogPrint("spysend", "CSpysendPool::CheckTimeout -- %s timed out (%ds) -- restting\n",
                (nState == POOL_STATE_SIGNING) ? "Signing" : "Session", nTimeout);
        ChargeFees();
        UnlockCoins();
        SetNull();
        SetState(POOL_STATE_ERROR);
        strLastMessage = _("Session timed out.");
    }
}

/*
    Check to see if we're ready for submissions from clients
    After receiving multiple dsa messages, the queue will switch to "accepting entries"
    which is the active state right before merging the transaction
*/
void CSpysendPool::CheckForCompleteQueue()
{
    if(!fEnableSpySend && !fEternityNode) return;

    if(nState == POOL_STATE_QUEUE && IsSessionReady()) {
        SetState(POOL_STATE_ACCEPTING_ENTRIES);

        CSpysendQueue dsq(nSessionDenom, activeEternitynode.vin, GetTime(), true);
        LogPrint("spysend", "CSpysendPool::CheckForCompleteQueue -- queue is ready, signing and relaying (%s)\n", dsq.ToString());
        dsq.Sign();
        dsq.Relay();
    }
}

// Check to make sure a given input matches an input in the pool and its scriptSig is valid
bool CSpysendPool::IsInputScriptSigValid(const CTxIn& txin)
{
    CMutableTransaction txNew;
    txNew.vin.clear();
    txNew.vout.clear();

    int i = 0;
    int nTxInIndex = -1;
    CScript sigPubKey = CScript();

    BOOST_FOREACH(CSpySendEntry& entry, vecEntries) {

        BOOST_FOREACH(const CTxDSOut& txdsout, entry.vecTxDSOut)
            txNew.vout.push_back(txdsout);

        BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn) {
            txNew.vin.push_back(txdsin);

            if(txdsin.prevout == txin.prevout) {
                nTxInIndex = i;
                sigPubKey = txdsin.prevPubKey;
            }
            i++;
        }
    }

    if(nTxInIndex >= 0) { //might have to do this one input at a time?
        txNew.vin[nTxInIndex].scriptSig = txin.scriptSig;
        LogPrint("spysend", "CSpysendPool::IsInputScriptSigValid -- verifying scriptSig %s\n", ScriptToAsmStr(txin.scriptSig).substr(0,24));
        if(!VerifyScript(txNew.vin[nTxInIndex].scriptSig, sigPubKey, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, MutableTransactionSignatureChecker(&txNew, nTxInIndex))) {
            LogPrint("spysend", "CSpysendPool::IsInputScriptSigValid -- VerifyScript() failed on input %d\n", nTxInIndex);
            return false;
        }
    } else {
        LogPrint("spysend", "CSpysendPool::IsInputScriptSigValid -- Failed to find matching input in pool, %s\n", txin.ToString());
        return false;
    }

    LogPrint("spysend", "CSpysendPool::IsInputScriptSigValid -- Successfully validated input and scriptSig\n");
    return true;
}

// check to make sure the collateral provided by the client is valid
bool CSpysendPool::IsCollateralValid(const CTransaction& txCollateral)
{
    if(txCollateral.vout.empty()) return false;
    if(txCollateral.nLockTime != 0) return false;

    CAmount nValueIn = 0;
    CAmount nValueOut = 0;
    bool fMissingTx = false;

    BOOST_FOREACH(const CTxOut txout, txCollateral.vout) {
        nValueOut += txout.nValue;

        if(!txout.scriptPubKey.IsNormalPaymentScript()) {
            LogPrintf ("CSpysendPool::IsCollateralValid -- Invalid Script, txCollateral=%s", txCollateral.ToString());
            return false;
        }
    }

    BOOST_FOREACH(const CTxIn txin, txCollateral.vin) {
        CTransaction txPrev;
        uint256 hash;
        if(GetTransaction(txin.prevout.hash, txPrev, Params().GetConsensus(), hash, true)) {
            if(txPrev.vout.size() > txin.prevout.n)
                nValueIn += txPrev.vout[txin.prevout.n].nValue;
        } else {
            fMissingTx = true;
        }
    }

    if(fMissingTx) {
        LogPrint("spysend", "CSpysendPool::IsCollateralValid -- Unknown inputs in collateral transaction, txCollateral=%s", txCollateral.ToString());
        return false;
    }

    //collateral transactions are required to pay out SPYSEND_COLLATERAL as a fee to the miners
    if(nValueIn - nValueOut < SPYSEND_COLLATERAL) {
        LogPrint("spysend", "CSpysendPool::IsCollateralValid -- did not include enough fees in transaction: fees: %d, txCollateral=%s", nValueOut - nValueIn, txCollateral.ToString());
        return false;
    }

    LogPrint("spysend", "CSpysendPool::IsCollateralValid -- %s", txCollateral.ToString());

    {
        LOCK(cs_main);
        CValidationState validationState;
        if(!AcceptToMemoryPool(mempool, validationState, txCollateral, false, NULL, false, true, true)) {
            LogPrint("spysend", "CSpysendPool::IsCollateralValid -- didn't pass AcceptToMemoryPool()\n");
            return false;
        }
    }

    return true;
}


//
// Add a clients transaction to the pool
//
bool CSpysendPool::AddEntry(const CSpySendEntry& entryNew, PoolMessage& nMessageIDRet)
{
    if(!fEternityNode) return false;

    BOOST_FOREACH(CTxIn txin, entryNew.vecTxDSIn) {
        if(txin.prevout.IsNull()) {
            LogPrint("spysend", "CSpysendPool::AddEntry -- input not valid!\n");
            nMessageIDRet = ERR_INVALID_INPUT;
            return false;
        }
    }

    if(!IsCollateralValid(entryNew.txCollateral)) {
        LogPrint("spysend", "CSpysendPool::AddEntry -- collateral not valid!\n");
        nMessageIDRet = ERR_INVALID_COLLATERAL;
        return false;
    }

    if(GetEntriesCount() >= GetMaxPoolTransactions()) {
        LogPrint("spysend", "CSpysendPool::AddEntry -- entries is full!\n");
        nMessageIDRet = ERR_ENTRIES_FULL;
        return false;
    }

    BOOST_FOREACH(CTxIn txin, entryNew.vecTxDSIn) {
        LogPrint("spysend", "looking for txin -- %s\n", txin.ToString());
        BOOST_FOREACH(const CSpySendEntry& entry, vecEntries) {
            BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn) {
                if(txdsin.prevout == txin.prevout) {
                    LogPrint("spysend", "CSpysendPool::AddEntry -- found in txin\n");
                    nMessageIDRet = ERR_ALREADY_HAVE;
                    return false;
                }
            }
        }
    }

    vecEntries.push_back(entryNew);

    LogPrint("spysend", "CSpysendPool::AddEntry -- adding entry\n");
    nMessageIDRet = MSG_ENTRIES_ADDED;
    nTimeLastSuccessfulStep = GetTimeMillis();

    return true;
}

bool CSpysendPool::AddScriptSig(const CTxIn& txinNew)
{
    LogPrint("spysend", "CSpysendPool::AddScriptSig -- scriptSig=%s\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));

    BOOST_FOREACH(const CSpySendEntry& entry, vecEntries) {
        BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn) {
            if(txdsin.scriptSig == txinNew.scriptSig) {
                LogPrint("spysend", "CSpysendPool::AddScriptSig -- already exists\n");
                return false;
            }
        }
    }

    if(!IsInputScriptSigValid(txinNew)) {
        LogPrint("spysend", "CSpysendPool::AddScriptSig -- Invalid scriptSig\n");
        return false;
    }

    LogPrint("spysend", "CSpysendPool::AddScriptSig -- scriptSig=%s new\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));

    BOOST_FOREACH(CTxIn& txin, finalMutableTransaction.vin) {
        if(txinNew.prevout == txin.prevout && txin.nSequence == txinNew.nSequence) {
            txin.scriptSig = txinNew.scriptSig;
            txin.prevPubKey = txinNew.prevPubKey;
            LogPrint("spysend", "CSpysendPool::AddScriptSig -- adding to finalMutableTransaction, scriptSig=%s\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));
        }
    }
    for(int i = 0; i < GetEntriesCount(); i++) {
        if(vecEntries[i].AddScriptSig(txinNew)) {
            LogPrint("spysend", "CSpysendPool::AddScriptSig -- adding to entries, scriptSig=%s\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));
            return true;
        }
    }

    LogPrintf("CSpysendPool::AddScriptSig -- Couldn't set sig!\n" );
    return false;
}

// Check to make sure everything is signed
bool CSpysendPool::IsSignaturesComplete()
{
    BOOST_FOREACH(const CSpySendEntry& entry, vecEntries)
        BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn)
            if(!txdsin.fHasSig) return false;

    return true;
}

//
// Execute a mixing denomination via a Eternitynode.
// This is only ran from clients
//
bool CSpysendPool::SendDenominate(const std::vector<CTxIn>& vecTxIn, const std::vector<CTxOut>& vecTxOut)
{
    if(fEternityNode) {
        LogPrintf("CSpysendPool::SendDenominate -- SpySend from a Eternitynode is not supported currently.\n");
        return false;
    }

    if(txMyCollateral == CMutableTransaction()) {
        LogPrintf("CSpysendPool:SendDenominate -- SpySend collateral not set\n");
        return false;
    }

    // lock the funds we're going to use
    BOOST_FOREACH(CTxIn txin, txMyCollateral.vin)
        vecOutPointLocked.push_back(txin.prevout);

    BOOST_FOREACH(CTxIn txin, vecTxIn)
        vecOutPointLocked.push_back(txin.prevout);

    // we should already be connected to a Eternitynode
    if(!nSessionID) {
        LogPrintf("CSpysendPool::SendDenominate -- No Eternitynode has been selected yet.\n");
        UnlockCoins();
        SetNull();
        return false;
    }

    if(!CheckDiskSpace()) {
        UnlockCoins();
        SetNull();
        fEnableSpySend = false;
        LogPrintf("CSpysendPool::SendDenominate -- Not enough disk space, disabling SpySend.\n");
        return false;
    }

    SetState(POOL_STATE_ACCEPTING_ENTRIES);
    strLastMessage = "";

    LogPrintf("CSpysendPool::SendDenominate -- Added transaction to pool.\n");

    //check it against the memory pool to make sure it's valid
    {
        CValidationState validationState;
        CMutableTransaction tx;

        BOOST_FOREACH(const CTxIn& txin, vecTxIn) {
            LogPrint("spysend", "CSpysendPool::SendDenominate -- txin=%s\n", txin.ToString());
            tx.vin.push_back(txin);
        }

        BOOST_FOREACH(const CTxOut& txout, vecTxOut) {
            LogPrint("spysend", "CSpysendPool::SendDenominate -- txout=%s\n", txout.ToString());
            tx.vout.push_back(txout);
        }

        LogPrintf("CSpysendPool::SendDenominate -- Submitting partial tx %s", tx.ToString());

        mempool.PrioritiseTransaction(tx.GetHash(), tx.GetHash().ToString(), 1000, 0.1*COIN);
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain || !AcceptToMemoryPool(mempool, validationState, CTransaction(tx), false, NULL, false, true, true)) {
            LogPrintf("CSpysendPool::SendDenominate -- AcceptToMemoryPool() failed! tx=%s", tx.ToString());
            UnlockCoins();
            SetNull();
            return false;
        }
    }

    // store our entry for later use
    CSpySendEntry entry(vecTxIn, vecTxOut, txMyCollateral);
    vecEntries.push_back(entry);
    RelayIn(entry);
    nTimeLastSuccessfulStep = GetTimeMillis();

    return true;
}

// Incoming message from Eternitynode updating the progress of mixing
bool CSpysendPool::CheckPoolStateUpdate(PoolState nStateNew, int nEntriesCountNew, PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID, int nSessionIDNew)
{
    if(fEternityNode) return false;

    // do not update state when mixing client state is one of these
    if(nState == POOL_STATE_IDLE || nState == POOL_STATE_ERROR || nState == POOL_STATE_SUCCESS) return false;

    strAutoDenomResult = _("Eternitynode:") + " " + GetMessageByID(nMessageID);

    // if rejected at any state
    if(nStatusUpdate == STATUS_REJECTED) {
        LogPrintf("CSpysendPool::CheckPoolStateUpdate -- entry is rejected by Eternitynode\n");
        UnlockCoins();
        SetNull();
        SetState(POOL_STATE_ERROR);
        strLastMessage = GetMessageByID(nMessageID);
        return true;
    }

    if(nStatusUpdate == STATUS_ACCEPTED && nState == nStateNew) {
        if(nStateNew == POOL_STATE_QUEUE && nSessionID == 0 && nSessionIDNew != 0) {
            // new session id should be set only in POOL_STATE_QUEUE state
            nSessionID = nSessionIDNew;
            nTimeLastSuccessfulStep = GetTimeMillis();
            LogPrintf("CSpysendPool::CheckPoolStateUpdate -- set nSessionID to %d\n", nSessionID);
            return true;
        }
        else if(nStateNew == POOL_STATE_ACCEPTING_ENTRIES && nEntriesCount != nEntriesCountNew) {
            nEntriesCount = nEntriesCountNew;
            nTimeLastSuccessfulStep = GetTimeMillis();
            fLastEntryAccepted = true;
            LogPrintf("CSpysendPool::CheckPoolStateUpdate -- new entry accepted!\n");
            return true;
        }
    }

    // only situations above are allowed, fail in any other case
    return false;
}

//
// After we receive the finalized transaction from the Eternitynode, we must
// check it to make sure it's what we want, then sign it if we agree.
// If we refuse to sign, it's possible we'll be charged collateral
//
bool CSpysendPool::SignFinalTransaction(const CTransaction& finalTransactionNew, CNode* pnode)
{
    if(fEternityNode || pnode == NULL) return false;

    finalMutableTransaction = finalTransactionNew;
    LogPrintf("CSpysendPool::SignFinalTransaction -- finalMutableTransaction=%s", finalMutableTransaction.ToString());

    std::vector<CTxIn> sigs;

    //make sure my inputs/outputs are present, otherwise refuse to sign
    BOOST_FOREACH(const CSpySendEntry entry, vecEntries) {
        BOOST_FOREACH(const CTxDSIn txdsin, entry.vecTxDSIn) {
            /* Sign my transaction and all outputs */
            int nMyInputIndex = -1;
            CScript prevPubKey = CScript();
            CTxIn txin = CTxIn();

            for(unsigned int i = 0; i < finalMutableTransaction.vin.size(); i++) {
                if(finalMutableTransaction.vin[i] == txdsin) {
                    nMyInputIndex = i;
                    prevPubKey = txdsin.prevPubKey;
                    txin = txdsin;
                }
            }

            if(nMyInputIndex >= 0) { //might have to do this one input at a time?
                int nFoundOutputsCount = 0;
                CAmount nValue1 = 0;
                CAmount nValue2 = 0;

                for(unsigned int i = 0; i < finalMutableTransaction.vout.size(); i++) {
                    BOOST_FOREACH(const CTxOut& txout, entry.vecTxDSOut) {
                        if(finalMutableTransaction.vout[i] == txout) {
                            nFoundOutputsCount++;
                            nValue1 += finalMutableTransaction.vout[i].nValue;
                        }
                    }
                }

                BOOST_FOREACH(const CTxOut txout, entry.vecTxDSOut)
                    nValue2 += txout.nValue;

                int nTargetOuputsCount = entry.vecTxDSOut.size();
                if(nFoundOutputsCount < nTargetOuputsCount || nValue1 != nValue2) {
                    // in this case, something went wrong and we'll refuse to sign. It's possible we'll be charged collateral. But that's
                    // better then signing if the transaction doesn't look like what we wanted.
                    LogPrintf("CSpysendPool::SignFinalTransaction -- My entries are not correct! Refusing to sign: nFoundOutputsCount: %d, nTargetOuputsCount: %d\n", nFoundOutputsCount, nTargetOuputsCount);
                    UnlockCoins();
                    SetNull();

                    return false;
                }

                const CKeyStore& keystore = *pwalletMain;

                LogPrint("spysend", "CSpysendPool::SignFinalTransaction -- Signing my input %i\n", nMyInputIndex);
                if(!SignSignature(keystore, prevPubKey, finalMutableTransaction, nMyInputIndex, int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))) { // changes scriptSig
                    LogPrint("spysend", "CSpysendPool::SignFinalTransaction -- Unable to sign my own transaction!\n");
                    // not sure what to do here, it will timeout...?
                }

                sigs.push_back(finalMutableTransaction.vin[nMyInputIndex]);
                LogPrint("spysend", "CSpysendPool::SignFinalTransaction -- nMyInputIndex: %d, sigs.size(): %d, scriptSig=%s\n", nMyInputIndex, (int)sigs.size(), ScriptToAsmStr(finalMutableTransaction.vin[nMyInputIndex].scriptSig));
            }
        }
    }

    if(sigs.empty()) {
        LogPrintf("CSpysendPool::SignFinalTransaction -- can't sign anything!\n");
        UnlockCoins();
        SetNull();

        return false;
    }

    // push all of our signatures to the Eternitynode
    LogPrintf("CSpysendPool::SignFinalTransaction -- pushing sigs to the eternitynode, finalMutableTransaction=%s", finalMutableTransaction.ToString());
    pnode->PushMessage(NetMsgType::DSSIGNFINALTX, sigs);
    SetState(POOL_STATE_SIGNING);
    nTimeLastSuccessfulStep = GetTimeMillis();

    return true;
}

void CSpysendPool::NewBlock()
{
    static int64_t nTimeNewBlockReceived = 0;

    //we we're processing lots of blocks, we'll just leave
    if(GetTime() - nTimeNewBlockReceived < 10) return;
    nTimeNewBlockReceived = GetTime();
    LogPrint("spysend", "CSpysendPool::NewBlock\n");

    CheckTimeout();
}

// mixing transaction was completed (failed or successful)
void CSpysendPool::CompletedTransaction(PoolMessage nMessageID)
{
    if(fEternityNode) return;

    if(nMessageID == MSG_SUCCESS) {
        LogPrintf("CompletedTransaction -- success\n");
        nCachedLastSuccessBlock = pCurrentBlockIndex->nHeight;
    } else {
        LogPrintf("CompletedTransaction -- error\n");
    }
    UnlockCoins();
    SetNull();
    strLastMessage = GetMessageByID(nMessageID);
}

//
// Passively run mixing in the background to anonymize funds based on the given configuration.
//
bool CSpysendPool::DoAutomaticDenominating(bool fDryRun)
{
    if(!fEnableSpySend || fEternityNode || !pCurrentBlockIndex) return false;
    if(!pwalletMain || pwalletMain->IsLocked(true)) return false;
    if(nState != POOL_STATE_IDLE) return false;

    if(!eternitynodeSync.IsEternitynodeListSynced()) {
        strAutoDenomResult = _("Can't mix while sync in progress.");
        return false;
    }

    switch(nWalletBackups) {
        case 0:
            LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- Automatic backups disabled, no mixing available.\n");
            strAutoDenomResult = _("Automatic backups disabled") + ", " + _("no mixing available.");
            fEnableSpySend = false; // stop mixing
            pwalletMain->nKeysLeftSinceAutoBackup = 0; // no backup, no "keys since last backup"
            return false;
        case -1:
            // Automatic backup failed, nothing else we can do until user fixes the issue manually.
            // There is no way to bring user attention in daemon mode so we just update status and
            // keep spaming if debug is on.
            LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- ERROR! Failed to create automatic backup.\n");
            strAutoDenomResult = _("ERROR! Failed to create automatic backup") + ", " + _("see debug.log for details.");
            return false;
        case -2:
            // We were able to create automatic backup but keypool was not replenished because wallet is locked.
            // There is no way to bring user attention in daemon mode so we just update status and
            // keep spaming if debug is on.
            LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- WARNING! Failed to create replenish keypool, please unlock your wallet to do so.\n");
            strAutoDenomResult = _("WARNING! Failed to replenish keypool, please unlock your wallet to do so.") + ", " + _("see debug.log for details.");
            return false;
    }

    if(pwalletMain->nKeysLeftSinceAutoBackup < SPYSEND_KEYS_THRESHOLD_STOP) {
        // We should never get here via mixing itself but probably smth else is still actively using keypool
        LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- Very low number of keys left: %d, no mixing available.\n", pwalletMain->nKeysLeftSinceAutoBackup);
        strAutoDenomResult = strprintf(_("Very low number of keys left: %d") + ", " + _("no mixing available."), pwalletMain->nKeysLeftSinceAutoBackup);
        // It's getting really dangerous, stop mixing
        fEnableSpySend = false;
        return false;
    } else if(pwalletMain->nKeysLeftSinceAutoBackup < SPYSEND_KEYS_THRESHOLD_WARNING) {
        // Low number of keys left but it's still more or less safe to continue
        LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- Very low number of keys left: %d\n", pwalletMain->nKeysLeftSinceAutoBackup);
        strAutoDenomResult = strprintf(_("Very low number of keys left: %d"), pwalletMain->nKeysLeftSinceAutoBackup);

        if(fCreateAutoBackups) {
            LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- Trying to create new backup.\n");
            std::string warningString;
            std::string errorString;

            if(!AutoBackupWallet(pwalletMain, "", warningString, errorString)) {
                if(!warningString.empty()) {
                    // There were some issues saving backup but yet more or less safe to continue
                    LogPrintf("CSpysendPool::DoAutomaticDenominating -- WARNING! Something went wrong on automatic backup: %s\n", warningString);
                }
                if(!errorString.empty()) {
                    // Things are really broken
                    LogPrintf("CSpysendPool::DoAutomaticDenominating -- ERROR! Failed to create automatic backup: %s\n", errorString);
                    strAutoDenomResult = strprintf(_("ERROR! Failed to create automatic backup") + ": %s", errorString);
                    return false;
                }
            }
        } else {
            // Wait for someone else (e.g. GUI action) to create automatic backup for us
            return false;
        }
    }

    LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- Keys left since latest backup: %d\n", pwalletMain->nKeysLeftSinceAutoBackup);

    if(GetEntriesCount() > 0) {
        strAutoDenomResult = _("Mixing in progress...");
        return false;
    }

    TRY_LOCK(cs_spysend, lockDS);
    if(!lockDS) {
        strAutoDenomResult = _("Lock is already in place.");
        return false;
    }

    if(!fDryRun && pwalletMain->IsLocked(true)) {
        strAutoDenomResult = _("Wallet is locked.");
        return false;
    }

    if(!fSpySendMultiSession && pCurrentBlockIndex->nHeight - nCachedLastSuccessBlock < nMinBlockSpacing) {
        LogPrintf("CSpysendPool::DoAutomaticDenominating -- Last successful SpySend action was too recent\n");
        strAutoDenomResult = _("Last successful SpySend action was too recent.");
        return false;
    }

    if(mnodeman.size() == 0) {
        LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- No Eternitynodes detected\n");
        strAutoDenomResult = _("No Eternitynodes detected.");
        return false;
    }

    CAmount nValueMin = vecSpySendDenominations.back();

    // if there are no confirmed DS collateral inputs yet
    if(!pwalletMain->HasCollateralInputs()) {
        // should have some additional amount for them
        nValueMin += SPYSEND_COLLATERAL*4;
    }

    // including denoms but applying some restrictions
    CAmount nBalanceNeedsAnonymized = pwalletMain->GetNeedsToBeAnonymizedBalance(nValueMin);

    // anonymizable balance is way too small
    if(nBalanceNeedsAnonymized < nValueMin) {
        LogPrintf("CSpysendPool::DoAutomaticDenominating -- Not enough funds to anonymize\n");
        strAutoDenomResult = _("Not enough funds to anonymize.");
        return false;
    }

    // excluding denoms
    CAmount nBalanceAnonimizableNonDenom = pwalletMain->GetAnonymizableBalance(true);
    // denoms
    CAmount nBalanceDenominatedConf = pwalletMain->GetDenominatedBalance();
    CAmount nBalanceDenominatedUnconf = pwalletMain->GetDenominatedBalance(true);
    CAmount nBalanceDenominated = nBalanceDenominatedConf + nBalanceDenominatedUnconf;

    LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- nValueMin: %f, nBalanceNeedsAnonymized: %f, nBalanceAnonimizableNonDenom: %f, nBalanceDenominatedConf: %f, nBalanceDenominatedUnconf: %f, nBalanceDenominated: %f\n",
            (float)nValueMin/COIN,
            (float)nBalanceNeedsAnonymized/COIN,
            (float)nBalanceAnonimizableNonDenom/COIN,
            (float)nBalanceDenominatedConf/COIN,
            (float)nBalanceDenominatedUnconf/COIN,
            (float)nBalanceDenominated/COIN);

    if(fDryRun) return true;

    // Check if we have should create more denominated inputs i.e.
    // there are funds to denominate and denominated balance does not exceed
    // max amount to mix yet.
    if(nBalanceAnonimizableNonDenom >= nValueMin + SPYSEND_COLLATERAL && nBalanceDenominated < nSpySendAmount*COIN)
        return CreateDenominated();

    //check if we have the collateral sized inputs
    if(!pwalletMain->HasCollateralInputs())
        return !pwalletMain->HasCollateralInputs(false) && MakeCollateralAmounts();

    if(nSessionID) {
        strAutoDenomResult = _("Mixing in progress...");
        return false;
    }

    // Initial phase, find a Eternitynode
    // Clean if there is anything left from previous session
    UnlockCoins();
    SetNull();

    // should be no unconfirmed denoms in non-multi-session mode
    if(!fSpySendMultiSession && nBalanceDenominatedUnconf > 0) {
        LogPrintf("CSpysendPool::DoAutomaticDenominating -- Found unconfirmed denominated outputs, will wait till they confirm to continue.\n");
        strAutoDenomResult = _("Found unconfirmed denominated outputs, will wait till they confirm to continue.");
        return false;
    }

    //check our collateral and create new if needed
    std::string strReason;
    if(txMyCollateral == CMutableTransaction()) {
        if(!pwalletMain->CreateCollateralTransaction(txMyCollateral, strReason)) {
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- create collateral error:%s\n", strReason);
            return false;
        }
    } else {
        if(!IsCollateralValid(txMyCollateral)) {
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- invalid collateral, recreating...\n");
            if(!pwalletMain->CreateCollateralTransaction(txMyCollateral, strReason)) {
                LogPrintf("CSpysendPool::DoAutomaticDenominating -- create collateral error: %s\n", strReason);
                return false;
            }
        }
    }

    int nMnCountEnabled = mnodeman.CountEnabled(MIN_SPYSEND_PEER_PROTO_VERSION);

    // If we've used 90% of the Eternitynode list then drop the oldest first ~30%
    int nThreshold_high = nMnCountEnabled * 0.9;
    int nThreshold_low = nThreshold_high * 0.7;
    LogPrint("spysend", "Checking vecEternitynodesUsed: size: %d, threshold: %d\n", (int)vecEternitynodesUsed.size(), nThreshold_high);

    if((int)vecEternitynodesUsed.size() > nThreshold_high) {
        vecEternitynodesUsed.erase(vecEternitynodesUsed.begin(), vecEternitynodesUsed.begin() + vecEternitynodesUsed.size() - nThreshold_low);
        LogPrint("spysend", "  vecEternitynodesUsed: new size: %d, threshold: %d\n", (int)vecEternitynodesUsed.size(), nThreshold_high);
    }

    bool fUseQueue = GetRandInt(100) > 33;
    // don't use the queues all of the time for mixing unless we are a liquidity provider
    if(nLiquidityProvider || fUseQueue) {

        // Look through the queues and see if anything matches
        BOOST_FOREACH(CSpysendQueue& dsq, vecSpysendQueue) {
            // only try each queue once
            if(dsq.fTried) continue;
            dsq.fTried = true;

            if(dsq.IsExpired()) continue;

            CEternitynode* pmn = mnodeman.Find(dsq.vin);
            if(pmn == NULL) {
                LogPrintf("CSpysendPool::DoAutomaticDenominating -- dsq eternitynode is not in eternitynode list, eternitynode=%s\n", dsq.vin.prevout.ToStringShort());
                continue;
            }

            if(pmn->nProtocolVersion < MIN_SPYSEND_PEER_PROTO_VERSION) continue;

            std::vector<int> vecBits;
            if(!GetDenominationsBits(dsq.nDenom, vecBits)) {
                // incompatible denom
                continue;
            }

            // mixing rate limit i.e. nLastDsq check should already pass in DSQUEUE ProcessMessage
            // in order for dsq to get into vecSpysendQueue, so we should be safe to mix already,
            // no need for additional verification here

            LogPrint("spysend", "CSpysendPool::DoAutomaticDenominating -- found valid queue: %s\n", dsq.ToString());

            CAmount nValueInTmp = 0;
            std::vector<CTxIn> vecTxInTmp;
            std::vector<COutput> vCoinsTmp;

            // Try to match their denominations if possible, select at least 1 denominations
            if(!pwalletMain->SelectCoinsByDenominations(dsq.nDenom, vecSpySendDenominations[vecBits.front()], nBalanceNeedsAnonymized, vecTxInTmp, vCoinsTmp, nValueInTmp, 0, nSpySendRounds)) {
                LogPrintf("CSpysendPool::DoAutomaticDenominating -- Couldn't match denominations %d %d (%s)\n", vecBits.front(), dsq.nDenom, GetDenominationsToString(dsq.nDenom));
                continue;
            }

            vecEternitynodesUsed.push_back(dsq.vin);

            CNode* pnodeFound = NULL;
            {
                LOCK(cs_vNodes);
                pnodeFound = FindNode(pmn->addr);
                if(pnodeFound) {
                    if(pnodeFound->fDisconnect) {
                        continue;
                    } else {
                        pnodeFound->AddRef();
                    }
                }
            }

            LogPrintf("CSpysendPool::DoAutomaticDenominating -- attempt to connect to eternitynode from queue, addr=%s\n", pmn->addr.ToString());
            // connect to Eternitynode and submit the queue request
            CNode* pnode = (pnodeFound && pnodeFound->fEternitynode) ? pnodeFound : ConnectNode((CAddress)pmn->addr, NULL, true);
            if(pnode) {
                pSubmittedToEternitynode = pmn;
                nSessionDenom = dsq.nDenom;

                pnode->PushMessage(NetMsgType::DSACCEPT, nSessionDenom, txMyCollateral);
                LogPrintf("CSpysendPool::DoAutomaticDenominating -- connected (from queue), sending DSACCEPT: nSessionDenom: %d (%s), addr=%s\n",
                        nSessionDenom, GetDenominationsToString(nSessionDenom), pnode->addr.ToString());
                strAutoDenomResult = _("Mixing in progress...");
                SetState(POOL_STATE_QUEUE);
                nTimeLastSuccessfulStep = GetTimeMillis();
                if(pnodeFound) {
                    pnodeFound->Release();
                }
                return true;
            } else {
                LogPrintf("CSpysendPool::DoAutomaticDenominating -- can't connect, addr=%s\n", pmn->addr.ToString());
                strAutoDenomResult = _("Error connecting to Eternitynode.");
                continue;
            }
        }
    }

    // do not initiate queue if we are a liquidity provider to avoid useless inter-mixing
    if(nLiquidityProvider) return false;

    int nTries = 0;

    // ** find the coins we'll use
    std::vector<CTxIn> vecTxIn;
    CAmount nValueInTmp = 0;
    if(!pwalletMain->SelectCoinsDark(nValueMin, nBalanceNeedsAnonymized, vecTxIn, nValueInTmp, 0, nSpySendRounds)) {
        // this should never happen
        LogPrintf("CSpysendPool::DoAutomaticDenominating -- Can't mix: no compatible inputs found!\n");
        strAutoDenomResult = _("Can't mix: no compatible inputs found!");
        return false;
    }

    // otherwise, try one randomly
    while(nTries < 10) {
        CEternitynode* pmn = mnodeman.FindRandomNotInVec(vecEternitynodesUsed, MIN_SPYSEND_PEER_PROTO_VERSION);
        if(pmn == NULL) {
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- Can't find random eternitynode!\n");
            strAutoDenomResult = _("Can't find random Eternitynode.");
            return false;
        }
        vecEternitynodesUsed.push_back(pmn->vin);

        if(pmn->nLastDsq != 0 && pmn->nLastDsq + nMnCountEnabled/5 > mnodeman.nDsqCount) {
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- Too early to mix on this eternitynode!"
                        " eternitynode=%s  addr=%s  nLastDsq=%d  CountEnabled/5=%d  nDsqCount=%d\n",
                        pmn->vin.prevout.ToStringShort(), pmn->addr.ToString(), pmn->nLastDsq,
                        nMnCountEnabled/5, mnodeman.nDsqCount);
            nTries++;
            continue;
        }

        CNode* pnodeFound = NULL;
        {
            LOCK(cs_vNodes);
            pnodeFound = FindNode(pmn->addr);
            if(pnodeFound) {
                if(pnodeFound->fDisconnect) {
                    nTries++;
                    continue;
                } else {
                    pnodeFound->AddRef();
                }
            }
        }

        LogPrintf("CSpysendPool::DoAutomaticDenominating -- attempt %d connection to Eternitynode %s\n", nTries, pmn->addr.ToString());
        CNode* pnode = (pnodeFound && pnodeFound->fEternitynode) ? pnodeFound : ConnectNode((CAddress)pmn->addr, NULL, true);
        if(pnode) {
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- connected, addr=%s\n", pmn->addr.ToString());
            pSubmittedToEternitynode = pmn;

            std::vector<CAmount> vecAmounts;
            pwalletMain->ConvertList(vecTxIn, vecAmounts);
            // try to get a single random denom out of vecAmounts
            while(nSessionDenom == 0) {
                nSessionDenom = GetDenominationsByAmounts(vecAmounts);
            }

            pnode->PushMessage(NetMsgType::DSACCEPT, nSessionDenom, txMyCollateral);
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- connected, sending DSACCEPT, nSessionDenom: %d (%s)\n",
                    nSessionDenom, GetDenominationsToString(nSessionDenom));
            strAutoDenomResult = _("Mixing in progress...");
            SetState(POOL_STATE_QUEUE);
            nTimeLastSuccessfulStep = GetTimeMillis();
            if(pnodeFound) {
                pnodeFound->Release();
            }
            return true;
        } else {
            LogPrintf("CSpysendPool::DoAutomaticDenominating -- can't connect, addr=%s\n", pmn->addr.ToString());
            nTries++;
            continue;
        }
    }

    strAutoDenomResult = _("No compatible Eternitynode found.");
    return false;
}

bool CSpysendPool::SubmitDenominate()
{
    std::string strError;
    std::vector<CTxIn> vecTxInRet;
    std::vector<CTxOut> vecTxOutRet;

    // Submit transaction to the pool if we get here
    // Try to use only inputs with the same number of rounds starting from lowest number of rounds possible
    for(int i = 0; i < nSpySendRounds; i++) {
        if(PrepareDenominate(i, i+1, strError, vecTxInRet, vecTxOutRet)) {
            LogPrintf("CSpysendPool::SubmitDenominate -- Running SpySend denominate for %d rounds, success\n", i);
            return SendDenominate(vecTxInRet, vecTxOutRet);
        }
        LogPrintf("CSpysendPool::SubmitDenominate -- Running SpySend denominate for %d rounds, error: %s\n", i, strError);
    }

    // We failed? That's strange but let's just make final attempt and try to mix everything
    if(PrepareDenominate(0, nSpySendRounds, strError, vecTxInRet, vecTxOutRet)) {
        LogPrintf("CSpysendPool::SubmitDenominate -- Running SpySend denominate for all rounds, success\n");
        return SendDenominate(vecTxInRet, vecTxOutRet);
    }

    // Should never actually get here but just in case
    LogPrintf("CSpysendPool::SubmitDenominate -- Running SpySend denominate for all rounds, error: %s\n", strError);
    strAutoDenomResult = strError;
    return false;
}

bool CSpysendPool::PrepareDenominate(int nMinRounds, int nMaxRounds, std::string& strErrorRet, std::vector<CTxIn>& vecTxInRet, std::vector<CTxOut>& vecTxOutRet)
{
    if (pwalletMain->IsLocked(true)) {
        strErrorRet = "Wallet locked, unable to create transaction!";
        return false;
    }

    if (GetEntriesCount() > 0) {
        strErrorRet = "Already have pending entries in the SpySend pool";
        return false;
    }

    // make sure returning vectors are empty before filling them up
    vecTxInRet.clear();
    vecTxOutRet.clear();

    // ** find the coins we'll use
    std::vector<CTxIn> vecTxIn;
    std::vector<COutput> vCoins;
    CAmount nValueIn = 0;
    CReserveKey reservekey(pwalletMain);

    /*
        Select the coins we'll use

        if nMinRounds >= 0 it means only denominated inputs are going in and coming out
    */
    std::vector<int> vecBits;
    if (!GetDenominationsBits(nSessionDenom, vecBits)) {
        strErrorRet = "Incorrect session denom";
        return false;
    }
    bool fSelected = pwalletMain->SelectCoinsByDenominations(nSessionDenom, vecSpySendDenominations[vecBits.front()], SPYSEND_POOL_MAX, vecTxIn, vCoins, nValueIn, nMinRounds, nMaxRounds);
    if (nMinRounds >= 0 && !fSelected) {
        strErrorRet = "Can't select current denominated inputs";
        return false;
    }

    LogPrintf("CSpysendPool::PrepareDenominate -- max value: %f\n", (double)nValueIn/COIN);

    {
        LOCK(pwalletMain->cs_wallet);
        BOOST_FOREACH(CTxIn txin, vecTxIn) {
            pwalletMain->LockCoin(txin.prevout);
        }
    }

    CAmount nValueLeft = nValueIn;

    // Try to add every needed denomination, repeat up to 5-9 times.
    // NOTE: No need to randomize order of inputs because they were
    // initially shuffled in CWallet::SelectCoinsByDenominations already.
    int nStep = 0;
    int nStepsMax = 5 + GetRandInt(5);

    while (nStep < nStepsMax) {
        BOOST_FOREACH(int nBit, vecBits) {
            CAmount nValueDenom = vecSpySendDenominations[nBit];
            if (nValueLeft - nValueDenom < 0) continue;

            // Note: this relies on a fact that both vectors MUST have same size
            std::vector<CTxIn>::iterator it = vecTxIn.begin();
            std::vector<COutput>::iterator it2 = vCoins.begin();
            while (it2 != vCoins.end()) {
                // we have matching inputs
                if ((*it2).tx->vout[(*it2).i].nValue == nValueDenom) {
                    // add new input in resulting vector
                    vecTxInRet.push_back(*it);
                    // remove corresponting items from initial vectors
                    vecTxIn.erase(it);
                    vCoins.erase(it2);

                    CScript scriptChange;
                    CPubKey vchPubKey;
                    // use a unique change address
                    assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
                    scriptChange = GetScriptForDestination(vchPubKey.GetID());
                    reservekey.KeepKey();

                    // add new output
                    CTxOut txout(nValueDenom, scriptChange);
                    vecTxOutRet.push_back(txout);

                    // subtract denomination amount
                    nValueLeft -= nValueDenom;

                    // step is complete
                    break;
                }
                ++it;
                ++it2;
            }
        }
        if(nValueLeft == 0) break;
        nStep++;
    }

    {
        // unlock unused coins
        LOCK(pwalletMain->cs_wallet);
        BOOST_FOREACH(CTxIn txin, vecTxIn) {
            pwalletMain->UnlockCoin(txin.prevout);
        }
    }

    if (GetDenominations(vecTxOutRet) != nSessionDenom) {
        // unlock used coins on failure
        LOCK(pwalletMain->cs_wallet);
        BOOST_FOREACH(CTxIn txin, vecTxInRet) {
            pwalletMain->UnlockCoin(txin.prevout);
        }
        strErrorRet = "Can't make current denominated outputs";
        return false;
    }

    // We also do not care about full amount as long as we have right denominations
    return true;
}

// Create collaterals by looping through inputs grouped by addresses
bool CSpysendPool::MakeCollateralAmounts()
{
    std::vector<CompactTallyItem> vecTally;
    if(!pwalletMain->SelectCoinsGrouppedByAddresses(vecTally, false)) {
        LogPrint("spysend", "CSpysendPool::MakeCollateralAmounts -- SelectCoinsGrouppedByAddresses can't find any inputs!\n");
        return false;
    }

    BOOST_FOREACH(CompactTallyItem& item, vecTally) {
        if(!MakeCollateralAmounts(item)) continue;
        return true;
    }

    LogPrintf("CSpysendPool::MakeCollateralAmounts -- failed!\n");
    return false;
}

// Split up large inputs or create fee sized inputs
bool CSpysendPool::MakeCollateralAmounts(const CompactTallyItem& tallyItem)
{
    CWalletTx wtx;
    CAmount nFeeRet = 0;
    int nChangePosRet = -1;
    std::string strFail = "";
    std::vector<CRecipient> vecSend;

    // make our collateral address
    CReserveKey reservekeyCollateral(pwalletMain);
    // make our change address
    CReserveKey reservekeyChange(pwalletMain);

    CScript scriptCollateral;
    CPubKey vchPubKey;
    assert(reservekeyCollateral.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
    scriptCollateral = GetScriptForDestination(vchPubKey.GetID());

    vecSend.push_back((CRecipient){scriptCollateral, SPYSEND_COLLATERAL*4, false});

    // try to use non-denominated and not mn-like funds first, select them explicitly
    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = false;
    coinControl.fAllowWatchOnly = false;
    // send change to the same address so that we were able create more denoms out of it later
    coinControl.destChange = tallyItem.address.Get();
    BOOST_FOREACH(const CTxIn& txin, tallyItem.vecTxIn)
        coinControl.Select(txin.prevout);

    bool fSuccess = pwalletMain->CreateTransaction(vecSend, wtx, reservekeyChange,
            nFeeRet, nChangePosRet, strFail, &coinControl, true, ONLY_NONDENOMINATED_NOT1000IFMN);
    if(!fSuccess) {
        // if we failed (most likeky not enough funds), try to use all coins instead -
        // MN-like funds should not be touched in any case and we can't mix denominated without collaterals anyway
        LogPrintf("CSpysendPool::MakeCollateralAmounts -- ONLY_NONDENOMINATED_NOT1000IFMN Error: %s\n", strFail);
        CCoinControl *coinControlNull = NULL;
        fSuccess = pwalletMain->CreateTransaction(vecSend, wtx, reservekeyChange,
                nFeeRet, nChangePosRet, strFail, coinControlNull, true, ONLY_NOT1000IFMN);
        if(!fSuccess) {
            LogPrintf("CSpysendPool::MakeCollateralAmounts -- ONLY_NOT1000IFMN Error: %s\n", strFail);
            reservekeyCollateral.ReturnKey();
            return false;
        }
    }

    reservekeyCollateral.KeepKey();

    LogPrintf("CSpysendPool::MakeCollateralAmounts -- txid=%s\n", wtx.GetHash().GetHex());

    // use the same nCachedLastSuccessBlock as for DS mixinx to prevent race
    if(!pwalletMain->CommitTransaction(wtx, reservekeyChange)) {
        LogPrintf("CSpysendPool::MakeCollateralAmounts -- CommitTransaction failed!\n");
        return false;
    }

    nCachedLastSuccessBlock = pCurrentBlockIndex->nHeight;

    return true;
}

// Create denominations by looping through inputs grouped by addresses
bool CSpysendPool::CreateDenominated()
{
    std::vector<CompactTallyItem> vecTally;
    if(!pwalletMain->SelectCoinsGrouppedByAddresses(vecTally)) {
        LogPrint("spysend", "CSpysendPool::CreateDenominated -- SelectCoinsGrouppedByAddresses can't find any inputs!\n");
        return false;
    }

    bool fCreateMixingCollaterals = !pwalletMain->HasCollateralInputs();

    BOOST_FOREACH(CompactTallyItem& item, vecTally) {
        if(!CreateDenominated(item, fCreateMixingCollaterals)) continue;
        return true;
    }

    LogPrintf("CSpysendPool::CreateDenominated -- failed!\n");
    return false;
}

// Create denominations
bool CSpysendPool::CreateDenominated(const CompactTallyItem& tallyItem, bool fCreateMixingCollaterals)
{
    std::vector<CRecipient> vecSend;
    CAmount nValueLeft = tallyItem.nAmount;
    nValueLeft -= SPYSEND_COLLATERAL; // leave some room for fees

    LogPrintf("CreateDenominated0 nValueLeft: %f\n", (float)nValueLeft/COIN);
    // make our collateral address
    CReserveKey reservekeyCollateral(pwalletMain);

    CScript scriptCollateral;
    CPubKey vchPubKey;
    assert(reservekeyCollateral.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
    scriptCollateral = GetScriptForDestination(vchPubKey.GetID());

    // ****** Add collateral outputs ************ /

    if(fCreateMixingCollaterals) {
        vecSend.push_back((CRecipient){scriptCollateral, SPYSEND_COLLATERAL*4, false});
        nValueLeft -= SPYSEND_COLLATERAL*4;
    }

    // ****** Add denoms ************ /

    // make our denom addresses
    CReserveKey reservekeyDenom(pwalletMain);

    // try few times - skipping smallest denoms first if there are too much already, if failed - use them
    int nOutputsTotal = 0;
    bool fSkip = true;
    do {

        BOOST_REVERSE_FOREACH(CAmount nDenomValue, vecSpySendDenominations) {

            if(fSkip) {
                // Note: denoms are skipped if there are already DENOMS_COUNT_MAX of them
                // and there are still larger denoms which can be used for mixing

                // check skipped denoms
                if(IsDenomSkipped(nDenomValue)) continue;

                // find new denoms to skip if any (ignore the largest one)
                if(nDenomValue != vecSpySendDenominations[0] && pwalletMain->CountInputsWithAmount(nDenomValue) > DENOMS_COUNT_MAX) {
                    strAutoDenomResult = strprintf(_("Too many %f denominations, removing."), (float)nDenomValue/COIN);
                    LogPrintf("CSpysendPool::CreateDenominated -- %s\n", strAutoDenomResult);
                    vecDenominationsSkipped.push_back(nDenomValue);
                    continue;
                }
            }

            int nOutputs = 0;

            // add each output up to 10 times until it can't be added again
            while(nValueLeft - nDenomValue >= 0 && nOutputs <= 10) {
                CScript scriptDenom;
                CPubKey vchPubKey;
                //use a unique change address
                assert(reservekeyDenom.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
                scriptDenom = GetScriptForDestination(vchPubKey.GetID());
                // TODO: do not keep reservekeyDenom here
                reservekeyDenom.KeepKey();

                vecSend.push_back((CRecipient){ scriptDenom, nDenomValue, false });

                //increment outputs and subtract denomination amount
                nOutputs++;
                nValueLeft -= nDenomValue;
                LogPrintf("CreateDenominated1: nOutputsTotal: %d, nOutputs: %d, nValueLeft: %f\n", nOutputsTotal, nOutputs, (float)nValueLeft/COIN);
            }

            nOutputsTotal += nOutputs;
            if(nValueLeft == 0) break;
        }
        LogPrintf("CreateDenominated2: nOutputsTotal: %d, nValueLeft: %f\n", nOutputsTotal, (float)nValueLeft/COIN);
        // if there were no outputs added, start over without skipping
        fSkip = !fSkip;
    } while (nOutputsTotal == 0 && !fSkip);
    LogPrintf("CreateDenominated3: nOutputsTotal: %d, nValueLeft: %f\n", nOutputsTotal, (float)nValueLeft/COIN);

    // if we have anything left over, it will be automatically send back as change - there is no need to send it manually

    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = false;
    coinControl.fAllowWatchOnly = false;
    // send change to the same address so that we were able create more denoms out of it later
    coinControl.destChange = tallyItem.address.Get();
    BOOST_FOREACH(const CTxIn& txin, tallyItem.vecTxIn)
        coinControl.Select(txin.prevout);

    CWalletTx wtx;
    CAmount nFeeRet = 0;
    int nChangePosRet = -1;
    std::string strFail = "";
    // make our change address
    CReserveKey reservekeyChange(pwalletMain);

    bool fSuccess = pwalletMain->CreateTransaction(vecSend, wtx, reservekeyChange,
            nFeeRet, nChangePosRet, strFail, &coinControl, true, ONLY_NONDENOMINATED_NOT1000IFMN);
    if(!fSuccess) {
        LogPrintf("CSpysendPool::CreateDenominated -- Error: %s\n", strFail);
        // TODO: return reservekeyDenom here
        reservekeyCollateral.ReturnKey();
        return false;
    }

    // TODO: keep reservekeyDenom here
    reservekeyCollateral.KeepKey();

    if(!pwalletMain->CommitTransaction(wtx, reservekeyChange)) {
        LogPrintf("CSpysendPool::CreateDenominated -- CommitTransaction failed!\n");
        return false;
    }

    // use the same nCachedLastSuccessBlock as for DS mixing to prevent race
    nCachedLastSuccessBlock = pCurrentBlockIndex->nHeight;
    LogPrintf("CSpysendPool::CreateDenominated -- txid=%s\n", wtx.GetHash().GetHex());

    return true;
}

bool CSpysendPool::IsOutputsCompatibleWithSessionDenom(const std::vector<CTxDSOut>& vecTxDSOut)
{
    if(GetDenominations(vecTxDSOut) == 0) return false;

    BOOST_FOREACH(const CSpySendEntry entry, vecEntries) {
        LogPrintf("CSpysendPool::IsOutputsCompatibleWithSessionDenom -- vecTxDSOut denom %d, entry.vecTxDSOut denom %d\n", GetDenominations(vecTxDSOut), GetDenominations(entry.vecTxDSOut));
        if(GetDenominations(vecTxDSOut) != GetDenominations(entry.vecTxDSOut)) return false;
    }

    return true;
}

bool CSpysendPool::IsAcceptableDenomAndCollateral(int nDenom, CTransaction txCollateral, PoolMessage& nMessageIDRet)
{
    if(!fEternityNode) return false;

    // is denom even smth legit?
    std::vector<int> vecBits;
    if(!GetDenominationsBits(nDenom, vecBits)) {
        LogPrint("spysend", "CSpysendPool::IsAcceptableDenomAndCollateral -- denom not valid!\n");
        nMessageIDRet = ERR_DENOM;
        return false;
    }

    // check collateral
    if(!fUnitTest && !IsCollateralValid(txCollateral)) {
        LogPrint("spysend", "CSpysendPool::IsAcceptableDenomAndCollateral -- collateral not valid!\n");
        nMessageIDRet = ERR_INVALID_COLLATERAL;
        return false;
    }

    return true;
}

bool CSpysendPool::CreateNewSession(int nDenom, CTransaction txCollateral, PoolMessage& nMessageIDRet)
{
    if(!fEternityNode || nSessionID != 0) return false;

    // new session can only be started in idle mode
    if(nState != POOL_STATE_IDLE) {
        nMessageIDRet = ERR_MODE;
        LogPrintf("CSpysendPool::CreateNewSession -- incompatible mode: nState=%d\n", nState);
        return false;
    }

    if(!IsAcceptableDenomAndCollateral(nDenom, txCollateral, nMessageIDRet)) {
        return false;
    }

    // start new session
    nMessageIDRet = MSG_NOERR;
    nSessionID = GetRandInt(999999)+1;
    nSessionDenom = nDenom;

    SetState(POOL_STATE_QUEUE);
    nTimeLastSuccessfulStep = GetTimeMillis();

    if(!fUnitTest) {
        //broadcast that I'm accepting entries, only if it's the first entry through
        CSpysendQueue dsq(nDenom, activeEternitynode.vin, GetTime(), false);
        LogPrint("spysend", "CSpysendPool::CreateNewSession -- signing and relaying new queue: %s\n", dsq.ToString());
        dsq.Sign();
        dsq.Relay();
        vecSpysendQueue.push_back(dsq);
    }

    vecSessionCollaterals.push_back(txCollateral);
    LogPrintf("CSpysendPool::CreateNewSession -- new session created, nSessionID: %d  nSessionDenom: %d (%s)  vecSessionCollaterals.size(): %d\n",
            nSessionID, nSessionDenom, GetDenominationsToString(nSessionDenom), vecSessionCollaterals.size());

    return true;
}

bool CSpysendPool::AddUserToExistingSession(int nDenom, CTransaction txCollateral, PoolMessage& nMessageIDRet)
{
    if(!fEternityNode || nSessionID == 0 || IsSessionReady()) return false;

    if(!IsAcceptableDenomAndCollateral(nDenom, txCollateral, nMessageIDRet)) {
        return false;
    }

    // we only add new users to an existing session when we are in queue mode
    if(nState != POOL_STATE_QUEUE) {
        nMessageIDRet = ERR_MODE;
        LogPrintf("CSpysendPool::AddUserToExistingSession -- incompatible mode: nState=%d\n", nState);
        return false;
    }

    if(nDenom != nSessionDenom) {
        LogPrintf("CSpysendPool::AddUserToExistingSession -- incompatible denom %d (%s) != nSessionDenom %d (%s)\n",
                    nDenom, GetDenominationsToString(nDenom), nSessionDenom, GetDenominationsToString(nSessionDenom));
        nMessageIDRet = ERR_DENOM;
        return false;
    }

    // count new user as accepted to an existing session

    nMessageIDRet = MSG_NOERR;
    nTimeLastSuccessfulStep = GetTimeMillis();
    vecSessionCollaterals.push_back(txCollateral);

    LogPrintf("CSpysendPool::AddUserToExistingSession -- new user accepted, nSessionID: %d  nSessionDenom: %d (%s)  vecSessionCollaterals.size(): %d\n",
            nSessionID, nSessionDenom, GetDenominationsToString(nSessionDenom), vecSessionCollaterals.size());

    return true;
}

/*  Create a nice string to show the denominations
    Function returns as follows (for 4 denominations):
        ( bit on if present )
        bit 0           - 100
        bit 1           - 10
        bit 2           - 1
        bit 3           - .1
        bit 4 and so on - out-of-bounds
        none of above   - non-denom
*/
std::string CSpysendPool::GetDenominationsToString(int nDenom)
{
    std::string strDenom = "";
    int nMaxDenoms = vecSpySendDenominations.size();

    if(nDenom >= (1 << nMaxDenoms)) {
        return "out-of-bounds";
    }

    for (int i = 0; i < nMaxDenoms; ++i) {
        if(nDenom & (1 << i)) {
            strDenom += (strDenom.empty() ? "" : "+") + FormatMoney(vecSpySendDenominations[i]);
        }
    }

    if(strDenom.empty()) {
        return "non-denom";
    }

    return strDenom;
}

int CSpysendPool::GetDenominations(const std::vector<CTxDSOut>& vecTxDSOut)
{
    std::vector<CTxOut> vecTxOut;

    BOOST_FOREACH(CTxDSOut out, vecTxDSOut)
        vecTxOut.push_back(out);

    return GetDenominations(vecTxOut);
}

/*  Return a bitshifted integer representing the denominations in this list
    Function returns as follows (for 4 denominations):
        ( bit on if present )
        100       - bit 0
        10        - bit 1
        1         - bit 2
        .1        - bit 3
        non-denom - 0, all bits off
*/
int CSpysendPool::GetDenominations(const std::vector<CTxOut>& vecTxOut, bool fSingleRandomDenom)
{
    std::vector<std::pair<CAmount, int> > vecDenomUsed;

    // make a list of denominations, with zero uses
    BOOST_FOREACH(CAmount nDenomValue, vecSpySendDenominations)
        vecDenomUsed.push_back(std::make_pair(nDenomValue, 0));

    // look for denominations and update uses to 1
    BOOST_FOREACH(CTxOut txout, vecTxOut) {
        bool found = false;
        BOOST_FOREACH (PAIRTYPE(CAmount, int)& s, vecDenomUsed) {
            if(txout.nValue == s.first) {
                s.second = 1;
                found = true;
            }
        }
        if(!found) return 0;
    }

    int nDenom = 0;
    int c = 0;
    // if the denomination is used, shift the bit on
    BOOST_FOREACH (PAIRTYPE(CAmount, int)& s, vecDenomUsed) {
        int bit = (fSingleRandomDenom ? GetRandInt(2) : 1) & s.second;
        nDenom |= bit << c++;
        if(fSingleRandomDenom && bit) break; // use just one random denomination
    }

    return nDenom;
}

bool CSpysendPool::GetDenominationsBits(int nDenom, std::vector<int> &vecBitsRet)
{
    // ( bit on if present, 4 denominations example )
    // bit 0 - 100ENT+1
    // bit 1 - 10ENT+1
    // bit 2 - 1ENT+1
    // bit 3 - .1ENT+1

    int nMaxDenoms = vecSpySendDenominations.size();

    if(nDenom >= (1 << nMaxDenoms)) return false;

    vecBitsRet.clear();

    for (int i = 0; i < nMaxDenoms; ++i) {
        if(nDenom & (1 << i)) {
            vecBitsRet.push_back(i);
        }
    }

    return !vecBitsRet.empty();
}

int CSpysendPool::GetDenominationsByAmounts(const std::vector<CAmount>& vecAmount)
{
    CScript scriptTmp = CScript();
    std::vector<CTxOut> vecTxOut;

    BOOST_REVERSE_FOREACH(CAmount nAmount, vecAmount) {
        CTxOut txout(nAmount, scriptTmp);
        vecTxOut.push_back(txout);
    }

    return GetDenominations(vecTxOut, true);
}

std::string CSpysendPool::GetMessageByID(PoolMessage nMessageID)
{
    switch (nMessageID) {
        case ERR_ALREADY_HAVE:          return _("Already have that input.");
        case ERR_DENOM:                 return _("No matching denominations found for mixing.");
        case ERR_ENTRIES_FULL:          return _("Entries are full.");
        case ERR_EXISTING_TX:           return _("Not compatible with existing transactions.");
        case ERR_FEES:                  return _("Transaction fees are too high.");
        case ERR_INVALID_COLLATERAL:    return _("Collateral not valid.");
        case ERR_INVALID_INPUT:         return _("Input is not valid.");
        case ERR_INVALID_SCRIPT:        return _("Invalid script detected.");
        case ERR_INVALID_TX:            return _("Transaction not valid.");
        case ERR_MAXIMUM:               return _("Value more than SpySend pool maximum allows.");
        case ERR_MN_LIST:               return _("Not in the Eternitynode list.");
        case ERR_MODE:                  return _("Incompatible mode.");
        case ERR_NON_STANDARD_PUBKEY:   return _("Non-standard public key detected.");
        case ERR_NOT_A_MN:              return _("This is not a Eternitynode.");
        case ERR_QUEUE_FULL:            return _("Eternitynode queue is full.");
        case ERR_RECENT:                return _("Last SpySend was too recent.");
        case ERR_SESSION:               return _("Session not complete!");
        case ERR_MISSING_TX:            return _("Missing input transaction information.");
        case ERR_VERSION:               return _("Incompatible version.");
        case MSG_NOERR:                 return _("No errors detected.");
        case MSG_SUCCESS:               return _("Transaction created successfully.");
        case MSG_ENTRIES_ADDED:         return _("Your entries added successfully.");
        default:                        return _("Unknown response.");
    }
}

bool CSpySendSigner::IsVinAssociatedWithPubkey(const CTxIn& txin, const CPubKey& pubkey)
{
    CScript payee;
    payee = GetScriptForDestination(pubkey.GetID());

    CTransaction tx;
    uint256 hash;
    if(GetTransaction(txin.prevout.hash, tx, Params().GetConsensus(), hash, true)) {
        BOOST_FOREACH(CTxOut out, tx.vout)
            if(out.nValue == 1000*COIN && out.scriptPubKey == payee) return true;
    }

    return false;
}

bool CSpySendSigner::GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet)
{
    CBitcoinSecret vchSecret;

    if(!vchSecret.SetString(strSecret)) return false;

    keyRet = vchSecret.GetKey();
    pubkeyRet = keyRet.GetPubKey();

    return true;
}

bool CSpySendSigner::SignMessage(std::string strMessage, std::vector<unsigned char>& vchSigRet, CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    return key.SignCompact(ss.GetHash(), vchSigRet);
}

bool CSpySendSigner::VerifyMessage(CPubKey pubkey, const std::vector<unsigned char>& vchSig, std::string strMessage, std::string& strErrorRet)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkeyFromSig;
    if(!pubkeyFromSig.RecoverCompact(ss.GetHash(), vchSig)) {
        strErrorRet = "Error recovering public key.";
        return false;
    }

    if(pubkeyFromSig.GetID() != pubkey.GetID()) {
        strErrorRet = strprintf("Keys don't match: pubkey=%s, pubkeyFromSig=%s, strMessage=%s, vchSig=%s",
                    pubkey.GetID().ToString(), pubkeyFromSig.GetID().ToString(), strMessage,
                    EncodeBase64(&vchSig[0], vchSig.size()));
        return false;
    }

    return true;
}

bool CSpySendEntry::AddScriptSig(const CTxIn& txin)
{
    BOOST_FOREACH(CTxDSIn& txdsin, vecTxDSIn) {
        if(txdsin.prevout == txin.prevout && txdsin.nSequence == txin.nSequence) {
            if(txdsin.fHasSig) return false;

            txdsin.scriptSig = txin.scriptSig;
            txdsin.prevPubKey = txin.prevPubKey;
            txdsin.fHasSig = true;

            return true;
        }
    }

    return false;
}

bool CSpysendQueue::Sign()
{
    if(!fEternityNode) return false;

    std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nDenom) + boost::lexical_cast<std::string>(nTime) + boost::lexical_cast<std::string>(fReady);

    if(!spySendSigner.SignMessage(strMessage, vchSig, activeEternitynode.keyEternitynode)) {
        LogPrintf("CSpysendQueue::Sign -- SignMessage() failed, %s\n", ToString());
        return false;
    }

    return CheckSignature(activeEternitynode.pubKeyEternitynode);
}

bool CSpysendQueue::CheckSignature(const CPubKey& pubKeyEternitynode)
{
    std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nDenom) + boost::lexical_cast<std::string>(nTime) + boost::lexical_cast<std::string>(fReady);
    std::string strError = "";

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, strError)) {
        LogPrintf("CSpysendQueue::CheckSignature -- Got bad Eternitynode queue signature: %s; error: %s\n", ToString(), strError);
        return false;
    }

    return true;
}

bool CSpysendQueue::Relay()
{
    std::vector<CNode*> vNodesCopy = CopyNodeVector();
    BOOST_FOREACH(CNode* pnode, vNodesCopy)
        if(pnode->nVersion >= MIN_SPYSEND_PEER_PROTO_VERSION)
            pnode->PushMessage(NetMsgType::DSQUEUE, (*this));

    ReleaseNodeVector(vNodesCopy);
    return true;
}

bool CSpysendBroadcastTx::Sign()
{
    if(!fEternityNode) return false;

    std::string strMessage = tx.GetHash().ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!spySendSigner.SignMessage(strMessage, vchSig, activeEternitynode.keyEternitynode)) {
        LogPrintf("CSpysendBroadcastTx::Sign -- SignMessage() failed\n");
        return false;
    }

    return CheckSignature(activeEternitynode.pubKeyEternitynode);
}

bool CSpysendBroadcastTx::CheckSignature(const CPubKey& pubKeyEternitynode)
{
    std::string strMessage = tx.GetHash().ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, strError)) {
        LogPrintf("CSpysendBroadcastTx::CheckSignature -- Got bad dstx signature, error: %s\n", strError);
        return false;
    }

    return true;
}

void CSpysendPool::RelayFinalTransaction(const CTransaction& txFinal)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if(pnode->nVersion >= MIN_SPYSEND_PEER_PROTO_VERSION)
            pnode->PushMessage(NetMsgType::DSFINALTX, nSessionID, txFinal);
}

void CSpysendPool::RelayIn(const CSpySendEntry& entry)
{
    if(!pSubmittedToEternitynode) return;

    CNode* pnode = FindNode(pSubmittedToEternitynode->addr);
    if(pnode != NULL) {
        LogPrintf("CSpysendPool::RelayIn -- found master, relaying message to %s\n", pnode->addr.ToString());
        pnode->PushMessage(NetMsgType::DSVIN, entry);
    }
}

void CSpysendPool::PushStatus(CNode* pnode, PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID)
{
    if(!pnode) return;
    pnode->PushMessage(NetMsgType::DSSTATUSUPDATE, nSessionID, (int)nState, (int)vecEntries.size(), (int)nStatusUpdate, (int)nMessageID);
}

void CSpysendPool::RelayStatus(PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if(pnode->nVersion >= MIN_SPYSEND_PEER_PROTO_VERSION)
            PushStatus(pnode, nStatusUpdate, nMessageID);
}

void CSpysendPool::RelayCompletedTransaction(PoolMessage nMessageID)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if(pnode->nVersion >= MIN_SPYSEND_PEER_PROTO_VERSION)
            pnode->PushMessage(NetMsgType::DSCOMPLETE, nSessionID, (int)nMessageID);
}

void CSpysendPool::SetState(PoolState nStateNew)
{
    if(fEternityNode && (nStateNew == POOL_STATE_ERROR || nStateNew == POOL_STATE_SUCCESS)) {
        LogPrint("spysend", "CSpysendPool::SetState -- Can't set state to ERROR or SUCCESS as a Eternitynode. \n");
        return;
    }

    LogPrintf("CSpysendPool::SetState -- nState: %d, nStateNew: %d\n", nState, nStateNew);
    nState = nStateNew;
}

void CSpysendPool::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("spysend", "CSpysendPool::UpdatedBlockTip -- pCurrentBlockIndex->nHeight: %d\n", pCurrentBlockIndex->nHeight);

    if(!fLiteMode && eternitynodeSync.IsEternitynodeListSynced()) {
        NewBlock();
    }
}

//TODO: Rename/move to core
void ThreadCheckSpySendPool()
{
    if(fLiteMode) return; // disable all Eternity specific functionality

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // Make this thread recognisable as the SpySend thread
    RenameThread("eternity-spysend");

    unsigned int nTick = 0;
    unsigned int nDoAutoNextRun = nTick + SPYSEND_AUTO_TIMEOUT_MIN;

    while (true)
    {
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        eternitynodeSync.ProcessTick();

        if(eternitynodeSync.IsBlockchainSynced() && !ShutdownRequested()) {

            nTick++;

            // make sure to check all eternitynodes first
            mnodeman.Check();

            // check if we should activate or ping every few minutes,
            // slightly postpone first run to give net thread a chance to connect to some peers
            if(nTick % ETERNITYNODE_MIN_MNP_SECONDS == 15)
                activeEternitynode.ManageState();

            if(nTick % 60 == 0) {
                mnodeman.ProcessEternitynodeConnections();
                mnodeman.CheckAndRemove();
                enpayments.CheckAndRemove();
                instantsend.CheckAndRemove();
            }
            if(fEternityNode && (nTick % (60 * 5) == 0)) {
                mnodeman.DoFullVerificationStep();
            }

            if(nTick % (60 * 5) == 0) {
                governance.DoMaintenance();
            }

            spySendPool.CheckTimeout();
            spySendPool.CheckForCompleteQueue();

            if(nDoAutoNextRun == nTick) {
                spySendPool.DoAutomaticDenominating();
                nDoAutoNextRun = nTick + SPYSEND_AUTO_TIMEOUT_MIN + GetRandInt(SPYSEND_AUTO_TIMEOUT_MAX - SPYSEND_AUTO_TIMEOUT_MIN);
            }
        }
    }
}
