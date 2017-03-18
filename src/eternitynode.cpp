// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "eternitynode.h"
#include "eternitynodeman.h"
#include "spysend.h"
#include "util.h"
#include "sync.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenEternitynodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if(nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if(mapCacheBlockHashes.count(nBlockHeight)){
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex *BlockLastSolved = chainActive.Tip();
    const CBlockIndex *BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight+1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if(nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight+1)-nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nBlocksAgo){
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CEternitynode::CEternitynode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubkey = CPubKey();
    pubkey2 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = ETERNITYNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CEternitynodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

CEternitynode::CEternitynode(const CEternitynode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubkey = other.pubkey;
    pubkey2 = other.pubkey2;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
}

CEternitynode::CEternitynode(const CEternitynodeBroadcast& enb)
{
    LOCK(cs);
    vin = enb.vin;
    addr = enb.addr;
    pubkey = enb.pubkey;
    pubkey2 = enb.pubkey2;
    sig = enb.sig;
    activeState = ETERNITYNODE_ENABLED;
    sigTime = enb.sigTime;
    lastPing = enb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = enb.protocolVersion;
    nLastDsq = enb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
}

//
// When a new eternitynode broadcast is sent, update our information
//
bool CEternitynode::UpdateFromNewBroadcast(CEternitynodeBroadcast& enb)
{
    if(enb.sigTime > sigTime) {    
        pubkey2 = enb.pubkey2;
        sigTime = enb.sigTime;
        sig = enb.sig;
        protocolVersion = enb.protocolVersion;
        addr = enb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if(enb.lastPing == CEternitynodePing() || (enb.lastPing != CEternitynodePing() && enb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = enb.lastPing;
            enodeman.mapSeenEternitynodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Eternitynode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CEternitynode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if(chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if(!GetBlockHash(hash, nBlockHeight)) {
        LogPrintf("CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CEternitynode::Check(bool forceCheck)
{
    if(ShutdownRequested()) return;

    if(!forceCheck && (GetTime() - lastTimeChecked < ETERNITYNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if(activeState == ETERNITYNODE_VIN_SPENT) return;


    if(!IsPingedWithin(ETERNITYNODE_REMOVAL_SECONDS)){
        activeState = ETERNITYNODE_REMOVE;
        return;
    }

    if(!IsPingedWithin(ETERNITYNODE_EXPIRATION_SECONDS)){
        activeState = ETERNITYNODE_EXPIRED;
        return;
    }

    if(!unitTest){
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(999.99*COIN, spySendPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if(!lockMain) return;

            if(!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)){
                activeState = ETERNITYNODE_VIN_SPENT;
                return;

            }
        }
    }

    activeState = ETERNITYNODE_ENABLED; // OK
}

int64_t CEternitynode::SecondsSincePayment() {
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubkey.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60*60*24*30;
    if(sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash =  ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CEternitynode::GetLastPaid() {
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubkey.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash =  ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150; 

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex *BlockReading = chainActive.Tip();

    int nMnCount = enodeman.CountEnabled()*1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nMnCount){
            return 0;
        }
        n++;

        if(eternitynodePayments.mapEternitynodeBlocks.count(BlockReading->nHeight)){
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network 
                to converge on the same payees quickly, then keep the same schedule.
            */
            if(eternitynodePayments.mapEternitynodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)){
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

CEternitynodeBroadcast::CEternitynodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubkey = CPubKey();
    pubkey2 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = ETERNITYNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CEternitynodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CEternitynodeBroadcast::CEternitynodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubkey = newPubkey;
    pubkey2 = newPubkey2;
    sig = std::vector<unsigned char>();
    activeState = ETERNITYNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CEternitynodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CEternitynodeBroadcast::CEternitynodeBroadcast(const CEternitynode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubkey = mn.pubkey;
    pubkey2 = mn.pubkey2;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CEternitynodeBroadcast::CheckAndUpdate(int& nDos)
{
    nDos = 0;

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("enb - Signature rejected, too far into the future %s\n", vin.ToString());
        nDos = 1;
        return false;
    }

    if(protocolVersion < eternitynodePayments.GetMinEternitynodePaymentsProto()) {
        LogPrintf("enb - ignoring outdated Eternitynode %s protocol version %d\n", vin.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubkey.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("enb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

    if(pubkeyScript2.size() != 25) {
        LogPrintf("enb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if(!vin.scriptSig.empty()) {
        LogPrintf("enb - Ignore Not Empty ScriptSig %s\n",vin.ToString());
        return false;
    }

    // incorrect ping or its sigTime
    if(lastPing == CEternitynodePing() || !lastPing.CheckAndUpdate(nDos, false, true))
        return false;

    std::string strMessage;
    std::string errorMessage = "";

    if(protocolVersion < 70201) {
        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

        LogPrint("eternitynode", "enb - sanitized strMessage: %s, pubkey address: %s, sig: %s\n",
            SanitizeString(strMessage), CBitcoinAddress(pubkey.GetID()).ToString(),
            EncodeBase64(&sig[0], sig.size()));

        if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
            if (addr.ToString() != addr.ToString(false))
            {
                // maybe it's wrong format, try again with the old one
                strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                                vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

                LogPrint("eternitynode", "enb - sanitized strMessage: %s, pubkey address: %s, sig: %s\n",
                    SanitizeString(strMessage), CBitcoinAddress(pubkey.GetID()).ToString(),
                    EncodeBase64(&sig[0], sig.size()));

                if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
                    // didn't work either
                    LogPrintf("enb - Got bad Eternitynode address signature, sanitized error: %s\n", SanitizeString(errorMessage));
                    // there is a bug in old MN signatures, ignore such MN but do not ban the peer we got this from
                    return false;
                }
            } else {
                // nope, sig is actually wrong
                LogPrintf("enb - Got bad Eternitynode address signature, sanitized error: %s\n", SanitizeString(errorMessage));
                // there is a bug in old MN signatures, ignore such MN but do not ban the peer we got this from
                return false;
            }
        }
    } else {
        strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubkey.GetID().ToString() + pubkey2.GetID().ToString() +
                        boost::lexical_cast<std::string>(protocolVersion);

        LogPrint("eternitynode", "enb - strMessage: %s, pubkey address: %s, sig: %s\n",
            strMessage, CBitcoinAddress(pubkey.GetID()).ToString(), EncodeBase64(&sig[0], sig.size()));

        if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)){
            LogPrintf("enb - Got bad Eternitynode address signature, error: %s\n", errorMessage);
            nDos = 100;
            return false;
        }
    }

    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != 4855) return false;
    } else if(addr.GetPort() == 4855) return false;

    //search existing Eternitynode list, this is where we update existing Eternitynodes with new enb broadcasts
    CEternitynode* pen = enodeman.Find(vin);

    // no such eternitynode, nothing to update
    if(pen == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    // (mapSeenEternitynodeBroadcast in CEternitynodeMan::ProcessMessage should filter legit duplicates)
    if(pen->sigTime >= sigTime) {
        LogPrintf("CEternitynodeBroadcast::CheckAndUpdate - Bad sigTime %d for Eternitynode %20s %105s (existing broadcast is at %d)\n",
                      sigTime, addr.ToString(), vin.ToString(), pen->sigTime);
        return false;
    }

    // eternitynode is not enabled yet/already, nothing to update
    if(!pen->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if(pen->pubkey == pubkey && !pen->IsBroadcastedWithin(ETERNITYNODE_MIN_ENB_SECONDS)) {
        //take the newest entry
        LogPrintf("enb - Got updated entry for %s\n", addr.ToString());
        if(pen->UpdateFromNewBroadcast((*this))){
            pen->Check();
            if(pen->IsEnabled()) Relay();
        }
        eternitynodeSync.AddedEternitynodeList(GetHash());
    }

    return true;
}

bool CEternitynodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a eternitynode with the same vin (i.e. already activated) and this enb is ours (matches our Eternitynode privkey)
    // so nothing to do here for us
    if(fEternityNode && vin.prevout == activeEternitynode.vin.prevout && pubkey2 == activeEternitynode.pubKeyEternitynode)
        return true;

    // incorrect ping or its sigTime
    if(lastPing == CEternitynodePing() || !lastPing.CheckAndUpdate(nDoS, false, true))
        return false;

    // search existing Eternitynode list
    CEternitynode* pen = enodeman.Find(vin);

    if(pen != NULL) {
        // nothing to do here if we already know about this eternitynode and it's enabled
        if(pen->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else enodeman.Remove(pen->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut(999.99*COIN, spySendPool.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) {
            // not enb fault, let it to be checked again later
            enodeman.mapSeenEternitynodeBroadcast.erase(GetHash());
            eternitynodeSync.mapSeenSyncENB.erase(GetHash());
            return false;
        }

        if(!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("eternitynode", "enb - Accepted Eternitynode entry\n");

    if(GetInputAge(vin) < ETERNITYNODE_MIN_CONFIRMATIONS){
        LogPrintf("enb - Input must have at least %d confirmations\n", ETERNITYNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this enb to be checked again later
        enodeman.mapSeenEternitynodeBroadcast.erase(GetHash());
        eternitynodeSync.mapSeenSyncENB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 ENT tx got ETERNITYNODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second)
    {
        CBlockIndex* pMNIndex = (*mi).second; // block for 1000 ENT tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + ETERNITYNODE_MIN_CONFIRMATIONS - 1]; // block where tx got ETERNITYNODE_MIN_CONFIRMATIONS
        if(pConfIndex->GetBlockTime() > sigTime)
        {
            LogPrintf("enb - Bad sigTime %d for Eternitynode %20s %105s (%i conf block is at %d)\n",
                      sigTime, addr.ToString(), vin.ToString(), ETERNITYNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrintf("enb - Got NEW Eternitynode entry - %s - %s - %s - %lli \n", GetHash().ToString(), addr.ToString(), vin.ToString(), sigTime);
    CEternitynode mn(*this);
    enodeman.Add(mn);

    // if it matches our Eternitynode privkey, then we've been remotely activated
    if(pubkey2 == activeEternitynode.pubKeyEternitynode && protocolVersion == PROTOCOL_VERSION){
        activeEternitynode.EnableHotColdEternityNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if(Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if(!isLocal) Relay();

    return true;
}

void CEternitynodeBroadcast::Relay()
{
    CInv inv(MSG_ETERNITYNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CEternitynodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubkey.begin(), pubkey.end());
    std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrintf("CEternitynodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CEternitynodeBroadcast::VerifySignature()
{
    std::string errorMessage;

    std::string vchPubKey(pubkey.begin(), pubkey.end());
    std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if(!spySendSigner.VerifyMessage(pubkey, sig, strMessage, errorMessage)) {
        LogPrintf("CEternitynodeBroadcast::VerifySignature() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CEternitynodePing::CEternitynodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CEternitynodePing::CEternitynodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CEternitynodePing::Sign(CKey& keyEternitynode, CPubKey& pubKeyEternitynode)
{
    std::string errorMessage;
    std::string strEternityNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyEternitynode)) {
        LogPrintf("CEternitynodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, errorMessage)) {
        LogPrintf("CEternitynodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CEternitynodePing::VerifySignature(CPubKey& pubKeyEternitynode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string errorMessage = "";

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, errorMessage))
    {
        LogPrintf("CEternitynodePing::VerifySignature - Got bad Eternitynode ping signature %s Error: %s\n", vin.ToString(), errorMessage);
        nDos = 33;
        return false;
    }
    return true;
}

bool CEternitynodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CEternitynodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrintf("CEternitynodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if(fCheckSigTimeOnly) {
        CEternitynode* pen = enodeman.Find(vin);
        if(pen) return VerifySignature(pen->pubkey2, nDos);
        return true;
    }

    LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this Eternitynode
    CEternitynode* pen = enodeman.Find(vin);
    if(pen != NULL && pen->protocolVersion >= eternitynodePayments.GetMinEternitynodePaymentsProto())
    {
        if (fRequireEnabled && !pen->IsEnabled()) return false;

        // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this eternitynode or
        // last ping was more then ETERNITYNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if(!pen->IsPingedWithin(ETERNITYNODE_MIN_MNP_SECONDS - 60, sigTime))
        {
            if(!VerifySignature(pen->pubkey2, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second)
            {
                if((*mi).second->nHeight < chainActive.Height() - 24)
                {
                    LogPrintf("CEternitynodePing::CheckAndUpdate - Eternitynode %s block hash %s is too old\n", vin.ToString(), blockHash.ToString());
                    // Do nothing here (no Eternitynode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrintf("CEternitynodePing::CheckAndUpdate - Eternitynode %s block hash %s is unknown\n", vin.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pen->lastPing = *this;

            //enodeman.mapSeenEternitynodeBroadcast.lastPing is probably outdated, so we'll update it
            CEternitynodeBroadcast enb(*pen);
            uint256 hash = enb.GetHash();
            if(enodeman.mapSeenEternitynodeBroadcast.count(hash)) {
                enodeman.mapSeenEternitynodeBroadcast[hash].lastPing = *this;
            }

            pen->Check(true);
            if(!pen->IsEnabled()) return false;

            LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate - Eternitynode ping accepted, vin: %s\n", vin.ToString());

            Relay();
            return true;
        }
        LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate - Eternitynode ping arrived too early, vin: %s\n", vin.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("eternitynode", "CEternitynodePing::CheckAndUpdate - Couldn't find compatible Eternitynode entry, vin: %s\n", vin.ToString());

    return false;
}

void CEternitynodePing::Relay()
{
    CInv inv(MSG_ETERNITYNODE_PING, GetHash());
    RelayInv(inv);
}
