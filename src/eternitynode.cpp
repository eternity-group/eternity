// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeeternitynode.h"
#include "consensus/validation.h"
#include "spysend.h"
#include "init.h"
#include "governance.h"
#include "eternitynode.h"
#include "eternitynode-payments.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "util.h"

#include <boost/lexical_cast.hpp>


CEternitynode::CEternitynode() :
    vin(),
    addr(),
    pubKeyCollateralAddress(),
    pubKeyEternitynode(),
    lastPing(),
    vchSig(),
    sigTime(GetAdjustedTime()),
    nLastDsq(0),
    nTimeLastChecked(0),
    nTimeLastPaid(0),
    nTimeLastWatchdogVote(0),
    nActiveState(ETERNITYNODE_ENABLED),
    nCacheCollateralBlock(0),
    nBlockLastPaid(0),
    nProtocolVersion(PROTOCOL_VERSION),
    nPoSeBanScore(0),
    nPoSeBanHeight(0),
    fAllowMixingTx(true),
    fUnitTest(false)
{}

CEternitynode::CEternitynode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyEternitynodeNew, int nProtocolVersionIn) :
    vin(vinNew),
    addr(addrNew),
    pubKeyCollateralAddress(pubKeyCollateralAddressNew),
    pubKeyEternitynode(pubKeyEternitynodeNew),
    lastPing(),
    vchSig(),
    sigTime(GetAdjustedTime()),
    nLastDsq(0),
    nTimeLastChecked(0),
    nTimeLastPaid(0),
    nTimeLastWatchdogVote(0),
    nActiveState(ETERNITYNODE_ENABLED),
    nCacheCollateralBlock(0),
    nBlockLastPaid(0),
    nProtocolVersion(nProtocolVersionIn),
    nPoSeBanScore(0),
    nPoSeBanHeight(0),
    fAllowMixingTx(true),
    fUnitTest(false)
{}

CEternitynode::CEternitynode(const CEternitynode& other) :
    vin(other.vin),
    addr(other.addr),
    pubKeyCollateralAddress(other.pubKeyCollateralAddress),
    pubKeyEternitynode(other.pubKeyEternitynode),
    lastPing(other.lastPing),
    vchSig(other.vchSig),
    sigTime(other.sigTime),
    nLastDsq(other.nLastDsq),
    nTimeLastChecked(other.nTimeLastChecked),
    nTimeLastPaid(other.nTimeLastPaid),
    nTimeLastWatchdogVote(other.nTimeLastWatchdogVote),
    nActiveState(other.nActiveState),
    nCacheCollateralBlock(other.nCacheCollateralBlock),
    nBlockLastPaid(other.nBlockLastPaid),
    nProtocolVersion(other.nProtocolVersion),
    nPoSeBanScore(other.nPoSeBanScore),
    nPoSeBanHeight(other.nPoSeBanHeight),
    fAllowMixingTx(other.fAllowMixingTx),
    fUnitTest(other.fUnitTest)
{}

CEternitynode::CEternitynode(const CEternitynodeBroadcast& mnb) :
    vin(mnb.vin),
    addr(mnb.addr),
    pubKeyCollateralAddress(mnb.pubKeyCollateralAddress),
    pubKeyEternitynode(mnb.pubKeyEternitynode),
    lastPing(mnb.lastPing),
    vchSig(mnb.vchSig),
    sigTime(mnb.sigTime),
    nLastDsq(0),
    nTimeLastChecked(0),
    nTimeLastPaid(0),
    nTimeLastWatchdogVote(mnb.sigTime),
    nActiveState(mnb.nActiveState),
    nCacheCollateralBlock(0),
    nBlockLastPaid(0),
    nProtocolVersion(mnb.nProtocolVersion),
    nPoSeBanScore(0),
    nPoSeBanHeight(0),
    fAllowMixingTx(true),
    fUnitTest(false)
{}

//
// When a new eternitynode broadcast is sent, update our information
//
bool CEternitynode::UpdateFromNewBroadcast(CEternitynodeBroadcast& mnb)
{
    if(mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyEternitynode = mnb.pubKeyEternitynode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if(mnb.lastPing == CEternitynodePing() || (mnb.lastPing != CEternitynodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos))) {
        lastPing = mnb.lastPing;
        mnodeman.mapSeenEternitynodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Eternitynode privkey...
    if(fEternityNode && pubKeyEternitynode == activeEternitynode.pubKeyEternitynode) {
        nPoSeBanScore = -ETERNITYNODE_POSE_BAN_MAX_SCORE;
        if(nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeEternitynode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CEternitynode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Eternitynode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CEternitynode::CalculateScore(const uint256& blockHash)
{
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CEternitynode::Check(bool fForce)
{
    LOCK(cs);

    if(ShutdownRequested()) return;

    if(!fForce && (GetTime() - nTimeLastChecked < ETERNITYNODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state\n", vin.prevout.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if(IsOutpointSpent()) return;

    int nHeight = 0;
    if(!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) return;

        CCoins coins;
        if(!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
           (unsigned int)vin.prevout.n>=coins.vout.size() ||
           coins.vout[vin.prevout.n].IsNull()) {
            nActiveState = ETERNITYNODE_OUTPOINT_SPENT;
            LogPrint("eternitynode", "CEternitynode::Check -- Failed to find Eternitynode UTXO, eternitynode=%s\n", vin.prevout.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if(IsPoSeBanned()) {
        if(nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Eternitynode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CEternitynode::Check -- Eternitynode %s is unbanned and back in list now\n", vin.prevout.ToStringShort());
        DecreasePoSeBanScore();
    } else if(nPoSeBanScore >= ETERNITYNODE_POSE_BAN_MAX_SCORE) {
        nActiveState = ETERNITYNODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CEternitynode::Check -- Eternitynode %s is banned till block %d now\n", vin.prevout.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurEternitynode = fEternityNode && activeEternitynode.pubKeyEternitynode == pubKeyEternitynode;

                   // eternitynode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < enpayments.GetMinEternitynodePaymentsProto() ||
                   // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                   (fOurEternitynode && nProtocolVersion < PROTOCOL_VERSION);

    if(fRequireUpdate) {
        nActiveState = ETERNITYNODE_UPDATE_REQUIRED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old eternitynodes on start, give them a chance to receive updates...
    bool fWaitForPing = !eternitynodeSync.IsEternitynodeListSynced() && !IsPingedWithin(ETERNITYNODE_MIN_MNP_SECONDS);

    if(fWaitForPing && !fOurEternitynode) {
        // ...but if it was already expired before the initial check - return right away
        if(IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state, waiting for ping\n", vin.prevout.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own eternitynode
    if(!fWaitForPing || fOurEternitynode) {

        if(!IsPingedWithin(ETERNITYNODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = ETERNITYNODE_NEW_START_REQUIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = eternitynodeSync.IsSynced() && mnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetTime() - nTimeLastWatchdogVote) > ETERNITYNODE_WATCHDOG_MAX_SECONDS));

        LogPrint("eternitynode", "CEternitynode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetTime()=%d, fWatchdogExpired=%d\n",
                vin.prevout.ToStringShort(), nTimeLastWatchdogVote, GetTime(), fWatchdogExpired);

        if(fWatchdogExpired) {
            nActiveState = ETERNITYNODE_WATCHDOG_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        if(!IsPingedWithin(ETERNITYNODE_EXPIRATION_SECONDS)) {
            nActiveState = ETERNITYNODE_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if(lastPing.sigTime - sigTime < ETERNITYNODE_MIN_MNP_SECONDS) {
        nActiveState = ETERNITYNODE_PRE_ENABLED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    nActiveState = ETERNITYNODE_ENABLED; // OK
    if(nActiveStatePrev != nActiveState) {
        LogPrint("eternitynode", "CEternitynode::Check -- Eternitynode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
    }
}

bool CEternitynode::IsValidNetAddr()
{
    return IsValidNetAddr(addr);
}

bool CEternitynode::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
            (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

eternitynode_info_t CEternitynode::GetInfo()
{
    eternitynode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyEternitynode = pubKeyEternitynode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nTimeLastPing = lastPing.sigTime;
    info.nActiveState = nActiveState;
    info.nProtocolVersion = nProtocolVersion;
    info.fInfoValid = true;
    return info;
}

std::string CEternitynode::StateToString(int nStateIn)
{
    switch(nStateIn) {
        case ETERNITYNODE_PRE_ENABLED:            return "PRE_ENABLED";
        case ETERNITYNODE_ENABLED:                return "ENABLED";
        case ETERNITYNODE_EXPIRED:                return "EXPIRED";
        case ETERNITYNODE_OUTPOINT_SPENT:         return "OUTPOINT_SPENT";
        case ETERNITYNODE_UPDATE_REQUIRED:        return "UPDATE_REQUIRED";
        case ETERNITYNODE_WATCHDOG_EXPIRED:       return "WATCHDOG_EXPIRED";
        case ETERNITYNODE_NEW_START_REQUIRED:     return "NEW_START_REQUIRED";
        case ETERNITYNODE_POSE_BAN:               return "POSE_BAN";
        default:                                return "UNKNOWN";
    }
}

std::string CEternitynode::GetStateString() const
{
    return StateToString(nActiveState);
}

std::string CEternitynode::GetStatus() const
{
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

int CEternitynode::GetCollateralAge()
{
    int nHeight;
    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain || !chainActive.Tip()) return -1;
        nHeight = chainActive.Height();
    }

    if (nCacheCollateralBlock == 0) {
        int nInputAge = GetInputAge(vin);
        if(nInputAge > 0) {
            nCacheCollateralBlock = nHeight - nInputAge;
        } else {
            return nInputAge;
        }
    }

    return nHeight - nCacheCollateralBlock;
}

void CEternitynode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex) return;

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    // LogPrint("eternitynode", "CEternitynode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());

    LOCK(cs_mapEternitynodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
        if(enpayments.mapEternitynodeBlocks.count(BlockReading->nHeight) &&
            enpayments.mapEternitynodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2))
        {
            CBlock block;
            if(!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) // shouldn't really happen
                continue;

            CAmount nEternitynodePayment = GetEternitynodePayment(BlockReading->nHeight, block.vtx[0].GetValueOut());

            BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
                if(mnpayee == txout.scriptPubKey && nEternitynodePayment == txout.nValue) {
                    nBlockLastPaid = BlockReading->nHeight;
                    nTimeLastPaid = BlockReading->nTime;
                    LogPrint("eternitynode", "CEternitynode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
                    return;
                }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this eternitynode wasn't found in latest enpayments blocks
    // or it was found in enpayments blocks but wasn't found in the blockchain.
    // LogPrint("eternitynode", "CEternitynode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
}

bool CEternitynodeBroadcast::Create(std::string strService, std::string strKeyEternitynode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CEternitynodeBroadcast &mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyEternitynodeNew;
    CKey keyEternitynodeNew;

    //need correct blocks to send ping
    if(!fOffline && !eternitynodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Eternitynode";
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if(!spySendSigner.GetKeysFromSecret(strKeyEternitynode, keyEternitynodeNew, pubKeyEternitynodeNew)) {
        strErrorRet = strprintf("Invalid eternitynode key %s", strKeyEternitynode);
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if(!pwalletMain->GetEternitynodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for eternitynode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for eternitynode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for eternitynode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyEternitynodeNew, pubKeyEternitynodeNew, strErrorRet, mnbRet);
}

bool CEternitynodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyEternitynodeNew, CPubKey pubKeyEternitynodeNew, std::string &strErrorRet, CEternitynodeBroadcast &mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("eternitynode", "CEternitynodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyEternitynodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyEternitynodeNew.GetID().ToString());


    CEternitynodePing mnp(txin);
    if(!mnp.Sign(keyEternitynodeNew, pubKeyEternitynodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, eternitynode=%s", txin.prevout.ToStringShort());
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CEternitynodeBroadcast();
        return false;
    }

    mnbRet = CEternitynodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyEternitynodeNew, PROTOCOL_VERSION);

    if(!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address, eternitynode=%s", txin.prevout.ToStringShort());
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CEternitynodeBroadcast();
        return false;
    }

    mnbRet.lastPing = mnp;
    if(!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, eternitynode=%s", txin.prevout.ToStringShort());
        LogPrintf("CEternitynodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CEternitynodeBroadcast();
        return false;
    }

    return true;
}

bool CEternitynodeBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    // make sure addr is valid
    if(!IsValidNetAddr()) {
        LogPrintf("CEternitynodeBroadcast::SimpleCheck -- Invalid addr, rejected: eternitynode=%s  addr=%s\n",
                    vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CEternitynodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: eternitynode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if(lastPing == CEternitynodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = ETERNITYNODE_EXPIRED;
    }

    if(nProtocolVersion < enpayments.GetMinEternitynodePaymentsProto()) {
        LogPrintf("CEternitynodeBroadcast::SimpleCheck -- ignoring outdated Eternitynode: eternitynode=%s  nProtocolVersion=%d\n", vin.prevout.ToStringShort(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("CEternitynodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyEternitynode.GetID());

    if(pubkeyScript2.size() != 25) {
        LogPrintf("CEternitynodeBroadcast::SimpleCheck -- pubKeyEternitynode has the wrong size\n");
        nDos = 100;
        return false;
    }

    if(!vin.scriptSig.empty()) {
        LogPrintf("CEternitynodeBroadcast::SimpleCheck -- Ignore Not Empty ScriptSig %s\n",vin.ToString());
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != mainnetDefaultPort) return false;
    } else if(addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CEternitynodeBroadcast::Update(CEternitynode* pmn, int& nDos)
{
    nDos = 0;

    if(pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenEternitynodeBroadcast in CEternitynodeMan::CheckMnbAndUpdateEternitynodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if(pmn->sigTime > sigTime) {
        LogPrintf("CEternitynodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Eternitynode %s %s\n",
                      sigTime, pmn->sigTime, vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // eternitynode is banned by PoSe
    if(pmn->IsPoSeBanned()) {
        LogPrintf("CEternitynodeBroadcast::Update -- Banned by PoSe, eternitynode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if(pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CEternitynodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CEternitynodeBroadcast::Update -- CheckSignature() failed, eternitynode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // if ther was no eternitynode broadcast recently or if it matches our Eternitynode privkey...
    if(!pmn->IsBroadcastedWithin(ETERNITYNODE_MIN_MNB_SECONDS) || (fEternityNode && pubKeyEternitynode == activeEternitynode.pubKeyEternitynode)) {
        // take the newest entry
        LogPrintf("CEternitynodeBroadcast::Update -- Got UPDATED Eternitynode entry: addr=%s\n", addr.ToString());
        if(pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            Relay();
        }
        eternitynodeSync.AddedEternitynodeList();
    }

    return true;
}

bool CEternitynodeBroadcast::CheckOutpoint(int& nDos)
{
    // we are a eternitynode with the same vin (i.e. already activated) and this mnb is ours (matches our Eternitynode privkey)
    // so nothing to do here for us
    if(fEternityNode && vin.prevout == activeEternitynode.vin.prevout && pubKeyEternitynode == activeEternitynode.pubKeyEternitynode) {
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CEternitynodeBroadcast::CheckOutpoint -- CheckSignature() failed, eternitynode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) {
            // not mnb fault, let it to be checked again later
            LogPrint("eternitynode", "CEternitynodeBroadcast::CheckOutpoint -- Failed to aquire lock, addr=%s", addr.ToString());
            mnodeman.mapSeenEternitynodeBroadcast.erase(GetHash());
            return false;
        }

        CCoins coins;
        if(!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
           (unsigned int)vin.prevout.n>=coins.vout.size() ||
           coins.vout[vin.prevout.n].IsNull()) {
            LogPrint("eternitynode", "CEternitynodeBroadcast::CheckOutpoint -- Failed to find Eternitynode UTXO, eternitynode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if(coins.vout[vin.prevout.n].nValue != 1000 * COIN) {
            LogPrint("eternitynode", "CEternitynodeBroadcast::CheckOutpoint -- Eternitynode UTXO should have 1000 ENT, eternitynode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if(chainActive.Height() - coins.nHeight + 1 < Params().GetConsensus().nEternitynodeMinimumConfirmations) {
            LogPrintf("CEternitynodeBroadcast::CheckOutpoint -- Eternitynode UTXO must have at least %d confirmations, eternitynode=%s\n",
                    Params().GetConsensus().nEternitynodeMinimumConfirmations, vin.prevout.ToStringShort());
            // maybe we miss few blocks, let this mnb to be checked again later
            mnodeman.mapSeenEternitynodeBroadcast.erase(GetHash());
            return false;
        }
    }

    LogPrint("eternitynode", "CEternitynodeBroadcast::CheckOutpoint -- Eternitynode UTXO verified\n");

    // make sure the vout that was signed is related to the transaction that spawned the Eternitynode
    //  - this is expensive, so it's only done once per Eternitynode
    if(!spySendSigner.IsVinAssociatedWithPubkey(vin, pubKeyCollateralAddress)) {
        LogPrintf("CEternitynodeMan::CheckOutpoint -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 ENT tx got nEternitynodeMinimumConfirmations
    uint256 hashBlock = uint256();
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pMNIndex = (*mi).second; // block for 1000 ENT tx -> 1 confirmation
            CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + Params().GetConsensus().nEternitynodeMinimumConfirmations - 1]; // block where tx got nEternitynodeMinimumConfirmations
            if(pConfIndex->GetBlockTime() > sigTime) {
                LogPrintf("CEternitynodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Eternitynode %s %s\n",
                          sigTime, Params().GetConsensus().nEternitynodeMinimumConfirmations, pConfIndex->GetBlockTime(), vin.prevout.ToStringShort(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CEternitynodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                    pubKeyCollateralAddress.GetID().ToString() + pubKeyEternitynode.GetID().ToString() +
                    boost::lexical_cast<std::string>(nProtocolVersion);

    if(!spySendSigner.SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CEternitynodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CEternitynodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CEternitynodeBroadcast::CheckSignature(int& nDos)
{
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                    pubKeyCollateralAddress.GetID().ToString() + pubKeyEternitynode.GetID().ToString() +
                    boost::lexical_cast<std::string>(nProtocolVersion);

    LogPrint("eternitynode", "CEternitynodeBroadcast::CheckSignature -- strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

    if(!spySendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)){
        LogPrintf("CEternitynodeBroadcast::CheckSignature -- Got bad Eternitynode announce signature, error: %s\n", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CEternitynodeBroadcast::Relay()
{
    CInv inv(MSG_ETERNITYNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

CEternitynodePing::CEternitynodePing(CTxIn& vinNew)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    vin = vinNew;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}

bool CEternitynodePing::Sign(CKey& keyEternitynode, CPubKey& pubKeyEternitynode)
{
    std::string strError;
    std::string strEternityNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!spySendSigner.SignMessage(strMessage, vchSig, keyEternitynode)) {
        LogPrintf("CEternitynodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, strError)) {
        LogPrintf("CEternitynodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CEternitynodePing::CheckSignature(CPubKey& pubKeyEternitynode, int &nDos)
{
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, strError)) {
        LogPrintf("CEternitynodePing::CheckSignature -- Got bad Eternitynode ping signature, eternitynode=%s, error: %s\n", vin.prevout.ToStringShort(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CEternitynodePing::SimpleCheck(int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CEternitynodePing::SimpleCheck -- Signature rejected, too far into the future, eternitynode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("eternitynode", "CEternitynodePing::SimpleCheck -- Eternitynode ping is invalid, unknown block hash: eternitynode=%s blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("eternitynode", "CEternitynodePing::SimpleCheck -- Eternitynode ping verified: eternitynode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CEternitynodePing::CheckAndUpdate(CEternitynode* pmn, bool fFromNewBroadcast, int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- Couldn't find Eternitynode entry, eternitynode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    if(!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- eternitynode protocol is outdated, eternitynode=%s\n", vin.prevout.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- eternitynode is completely expired, new start is required, eternitynode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            LogPrintf("CEternitynodePing::CheckAndUpdate -- Eternitynode ping is invalid, block hash is too old: eternitynode=%s  blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- New ping: eternitynode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.prevout.ToStringShort());
    // update only if there is no known ping for this eternitynode or
    // last ping was more then ETERNITYNODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(ETERNITYNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- Eternitynode ping arrived too early, eternitynode=%s\n", vin.prevout.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyEternitynode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that ETERNITYNODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if(!eternitynodeSync.IsEternitynodeListSynced() && !pmn->IsPingedWithin(ETERNITYNODE_EXPIRATION_SECONDS/2)) {
        // let's bump sync timeout
        LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- bumping sync timeout, eternitynode=%s\n", vin.prevout.ToStringShort());
        eternitynodeSync.AddedEternitynodeList();
    }

    // let's store this ping as the last one
    LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- Eternitynode ping accepted, eternitynode=%s\n", vin.prevout.ToStringShort());
    pmn->lastPing = *this;

    // and update mnodeman.mapSeenEternitynodeBroadcast.lastPing which is probably outdated
    CEternitynodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenEternitynodeBroadcast.count(hash)) {
        mnodeman.mapSeenEternitynodeBroadcast[hash].second.lastPing = *this;
    }

    pmn->Check(true); // force update, ignoring cache
    if (!pmn->IsEnabled()) return false;

    LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate -- Eternitynode ping acceepted and relayed, eternitynode=%s\n", vin.prevout.ToStringShort());
    Relay();

    return true;
}

void CEternitynodePing::Relay()
{
    CInv inv(MSG_ETERNITYNODE_PING, GetHash());
    RelayInv(inv);
}

void CEternitynode::AddGovernanceVote(uint256 nGovernanceObjectHash)
{
    if(mapGovernanceObjectsVotedOn.count(nGovernanceObjectHash)) {
        mapGovernanceObjectsVotedOn[nGovernanceObjectHash]++;
    } else {
        mapGovernanceObjectsVotedOn.insert(std::make_pair(nGovernanceObjectHash, 1));
    }
}

void CEternitynode::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
{
    std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.find(nGovernanceObjectHash);
    if(it == mapGovernanceObjectsVotedOn.end()) {
        return;
    }
    mapGovernanceObjectsVotedOn.erase(it);
}

void CEternitynode::UpdateWatchdogVoteTime()
{
    LOCK(cs);
    nTimeLastWatchdogVote = GetTime();
}

/**
*   FLAG GOVERNANCE ITEMS AS DIRTY
*
*   - When eternitynode come and go on the network, we must flag the items they voted on to recalc it's cached flags
*
*/
void CEternitynode::FlagGovernanceItemsAsDirty()
{
    std::vector<uint256> vecDirty;
    {
        std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.begin();
        while(it != mapGovernanceObjectsVotedOn.end()) {
            vecDirty.push_back(it->first);
            ++it;
        }
    }
    for(size_t i = 0; i < vecDirty.size(); ++i) {
        mnodeman.AddDirtyGovernanceObjectHash(vecDirty[i]);
    }
}
