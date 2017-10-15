// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeeternitynode.h"
#include "addrman.h"
#include "spysend.h"
#include "governance.h"
#include "eternitynode-payments.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "netfulfilledman.h"
#include "util.h"

/** Eternitynode manager */
CEternitynodeMan mnodeman;

const std::string CEternitynodeMan::SERIALIZATION_VERSION_STRING = "CEternitynodeMan-Version-4";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CEternitynode*>& t1,
                    const std::pair<int, CEternitynode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CEternitynode*>& t1,
                    const std::pair<int64_t, CEternitynode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CEternitynodeIndex::CEternitynodeIndex()
    : nSize(0),
      mapIndex(),
      mapReverseIndex()
{}

bool CEternitynodeIndex::Get(int nIndex, CTxIn& vinEternitynode) const
{
    rindex_m_cit it = mapReverseIndex.find(nIndex);
    if(it == mapReverseIndex.end()) {
        return false;
    }
    vinEternitynode = it->second;
    return true;
}

int CEternitynodeIndex::GetEternitynodeIndex(const CTxIn& vinEternitynode) const
{
    index_m_cit it = mapIndex.find(vinEternitynode);
    if(it == mapIndex.end()) {
        return -1;
    }
    return it->second;
}

void CEternitynodeIndex::AddEternitynodeVIN(const CTxIn& vinEternitynode)
{
    index_m_it it = mapIndex.find(vinEternitynode);
    if(it != mapIndex.end()) {
        return;
    }
    int nNextIndex = nSize;
    mapIndex[vinEternitynode] = nNextIndex;
    mapReverseIndex[nNextIndex] = vinEternitynode;
    ++nSize;
}

void CEternitynodeIndex::Clear()
{
    mapIndex.clear();
    mapReverseIndex.clear();
    nSize = 0;
}
struct CompareByAddr

{
    bool operator()(const CEternitynode* t1,
                    const CEternitynode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

void CEternitynodeIndex::RebuildIndex()
{
    nSize = mapIndex.size();
    for(index_m_it it = mapIndex.begin(); it != mapIndex.end(); ++it) {
        mapReverseIndex[it->second] = it->first;
    }
}

CEternitynodeMan::CEternitynodeMan()
: cs(),
  vEternitynodes(),
  mAskedUsForEternitynodeList(),
  mWeAskedForEternitynodeList(),
  mWeAskedForEternitynodeListEntry(),
  mWeAskedForVerification(),
  mMnbRecoveryRequests(),
  mMnbRecoveryGoodReplies(),
  listScheduledMnbRequestConnections(),
  nLastIndexRebuildTime(0),
  indexEternitynodes(),
  indexEternitynodesOld(),
  fIndexRebuilt(false),
  fEternitynodesAdded(false),
  fEternitynodesRemoved(false),
  vecDirtyGovernanceObjectHashes(),
  nLastWatchdogVoteTime(0),
  mapSeenEternitynodeBroadcast(),
  mapSeenEternitynodePing(),
  nDsqCount(0)
{}

bool CEternitynodeMan::Add(CEternitynode &mn)
{
    LOCK(cs);

    CEternitynode *pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("eternitynode", "CEternitynodeMan::Add -- Adding new Eternitynode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
        vEternitynodes.push_back(mn);
        indexEternitynodes.AddEternitynodeVIN(mn.vin);
        fEternitynodesAdded = true;
        return true;
    }

    return false;
}

void CEternitynodeMan::AskForEN(CNode* pnode, const CTxIn &vin)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForEternitynodeListEntry.find(vin.prevout);
    if (it1 != mWeAskedForEternitynodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CEternitynodeMan::AskForEN -- Asking same peer %s for missing eternitynode entry again: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CEternitynodeMan::AskForEN -- Asking new peer %s for missing eternitynode entry: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CEternitynodeMan::AskForEN -- Asking peer %s for missing eternitynode entry for the first time: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
    }
    mWeAskedForEternitynodeListEntry[vin.prevout][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, vin);
}

void CEternitynodeMan::Check()
{
    LOCK(cs);

    LogPrint("eternitynode", "CEternitynodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        mn.Check();
    }
}

void CEternitynodeMan::CheckAndRemove()
{
    if(!eternitynodeSync.IsEternitynodeListSynced()) return;

    LogPrintf("CEternitynodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateEternitynodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent eternitynodes, prepare structures and make requests to reasure the state of inactive ones
        std::vector<CEternitynode>::iterator it = vEternitynodes.begin();
        std::vector<std::pair<int, CEternitynode> > vecEternitynodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES eternitynode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        while(it != vEternitynodes.end()) {
            CEternitynodeBroadcast mnb = CEternitynodeBroadcast(*it);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if ((*it).IsOutpointSpent()) {
                LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- Removing Eternitynode: %s  addr=%s  %i now\n", (*it).GetStateString(), (*it).addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenEternitynodeBroadcast.erase(hash);
                mWeAskedForEternitynodeListEntry.erase((*it).vin.prevout);

                // and finally remove it from the list
                it->FlagGovernanceItemsAsDirty();
                it = vEternitynodes.erase(it);
                fEternitynodesRemoved = true;
            } else {
                bool fAsk = pCurrentBlockIndex &&
                            (nAskForMnbRecovery > 0) &&
                            eternitynodeSync.IsSynced() &&
                            it->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecEternitynodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(pCurrentBlockIndex->nHeight);
                        vecEternitynodeRanks = GetEternitynodeRanks(nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL eternitynodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecEternitynodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForEternitynodeListEntry.count(it->vin.prevout) && mWeAskedForEternitynodeListEntry[it->vin.prevout].count(vecEternitynodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecEternitynodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- Recovery initiated, eternitynode=%s\n", it->vin.prevout.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for ETERNITYNODE_NEW_START_REQUIRED eternitynodes
        LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CEternitynodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- reprocessing mnb, eternitynode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenEternitynodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateEternitynodeList(NULL, itMnbReplies->second[0], nDos);
                }
                LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- removing mnb recovery reply, eternitynode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in ETERNITYNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Eternitynode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForEternitynodeList.begin();
        while(it1 != mAskedUsForEternitynodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForEternitynodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Eternitynode list
        it1 = mWeAskedForEternitynodeList.begin();
        while(it1 != mWeAskedForEternitynodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForEternitynodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Eternitynodes we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForEternitynodeListEntry.begin();
        while(it2 != mWeAskedForEternitynodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForEternitynodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CEternitynodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenEternitynodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenEternitynodePing
        std::map<uint256, CEternitynodePing>::iterator it4 = mapSeenEternitynodePing.begin();
        while(it4 != mapSeenEternitynodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- Removing expired Eternitynode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenEternitynodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenEternitynodeVerification
        std::map<uint256, CEternitynodeVerification>::iterator itv2 = mapSeenEternitynodeVerification.begin();
        while(itv2 != mapSeenEternitynodeVerification.end()){
            if((*itv2).second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS){
                LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove -- Removing expired Eternitynode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenEternitynodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CEternitynodeMan::CheckAndRemove -- %s\n", ToString());

        if(fEternitynodesRemoved) {
            CheckAndRebuildEternitynodeIndex();
        }
    }

    if(fEternitynodesRemoved) {
        NotifyEternitynodeUpdates();
    }
}

void CEternitynodeMan::Clear()
{
    LOCK(cs);
    vEternitynodes.clear();
    mAskedUsForEternitynodeList.clear();
    mWeAskedForEternitynodeList.clear();
    mWeAskedForEternitynodeListEntry.clear();
    mapSeenEternitynodeBroadcast.clear();
    mapSeenEternitynodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexEternitynodes.Clear();
    indexEternitynodesOld.Clear();
}

int CEternitynodeMan::CountEternitynodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? enpayments.GetMinEternitynodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        if(mn.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CEternitynodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? enpayments.GetMinEternitynodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        if(mn.nProtocolVersion < nProtocolVersion || !mn.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 eternitynodes are allowed in 12.1, saving this for later
int CEternitynodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
        if ((nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CEternitynodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForEternitynodeList.find(pnode->addr);
            if(it != mWeAskedForEternitynodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CEternitynodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }
    
    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForEternitynodeList[pnode->addr] = askAgain;

    LogPrint("eternitynode", "CEternitynodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CEternitynode* CEternitynodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CEternitynode* CEternitynodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CEternitynode* CEternitynodeMan::Find(const CPubKey &pubKeyEternitynode)
{
    LOCK(cs);

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        if(mn.pubKeyEternitynode == pubKeyEternitynode)
            return &mn;
    }
    return NULL;
}

bool CEternitynodeMan::Get(const CPubKey& pubKeyEternitynode, CEternitynode& eternitynode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CEternitynode* pMN = Find(pubKeyEternitynode);
    if(!pMN)  {
        return false;
    }
    eternitynode = *pMN;
    return true;
}

bool CEternitynodeMan::Get(const CTxIn& vin, CEternitynode& eternitynode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    eternitynode = *pMN;
    return true;
}

eternitynode_info_t CEternitynodeMan::GetEternitynodeInfo(const CTxIn& vin)
{
    eternitynode_info_t info;
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

eternitynode_info_t CEternitynodeMan::GetEternitynodeInfo(const CPubKey& pubKeyEternitynode)
{
    eternitynode_info_t info;
    LOCK(cs);
    CEternitynode* pMN = Find(pubKeyEternitynode);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bool CEternitynodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    return (pMN != NULL);
}

//
// Deterministically select the oldest/best eternitynode to pay on the network
//
CEternitynode* CEternitynodeMan::GetNextEternitynodeInQueueForPayment(bool fFilterSigTime, int& nCount)
{
    if(!pCurrentBlockIndex) {
        nCount = 0;
        return NULL;
    }
    return GetNextEternitynodeInQueueForPayment(pCurrentBlockIndex->nHeight, fFilterSigTime, nCount);
}

CEternitynode* CEternitynodeMan::GetNextEternitynodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    CEternitynode *pBestEternitynode = NULL;
    std::vector<std::pair<int, CEternitynode*> > vecEternitynodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH(CEternitynode &mn, vEternitynodes)
    {
        if(!mn.IsValidForPayment()) continue;

        // //check protocol version
        if(mn.nProtocolVersion < enpayments.GetMinEternitynodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(enpayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && mn.sigTime + (nMnCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has at least as many confirmations as there are eternitynodes
        if(mn.GetCollateralAge() < nMnCount) continue;

        vecEternitynodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }

    nCount = (int)vecEternitynodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount/3) return GetNextEternitynodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them low to high
    sort(vecEternitynodeLastPaid.begin(), vecEternitynodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CEternitynode::GetNextEternitynodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int, CEternitynode*)& s, vecEternitynodeLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestEternitynode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestEternitynode;
}

CEternitynode* CEternitynodeMan::FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? enpayments.GetMinEternitynodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CEternitynodeMan::FindRandomNotInVec -- %d enabled eternitynodes, %d eternitynodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CEternitynode*> vpEternitynodesShuffled;
    BOOST_FOREACH(CEternitynode &mn, vEternitynodes) {
        vpEternitynodesShuffled.push_back(&mn);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpEternitynodesShuffled.begin(), vpEternitynodesShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CEternitynode* pmn, vpEternitynodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("eternitynode", "CEternitynodeMan::FindRandomNotInVec -- found, eternitynode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("eternitynode", "CEternitynodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CEternitynodeMan::GetEternitynodeRank(const CTxIn& vin, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CEternitynode*> > vecEternitynodeScores;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive) {
            if(!mn.IsEnabled()) continue;
        }
        else {
            if(!mn.IsValidForPayment()) continue;
        }
        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecEternitynodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecEternitynodeScores.rbegin(), vecEternitynodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CEternitynode*)& scorePair, vecEternitynodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<std::pair<int, CEternitynode> > CEternitynodeMan::GetEternitynodeRanks(int nBlockHeight, int nMinProtocol)
{
    std::vector<std::pair<int64_t, CEternitynode*> > vecEternitynodeScores;
    std::vector<std::pair<int, CEternitynode> > vecEternitynodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return vecEternitynodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {

        if(mn.nProtocolVersion < nMinProtocol || !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecEternitynodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecEternitynodeScores.rbegin(), vecEternitynodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CEternitynode*)& s, vecEternitynodeScores) {
        nRank++;
        vecEternitynodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecEternitynodeRanks;
}

CEternitynode* CEternitynodeMan::GetEternitynodeByRank(int nRank, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CEternitynode*> > vecEternitynodeScores;

    LOCK(cs);

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CEternitynode::GetEternitynodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // Fill scores
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {

        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive && !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecEternitynodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecEternitynodeScores.rbegin(), vecEternitynodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CEternitynode*)& s, vecEternitynodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CEternitynodeMan::ProcessEternitynodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fEternitynode) {
            if(spySendPool.pSubmittedToEternitynode != NULL && pnode->addr == spySendPool.pSubmittedToEternitynode->addr) continue;
            LogPrintf("Closing Eternitynode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

std::pair<CService, std::set<uint256> > CEternitynodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CEternitynodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(fLiteMode) return; // disable all Eternity specific functionality
    if(!eternitynodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MNANNOUNCE) { //Eternitynode Broadcast

        CEternitynodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        LogPrint("eternitynode", "MNANNOUNCE -- Eternitynode announce, eternitynode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateEternitynodeList(pfrom, mnb, nDos)) {
            // use announced Eternitynode as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fEternitynodesAdded) {
            NotifyEternitynodeUpdates();
        }
    } else if (strCommand == NetMsgType::MNPING) { //Eternitynode Ping

        CEternitynodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        LogPrint("eternitynode", "MNPING -- Eternitynode ping, eternitynode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenEternitynodePing.count(nHash)) return; //seen
        mapSeenEternitynodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("eternitynode", "MNPING -- Eternitynode ping, eternitynode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this Eternitynode
        CEternitynode* pmn = mnodeman.Find(mnp.vin);

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a eternitynode entry once
        AskForEN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Eternitynode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after eternitynode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!eternitynodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("eternitynode", "DSEG -- Eternitynode list, eternitynode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForEternitynodeList.find(pfrom->addr);
                if (i != mAskedUsForEternitynodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForEternitynodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network eternitynode
            if (mn.IsUpdateRequired()) continue; // do not send outdated eternitynodes

            LogPrint("eternitynode", "DSEG -- Sending Eternitynode entry: eternitynode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CEternitynodeBroadcast mnb = CEternitynodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_ETERNITYNODE_ANNOUNCE, hash));
            pfrom->PushInventory(CInv(MSG_ETERNITYNODE_PING, mn.lastPing.GetHash()));
            nInvCount++;

            if (!mapSeenEternitynodeBroadcast.count(hash)) {
                mapSeenEternitynodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));
            }

            if (vin == mn.vin) {
                LogPrintf("DSEG -- Sent 1 Eternitynode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, ETERNITYNODE_SYNC_LIST, nInvCount);
            LogPrintf("DSEG -- Sent %d Eternitynode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("eternitynode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Eternitynode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CEternitynodeVerification mnv;
        vRecv >> mnv;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some eternitynode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some eternitynode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of eternitynodes via unique direct requests.

void CEternitynodeMan::DoFullVerificationStep()
{
    if(activeEternitynode.vin == CTxIn()) return;
    if(!eternitynodeSync.IsSynced()) return;

    std::vector<std::pair<int, CEternitynode> > vecEternitynodeRanks = GetEternitynodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecEternitynodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CEternitynode> >::iterator it = vecEternitynodeRanks.begin();
    while(it != vecEternitynodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("eternitynode", "CEternitynodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeEternitynode.vin) {
            nMyRank = it->first;
            LogPrint("eternitynode", "CEternitynodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d eternitynodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this eternitynode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS eternitynodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecEternitynodeRanks.size()) return;

    std::vector<CEternitynode*> vSortedByAddr;
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        vSortedByAddr.push_back(&mn);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecEternitynodeRanks.begin() + nOffset;
    while(it != vecEternitynodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("eternitynode", "CEternitynodeMan::DoFullVerificationStep -- Already %s%s%s eternitynode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecEternitynodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("eternitynode", "CEternitynodeMan::DoFullVerificationStep -- Verifying eternitynode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest((CAddress)it->second.addr, vSortedByAddr)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecEternitynodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("eternitynode", "CEternitynodeMan::DoFullVerificationStep -- Sent verification requests to %d eternitynodes\n", nCount);
}

// This function tries to find eternitynodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CEternitynodeMan::CheckSameAddr()
{
    if(!eternitynodeSync.IsSynced() || vEternitynodes.empty()) return;

    std::vector<CEternitynode*> vBan;
    std::vector<CEternitynode*> vSortedByAddr;

    {
        LOCK(cs);

        CEternitynode* pprevEternitynode = NULL;
        CEternitynode* pverifiedEternitynode = NULL;

        BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
            vSortedByAddr.push_back(&mn);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CEternitynode* pmn, vSortedByAddr) {
            // check only (pre)enabled eternitynodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevEternitynode) {
                pprevEternitynode = pmn;
                pverifiedEternitynode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevEternitynode->addr) {
                if(pverifiedEternitynode) {
                    // another eternitynode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this eternitynode with the same ip is verified, ban previous one
                    vBan.push_back(pprevEternitynode);
                    // and keep a reference to be able to ban following eternitynodes with the same ip
                    pverifiedEternitynode = pmn;
                }
            } else {
                pverifiedEternitynode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevEternitynode = pmn;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CEternitynode* pmn, vBan) {
        LogPrintf("CEternitynodeMan::CheckSameAddr -- increasing PoSe ban score for eternitynode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CEternitynodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<CEternitynode*>& vSortedByAddr)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("eternitynode", "CEternitynodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = ConnectNode(addr, NULL, true);
    if(pnode == NULL) {
        LogPrintf("CEternitynodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CEternitynodeVerification mnv(addr, GetRandInt(999999), pCurrentBlockIndex->nHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CEternitynodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CEternitynodeMan::SendVerifyReply(CNode* pnode, CEternitynodeVerification& mnv)
{
    // only eternitynodes can sign this, why would someone ask regular node?
    if(!fEternityNode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
        // peer should not ask us that often
        LogPrintf("EternitynodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("EternitynodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeEternitynode.service.ToString(false), mnv.nonce, blockHash.ToString());

    if(!spySendSigner.SignMessage(strMessage, mnv.vchSig1, activeEternitynode.keyEternitynode)) {
        LogPrintf("EternitynodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!spySendSigner.VerifyMessage(activeEternitynode.pubKeyEternitynode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("EternitynodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CEternitynodeMan::ProcessVerifyReply(CNode* pnode, CEternitynodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CEternitynodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CEternitynodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CEternitynodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("EternitynodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CEternitynodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CEternitynode* prealEternitynode = NULL;
        std::vector<CEternitynode*> vpEternitynodesToBan;
        std::vector<CEternitynode>::iterator it = vEternitynodes.begin();
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(false), mnv.nonce, blockHash.ToString());
        while(it != vEternitynodes.end()) {
            if((CAddress)it->addr == pnode->addr) {
                if(spySendSigner.VerifyMessage(it->pubKeyEternitynode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealEternitynode = &(*it);
                    if(!it->IsPoSeVerified()) {
                        it->DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated eternitynode
                    if(activeEternitynode.vin == CTxIn()) continue;
                    // update ...
                    mnv.addr = it->addr;
                    mnv.vin1 = it->vin;
                    mnv.vin2 = activeEternitynode.vin;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!spySendSigner.SignMessage(strMessage2, mnv.vchSig2, activeEternitynode.keyEternitynode)) {
                        LogPrintf("EternitynodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!spySendSigner.VerifyMessage(activeEternitynode.pubKeyEternitynode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("EternitynodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mnv.Relay();

                } else {
                    vpEternitynodesToBan.push_back(&(*it));
                }
            }
            ++it;
        }
        // no real eternitynode found?...
        if(!prealEternitynode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CEternitynodeMan::ProcessVerifyReply -- ERROR: no real eternitynode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CEternitynodeMan::ProcessVerifyReply -- verified real eternitynode %s for addr %s\n",
                    prealEternitynode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CEternitynode* pmn, vpEternitynodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("eternitynode", "CEternitynodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealEternitynode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        LogPrintf("CEternitynodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake eternitynodes, addr %s\n",
                    (int)vpEternitynodesToBan.size(), pnode->addr.ToString());
    }
}

void CEternitynodeMan::ProcessVerifyBroadcast(CNode* pnode, const CEternitynodeVerification& mnv)
{
    std::string strError;

    if(mapSeenEternitynodeVerification.find(mnv.GetHash()) != mapSeenEternitynodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenEternitynodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
        LogPrint("eternitynode", "EternitynodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.vin1.prevout == mnv.vin2.prevout) {
        LogPrint("eternitynode", "EternitynodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("EternitynodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank = GetEternitynodeRank(mnv.vin2, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION);

    if (nRank == -1) {
        LogPrint("eternitynode", "CEternitynodeMan::ProcessVerifyBroadcast -- Can't calculate rank for eternitynode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("eternitynode", "CEternitynodeMan::ProcessVerifyBroadcast -- Mastrernode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CEternitynode* pmn1 = Find(mnv.vin1);
        if(!pmn1) {
            LogPrintf("CEternitynodeMan::ProcessVerifyBroadcast -- can't find eternitynode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CEternitynode* pmn2 = Find(mnv.vin2);
        if(!pmn2) {
            LogPrintf("CEternitynodeMan::ProcessVerifyBroadcast -- can't find eternitynode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CEternitynodeMan::ProcessVerifyBroadcast -- addr %s do not match %s\n", mnv.addr.ToString(), pnode->addr.ToString());
            return;
        }

        if(spySendSigner.VerifyMessage(pmn1->pubKeyEternitynode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("EternitynodeMan::ProcessVerifyBroadcast -- VerifyMessage() for eternitynode1 failed, error: %s\n", strError);
            return;
        }

        if(spySendSigner.VerifyMessage(pmn2->pubKeyEternitynode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("EternitynodeMan::ProcessVerifyBroadcast -- VerifyMessage() for eternitynode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CEternitynodeMan::ProcessVerifyBroadcast -- verified eternitynode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pnode->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
            if(mn.addr != mnv.addr || mn.vin.prevout == mnv.vin1.prevout) continue;
            mn.IncreasePoSeBanScore();
            nCount++;
            LogPrint("eternitynode", "CEternitynodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mn.vin.prevout.ToStringShort(), mn.addr.ToString(), mn.nPoSeBanScore);
        }
        LogPrintf("CEternitynodeMan::ProcessVerifyBroadcast -- PoSe score incresed for %d fake eternitynodes, addr %s\n",
                    nCount, pnode->addr.ToString());
    }
}

std::string CEternitynodeMan::ToString() const
{
    std::ostringstream info;

    info << "Eternitynodes: " << (int)vEternitynodes.size() <<
            ", peers who asked us for Eternitynode list: " << (int)mAskedUsForEternitynodeList.size() <<
            ", peers we asked for Eternitynode list: " << (int)mWeAskedForEternitynodeList.size() <<
            ", entries in Eternitynode list we asked for: " << (int)mWeAskedForEternitynodeListEntry.size() <<
            ", eternitynode index size: " << indexEternitynodes.GetSize() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CEternitynodeMan::UpdateEternitynodeList(CEternitynodeBroadcast mnb)
{
    LOCK(cs);
    mapSeenEternitynodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenEternitynodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

    LogPrintf("CEternitynodeMan::UpdateEternitynodeList -- eternitynode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

    CEternitynode* pmn = Find(mnb.vin);
    if(pmn == NULL) {
        CEternitynode mn(mnb);
        if(Add(mn)) {
            eternitynodeSync.AddedEternitynodeList();
        }
    } else {
        CEternitynodeBroadcast mnbOld = mapSeenEternitynodeBroadcast[CEternitynodeBroadcast(*pmn).GetHash()].second;
        if(pmn->UpdateFromNewBroadcast(mnb)) {
            eternitynodeSync.AddedEternitynodeList();
            mapSeenEternitynodeBroadcast.erase(mnbOld.GetHash());
        }
    }
}

bool CEternitynodeMan::CheckMnbAndUpdateEternitynodeList(CNode* pfrom, CEternitynodeBroadcast mnb, int& nDos)
{
    // Need LOCK2 here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK2(cs_main, cs);

    nDos = 0;
    LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- eternitynode=%s\n", mnb.vin.prevout.ToStringShort());

    uint256 hash = mnb.GetHash();
    if(mapSeenEternitynodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
        LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- eternitynode=%s seen\n", mnb.vin.prevout.ToStringShort());
        // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
        if(GetTime() - mapSeenEternitynodeBroadcast[hash].first > ETERNITYNODE_NEW_START_REQUIRED_SECONDS - ETERNITYNODE_MIN_MNP_SECONDS * 2) {
            LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- eternitynode=%s seen update\n", mnb.vin.prevout.ToStringShort());
            mapSeenEternitynodeBroadcast[hash].first = GetTime();
            eternitynodeSync.AddedEternitynodeList();
        }
        // did we ask this node for it?
        if(pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
            LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- mnb=%s seen request\n", hash.ToString());
            if(mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                // do not allow node to send same mnb multiple times in recovery mode
                mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                // does it have newer lastPing?
                if(mnb.lastPing.sigTime > mapSeenEternitynodeBroadcast[hash].second.lastPing.sigTime) {
                    // simulate Check
                    CEternitynode mnTemp = CEternitynode(mnb);
                    mnTemp.Check();
                    LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetTime() - mnb.lastPing.sigTime)/60, mnTemp.GetStateString());
                    if(mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                        // this node thinks it's a good one
                        LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- eternitynode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                        mMnbRecoveryGoodReplies[hash].push_back(mnb);
                    }
                }
            }
        }
        return true;
    }
    mapSeenEternitynodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

    LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- eternitynode=%s new\n", mnb.vin.prevout.ToStringShort());

    if(!mnb.SimpleCheck(nDos)) {
        LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- SimpleCheck() failed, eternitynode=%s\n", mnb.vin.prevout.ToStringShort());
        return false;
    }

    // search Eternitynode list
    CEternitynode* pmn = Find(mnb.vin);
    if(pmn) {
        CEternitynodeBroadcast mnbOld = mapSeenEternitynodeBroadcast[CEternitynodeBroadcast(*pmn).GetHash()].second;
        if(!mnb.Update(pmn, nDos)) {
            LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- Update() failed, eternitynode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }
        if(hash != mnbOld.GetHash()) {
            mapSeenEternitynodeBroadcast.erase(mnbOld.GetHash());
        }
    } else {
        if(mnb.CheckOutpoint(nDos)) {
            Add(mnb);
            eternitynodeSync.AddedEternitynodeList();
            // if it matches our Eternitynode privkey...
            if(fEternityNode && mnb.pubKeyEternitynode == activeEternitynode.pubKeyEternitynode) {
                mnb.nPoSeBanScore = -ETERNITYNODE_POSE_BAN_MAX_SCORE;
                if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                    // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                    LogPrintf("CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- Got NEW Eternitynode entry: eternitynode=%s  sigTime=%lld  addr=%s\n",
                                mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                    activeEternitynode.ManageState();
                } else {
                    // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                    // but also do not ban the node we get this message from
                    LogPrintf("CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                    return false;
                }
            }
            mnb.Relay();
        } else {
            LogPrintf("CEternitynodeMan::CheckMnbAndUpdateEternitynodeList -- Rejected Eternitynode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
            return false;
        }
    }

    return true;
}

void CEternitynodeMan::UpdateLastPaid()
{
    LOCK(cs);

    if(fLiteMode) return;
    if(!pCurrentBlockIndex) return;

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a eternitynode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fEternityNode) ? enpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    // LogPrint("enpayments", "CEternitynodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
    //                         pCurrentBlockIndex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        mn.UpdateLastPaid(pCurrentBlockIndex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !eternitynodeSync.IsWinnersListSynced();
}

void CEternitynodeMan::CheckAndRebuildEternitynodeIndex()
{
    LOCK(cs);

    if(GetTime() - nLastIndexRebuildTime < MIN_INDEX_REBUILD_TIME) {
        return;
    }

    if(indexEternitynodes.GetSize() <= MAX_EXPECTED_INDEX_SIZE) {
        return;
    }

    if(indexEternitynodes.GetSize() <= int(vEternitynodes.size())) {
        return;
    }

    indexEternitynodesOld = indexEternitynodes;
    indexEternitynodes.Clear();
    for(size_t i = 0; i < vEternitynodes.size(); ++i) {
        indexEternitynodes.AddEternitynodeVIN(vEternitynodes[i].vin);
    }

    fIndexRebuilt = true;
    nLastIndexRebuildTime = GetTime();
}

void CEternitynodeMan::UpdateWatchdogVoteTime(const CTxIn& vin)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->UpdateWatchdogVoteTime();
    nLastWatchdogVoteTime = GetTime();
}

bool CEternitynodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any eternitynodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= ETERNITYNODE_WATCHDOG_MAX_SECONDS;
}

bool CEternitynodeMan::AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    pMN->AddGovernanceVote(nGovernanceObjectHash);
    return true;
}

void CEternitynodeMan::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
{
    LOCK(cs);
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        mn.RemoveGovernanceObject(nGovernanceObjectHash);
    }
}

void CEternitynodeMan::CheckEternitynode(const CTxIn& vin, bool fForce)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

void CEternitynodeMan::CheckEternitynode(const CPubKey& pubKeyEternitynode, bool fForce)
{
    LOCK(cs);
    CEternitynode* pMN = Find(pubKeyEternitynode);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

int CEternitynodeMan::GetEternitynodeState(const CTxIn& vin)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return CEternitynode::ETERNITYNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

int CEternitynodeMan::GetEternitynodeState(const CPubKey& pubKeyEternitynode)
{
    LOCK(cs);
    CEternitynode* pMN = Find(pubKeyEternitynode);
    if(!pMN)  {
        return CEternitynode::ETERNITYNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

bool CEternitynodeMan::IsEternitynodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN) {
        return false;
    }
    return pMN->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CEternitynodeMan::SetEternitynodeLastPing(const CTxIn& vin, const CEternitynodePing& mnp)
{
    LOCK(cs);
    CEternitynode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->lastPing = mnp;
    mapSeenEternitynodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CEternitynodeBroadcast mnb(*pMN);
    uint256 hash = mnb.GetHash();
    if(mapSeenEternitynodeBroadcast.count(hash)) {
        mapSeenEternitynodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CEternitynodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("eternitynode", "CEternitynodeMan::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    CheckSameAddr();

    if(fEternityNode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid();
    }
}

void CEternitynodeMan::NotifyEternitynodeUpdates()
{
    // Avoid double locking
    bool fEternitynodesAddedLocal = false;
    bool fEternitynodesRemovedLocal = false;
    {
        LOCK(cs);
        fEternitynodesAddedLocal = fEternitynodesAdded;
        fEternitynodesRemovedLocal = fEternitynodesRemoved;
    }

    if(fEternitynodesAddedLocal) {
        governance.CheckEternitynodeOrphanObjects();
        governance.CheckEternitynodeOrphanVotes();
    }
    if(fEternitynodesRemovedLocal) {
        governance.UpdateCachesAndClean();
    }

    LOCK(cs);
    fEternitynodesAdded = false;
    fEternitynodesRemoved = false;
}
