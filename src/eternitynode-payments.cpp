// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeeternitynode.h"
#include "spysend.h"
#include "governance-classes.h"
#include "eternitynode-payments.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CEternitynodePayments enpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapEternitynodeBlocks;
CCriticalSection cs_mapEternitynodePaymentVotes;

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In Eternity some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet)
{
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    if(fDebug) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0].GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) {
        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
            // NOTE: make sure SPORK_13_OLD_SUPERBLOCK_FLAG is disabled when 12.1 starts to go live
            if(eternitynodeSync.IsSynced() && !sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
                LogPrint("gobject", "IsBlockValueValid -- Client synced but budget spork is disabled, checking block value against block reward\n");
                if(!isBlockRewardValueMet) {
                    strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, budgets are disabled",
                                            nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
                }
                return isBlockRewardValueMet;
            }
            LogPrint("gobject", "IsBlockValueValid -- WARNING: Skipping budget block value checks, accepting block\n");
            // TODO: reprocess blocks to make sure they are legit?
            return true;
        }
        // LogPrint("gobject", "IsBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in budget cycle window",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // superblocks started

    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
    bool isSuperblockMaxValueMet = (block.vtx[0].GetValueOut() <= nSuperblockMaxValue);

    LogPrint("gobject", "block.vtx[0].GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0].GetValueOut(), nSuperblockMaxValue);

    if(!eternitynodeSync.IsSynced()) {
        // not enough data but at least it must NOT exceed superblock max value
        if(CSuperblock::IsValidBlockHeight(nBlockHeight)) {
            if(fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, checking superblock max bounds only\n");
            if(!isSuperblockMaxValueMet) {
                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
                                        nBlockHeight, block.vtx[0].GetValueOut(), nSuperblockMaxValue);
            }
            return isSuperblockMaxValueMet;
        }
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(block.vtx[0], nBlockHeight, blockReward)) {
                LogPrint("gobject", "IsBlockValueValid -- Valid superblock at height %d: %s", nBlockHeight, block.vtx[0].ToString());
                // all checks are done in CSuperblock::IsValid, nothing to do here
                return true;
            }

            // triggered but invalid? that's weird
            LogPrintf("IsBlockValueValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, block.vtx[0].ToString());
            // should NOT allow invalid superblocks, when superblocks are enabled
            strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
            return false;
        }
        LogPrint("gobject", "IsBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
    }

    // it MUST be a regular block
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
    if(!eternitynodeSync.IsSynced()) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if(fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    // we are still using budgets, but we have no data about them anymore,
    // we can only check eternitynode payments

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.nSuperblockStartBlock) {
        if(enpayments.IsTransactionValid(txNew, nBlockHeight)) {
            LogPrint("enpayments", "IsBlockPayeeValid -- Valid eternitynode payment at height %d: %s", nBlockHeight, txNew.ToString());
            return true;
        }

        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
        if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
            if(!sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
                LogPrint("gobject", "IsBlockPayeeValid -- ERROR: Client synced but budget spork is disabled and eternitynode payment is invalid\n");
                return false;
            }
            // NOTE: this should never happen in real, SPORK_13_OLD_SUPERBLOCK_FLAG MUST be disabled when 12.1 starts to go live
            LogPrint("gobject", "IsBlockPayeeValid -- WARNING: Probably valid budget block, have no data, accepting\n");
            // TODO: reprocess blocks to make sure they are legit?
            return true;
        }

        if(sporkManager.IsSporkActive(SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT)) {
            LogPrintf("IsBlockPayeeValid -- ERROR: Invalid eternitynode payment detected at height %d: %s", nBlockHeight, txNew.ToString());
            return false;
        }

        LogPrintf("IsBlockPayeeValid -- WARNING: Eternitynode payment enforcement is disabled, accepting any payee\n");
        return true;
    }

    // superblocks started
    // SEE IF THIS IS A VALID SUPERBLOCK

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(txNew, nBlockHeight, blockReward)) {
                LogPrint("gobject", "IsBlockPayeeValid -- Valid superblock at height %d: %s", nBlockHeight, txNew.ToString());
                return true;
            }

            LogPrintf("IsBlockPayeeValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, txNew.ToString());
            // should NOT allow such superblocks, when superblocks are enabled
            return false;
        }
        // continue validation, should pay MN
        LogPrint("gobject", "IsBlockPayeeValid -- No triggered superblock detected at height %d\n", nBlockHeight);
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockPayeeValid -- Superblocks are disabled, no superblocks allowed\n");
    }

    // IF THIS ISN'T A SUPERBLOCK OR SUPERBLOCK IS INVALID, IT SHOULD PAY A ETERNITYNODE DIRECTLY
    if(enpayments.IsTransactionValid(txNew, nBlockHeight)) {
        LogPrint("enpayments", "IsBlockPayeeValid -- Valid eternitynode payment at height %d: %s", nBlockHeight, txNew.ToString());
        return true;
    }

    if(sporkManager.IsSporkActive(SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT)) {
        LogPrintf("IsBlockPayeeValid -- ERROR: Invalid eternitynode payment detected at height %d: %s", nBlockHeight, txNew.ToString());
        return false;
    }

    LogPrintf("IsBlockPayeeValid -- WARNING: Eternitynode payment enforcement is disabled, accepting any payee\n");
    return true;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount blockEvolution, CTxOut& txoutEternitynodeRet, std::vector<CTxOut>& voutSuperblockRet)
{
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED) &&
        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            LogPrint("gobject", "FillBlockPayments -- triggered superblock creation at height %d\n", nBlockHeight);
            CSuperblockManager::CreateSuperblock(txNew, nBlockHeight, voutSuperblockRet);
            return;
    }
	
	if(  sporkManager.GetSporkValue(SPORK_6_EVOLUTION_PAYMENTS) == 1  ){	
		CSuperblockManager::CreateEvolution(  txNew, nBlockHeight, blockEvolution, voutSuperblockRet  );
    }		
	

    // FILL BLOCK PAYEE WITH ETERNITYNODE PAYMENT OTHERWISE
    enpayments.FillBlockPayee(txNew, nBlockHeight, blockReward, txoutEternitynodeRet);
    LogPrint("enpayments", "FillBlockPayments -- nBlockHeight %d blockReward %lld txoutEternitynodeRet %s txNew %s",
                            nBlockHeight, blockReward, txoutEternitynodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
    if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        return CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
    }

    // OTHERWISE, PAY ETERNITYNODE
    return enpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CEternitynodePayments::Clear()
{
    LOCK2(cs_mapEternitynodeBlocks, cs_mapEternitynodePaymentVotes);
    mapEternitynodeBlocks.clear();
    mapEternitynodePaymentVotes.clear();
}

bool CEternitynodePayments::CanVote(COutPoint outEternitynode, int nBlockHeight)
{
    LOCK(cs_mapEternitynodePaymentVotes);

    if (mapEternitynodesLastVote.count(outEternitynode) && mapEternitynodesLastVote[outEternitynode] == nBlockHeight) {
        return false;
    }

    //record this eternitynode voted
    mapEternitynodesLastVote[outEternitynode] = nBlockHeight;
    return true;
}

/**
*   FillBlockPayee
*
*   Fill Eternitynode ONLY payment block
*/

void CEternitynodePayments::FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutEternitynodeRet)
{
    // make sure it's not filled yet
    txoutEternitynodeRet = CTxOut();

    CScript payee;

    if(!enpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no eternitynode detected...
        int nCount = 0;
        CEternitynode *winningNode = mnodeman.GetNextEternitynodeInQueueForPayment(nBlockHeight, true, nCount);
        if(!winningNode) {
            // ...and we can't calculate it on our own
            LogPrintf("CEternitynodePayments::FillBlockPayee -- Failed to detect eternitynode to pay\n");
            return;
        }
        // fill payee with locally calculated winner and hope for the best
        payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
    }

    // GET ETERNITYNODE PAYMENT VARIABLES SETUP
    CAmount eternitynodePayment = GetEternitynodePayment(nBlockHeight, blockReward);

    // split reward between miner ...
    txNew.vout[0].nValue -= eternitynodePayment;
    // ... and eternitynode
    txoutEternitynodeRet = CTxOut(eternitynodePayment, payee);
    txNew.vout.push_back(txoutEternitynodeRet);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CEternitynodePayments::FillBlockPayee -- Eternitynode payment %lld to %s\n", eternitynodePayment, address2.ToString());
}

int CEternitynodePayments::GetMinEternitynodePaymentsProto() {
    return sporkManager.IsSporkActive(SPORK_10_ETERNITYNODE_PAY_UPDATED_NODES)
            ? MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_2
            : MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_1;
}

void CEternitynodePayments::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // Ignore any payments messages until eternitynode list is synced
    if(!eternitynodeSync.IsEternitynodeListSynced()) return;

    if(fLiteMode) return; // disable all Eternity specific functionality

    if (strCommand == NetMsgType::ETERNITYNODEPAYMENTSYNC) { //Eternitynode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after eternitynode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!eternitynodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::ETERNITYNODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("ETERNITYNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::ETERNITYNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrintf("ETERNITYNODEPAYMENTSYNC -- Sent Eternitynode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::ETERNITYNODEPAYMENTVOTE) { // Eternitynode Payments Vote for the Winner

        CEternitynodePaymentVote vote;
        vRecv >> vote;

        if(pfrom->nVersion < GetMinEternitynodePaymentsProto()) return;

        if(!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapEternitynodePaymentVotes);
            if(mapEternitynodePaymentVotes.count(nHash)) {
                LogPrint("enpayments", "ETERNITYNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapEternitynodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapEternitynodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight+20) {
            LogPrint("enpayments", "ETERNITYNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("enpayments", "ETERNITYNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if(!CanVote(vote.vinEternitynode.prevout, vote.nBlockHeight)) {
            LogPrintf("ETERNITYNODEPAYMENTVOTE -- eternitynode already voted, eternitynode=%s\n", vote.vinEternitynode.prevout.ToStringShort());
            return;
        }

        eternitynode_info_t mnInfo = mnodeman.GetEternitynodeInfo(vote.vinEternitynode);
        if(!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("ETERNITYNODEPAYMENTVOTE -- eternitynode is missing %s\n", vote.vinEternitynode.prevout.ToStringShort());
            mnodeman.AskForEN(pfrom, vote.vinEternitynode);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyEternitynode, pCurrentBlockIndex->nHeight, nDos)) {
            if(nDos) {
                LogPrintf("ETERNITYNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("enpayments", "ETERNITYNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForEN(pfrom, vote.vinEternitynode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("enpayments", "ETERNITYNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, vote.vinEternitynode.prevout.ToStringShort());

        if(AddPaymentVote(vote)){
            vote.Relay();
            eternitynodeSync.AddedPaymentVote();
        }
    }
}

bool CEternitynodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinEternitynode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    if(!spySendSigner.SignMessage(strMessage, vchSig, activeEternitynode.keyEternitynode)) {
        LogPrintf("CEternitynodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!spySendSigner.VerifyMessage(activeEternitynode.pubKeyEternitynode, vchSig, strMessage, strError)) {
        LogPrintf("CEternitynodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CEternitynodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapEternitynodeBlocks.count(nBlockHeight)){
        return mapEternitynodeBlocks[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this eternitynode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CEternitynodePayments::IsScheduled(CEternitynode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapEternitynodeBlocks);

    if(!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for(int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++){
        if(h == nNotBlockHeight) continue;
        if(mapEternitynodeBlocks.count(h) && mapEternitynodeBlocks[h].GetBestPayee(payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CEternitynodePayments::AddPaymentVote(const CEternitynodePaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    if(HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapEternitynodeBlocks, cs_mapEternitynodePaymentVotes);

    mapEternitynodePaymentVotes[vote.GetHash()] = vote;

    if(!mapEternitynodeBlocks.count(vote.nBlockHeight)) {
       CEternitynodeBlockPayees blockPayees(vote.nBlockHeight);
       mapEternitynodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapEternitynodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CEternitynodePayments::HasVerifiedPaymentVote(uint256 hashIn)
{
    LOCK(cs_mapEternitynodePaymentVotes);
    std::map<uint256, CEternitynodePaymentVote>::iterator it = mapEternitynodePaymentVotes.find(hashIn);
    return it != mapEternitynodePaymentVotes.end() && it->second.IsVerified();
}

void CEternitynodeBlockPayees::AddPayee(const CEternitynodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayees) {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CEternitynodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CEternitynodeBlockPayees::GetBestPayee(CScript& payeeRet)
{
    LOCK(cs_vecPayees);

    if(!vecPayees.size()) {
        LogPrint("enpayments", "CEternitynodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CEternitynodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CEternitynodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("enpayments", "CEternitynodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CEternitynodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nEternitynodePayment = GetEternitynodePayment(nBlockHeight, txNew.GetValueOut());

    //require at least ENPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least ENPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < ENPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= ENPAYMENTS_SIGNATURES_REQUIRED) {
            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nEternitynodePayment == txout.nValue) {
                    LogPrint("enpayments", "CEternitynodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrintf("CEternitynodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f ENT\n", strPayeesPossible, (float)nEternitynodePayment/COIN);
    return false;
}

std::string CEternitynodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

std::string CEternitynodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapEternitynodeBlocks);

    if(mapEternitynodeBlocks.count(nBlockHeight)){
        return mapEternitynodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CEternitynodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapEternitynodeBlocks);

    if(mapEternitynodeBlocks.count(nBlockHeight)){
        return mapEternitynodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CEternitynodePayments::CheckAndRemove()
{
    if(!pCurrentBlockIndex) return;

    LOCK2(cs_mapEternitynodeBlocks, cs_mapEternitynodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CEternitynodePaymentVote>::iterator it = mapEternitynodePaymentVotes.begin();
    while(it != mapEternitynodePaymentVotes.end()) {
        CEternitynodePaymentVote vote = (*it).second;

        if(pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) {
            LogPrint("enpayments", "CEternitynodePayments::CheckAndRemove -- Removing old Eternitynode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapEternitynodePaymentVotes.erase(it++);
            mapEternitynodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CEternitynodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CEternitynodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError)
{
    CEternitynode* pmn = mnodeman.Find(vinEternitynode);

    if(!pmn) {
        strError = strprintf("Unknown Eternitynode: prevout=%s", vinEternitynode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Eternitynode
        if(eternitynodeSync.IsEternitynodeListSynced()) {
            mnodeman.AskForEN(pnode, vinEternitynode);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if(nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_ETERNITYNODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = enpayments.GetMinEternitynodePaymentsProto();
    } else {
        // allow non-updated eternitynodes for old blocks
        nMinRequiredProtocol = MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_1;
    }

    if(pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Eternitynode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only eternitynodes should try to check eternitynode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify eternitynode rank for future block votes only.
    if(!fEternityNode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetEternitynodeRank(vinEternitynode, nBlockHeight - 101, nMinRequiredProtocol, false);

    if(nRank == -1) {
        LogPrint("enpayments", "CEternitynodePaymentVote::IsValid -- Can't calculate rank for eternitynode %s\n",
                    vinEternitynode.prevout.ToStringShort());
        return false;
    }

    if(nRank > ENPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have eternitynodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Eternitynode is not in the top %d (%d)", ENPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if(nRank > ENPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Eternitynode is not in the top %d (%d)", ENPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogPrintf("CEternitynodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CEternitynodePayments::ProcessBlock(int nBlockHeight)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(fLiteMode || !fEternityNode) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about eternitynodes.
    if(!eternitynodeSync.IsEternitynodeListSynced()) return false;

    int nRank = mnodeman.GetEternitynodeRank(activeEternitynode.vin, nBlockHeight - 101, GetMinEternitynodePaymentsProto(), false);

    if (nRank == -1) {
        LogPrint("enpayments", "CEternitynodePayments::ProcessBlock -- Unknown Eternitynode\n");
        return false;
    }

    if (nRank > ENPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("enpayments", "CEternitynodePayments::ProcessBlock -- Eternitynode not in the top %d (%d)\n", ENPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT ETERNITYNODE WHICH SHOULD BE PAID

    LogPrintf("CEternitynodePayments::ProcessBlock -- Start: nBlockHeight=%d, eternitynode=%s\n", nBlockHeight, activeEternitynode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CEternitynode *pmn = mnodeman.GetNextEternitynodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) {
        LogPrintf("CEternitynodePayments::ProcessBlock -- ERROR: Failed to find eternitynode to pay\n");
        return false;
    }

    LogPrintf("CEternitynodePayments::ProcessBlock -- Eternitynode found by GetNextEternitynodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CEternitynodePaymentVote voteNew(activeEternitynode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CEternitynodePayments::ProcessBlock -- vote: payee=%s, nBlockHeight=%d\n", address2.ToString(), nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR ETERNITYNODE KEYS

    LogPrintf("CEternitynodePayments::ProcessBlock -- Signing vote\n");
    if (voteNew.Sign()) {
        LogPrintf("CEternitynodePayments::ProcessBlock -- AddPaymentVote()\n");

        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CEternitynodePaymentVote::Relay()
{
    // do not relay until synced
    if (!eternitynodeSync.IsWinnersListSynced()) return;
    CInv inv(MSG_ETERNITYNODE_PAYMENT_VOTE, GetHash());
    RelayInv(inv);
}

bool CEternitynodePaymentVote::CheckSignature(const CPubKey& pubKeyEternitynode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinEternitynode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    std::string strError = "";
    if (!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if(eternitynodeSync.IsEternitynodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CEternitynodePaymentVote::CheckSignature -- Got bad Eternitynode payment signature, eternitynode=%s, error: %s", vinEternitynode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CEternitynodePaymentVote::ToString() const
{
    std::ostringstream info;

    info << vinEternitynode.prevout.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee) <<
            ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CEternitynodePayments::Sync(CNode* pnode)
{
    LOCK(cs_mapEternitynodeBlocks);

    if(!pCurrentBlockIndex) return;

    int nInvCount = 0;

    for(int h = pCurrentBlockIndex->nHeight; h < pCurrentBlockIndex->nHeight + 20; h++) {
        if(mapEternitynodeBlocks.count(h)) {
            BOOST_FOREACH(CEternitynodePayee& payee, mapEternitynodeBlocks[h].vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256& hash, vecVoteHashes) {
                    if(!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_ETERNITYNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CEternitynodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, ETERNITYNODE_SYNC_ENW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CEternitynodePayments::RequestLowDataPaymentBlocks(CNode* pnode)
{
    if(!pCurrentBlockIndex) return;

    LOCK2(cs_main, cs_mapEternitynodeBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while(pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if(!mapEternitynodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_ETERNITYNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CEternitynodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CEternitynodeBlockPayees>::iterator it = mapEternitynodeBlocks.begin();

    while(it != mapEternitynodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CEternitynodePayee& payee, it->second.vecPayees) {
            if(payee.GetVoteCount() >= ENPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (ENPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if(fFound || nTotalVotes >= (ENPAYMENTS_SIGNATURES_TOTAL + ENPAYMENTS_SIGNATURES_REQUIRED)/2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
        DBG (
            // Let's see why this failed
            BOOST_FOREACH(CEternitynodePayee& payee, it->second.vecPayees) {
                CTxDestination address1;
                ExtractDestination(payee.GetPayee(), address1);
                CBitcoinAddress address2(address1);
                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
            }
            printf("block %d votes total %d\n", it->first, nTotalVotes);
        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if(GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_ETERNITYNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if(vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CEternitynodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) {
        LogPrintf("CEternitynodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
    }
}

std::string CEternitynodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapEternitynodePaymentVotes.size() <<
            ", Blocks: " << (int)mapEternitynodeBlocks.size();

    return info.str();
}

bool CEternitynodePayments::IsEnoughData()
{
    float nAverageVotes = (ENPAYMENTS_SIGNATURES_TOTAL + ENPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CEternitynodePayments::GetStorageLimit()
{
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CEternitynodePayments::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("enpayments", "CEternitynodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    ProcessBlock(pindex->nHeight + 10);
}
