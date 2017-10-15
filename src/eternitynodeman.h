// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ETERNITYNODEMAN_H
#define ETERNITYNODEMAN_H

#include "eternitynode.h"
#include "sync.h"

using namespace std;

class CEternitynodeMan;

extern CEternitynodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CEternitynodeMan
 */
class CEternitynodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CEternitynodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve eternitynode vin by index
    bool Get(int nIndex, CTxIn& vinEternitynode) const;

    /// Get index of a eternitynode vin
    int GetEternitynodeIndex(const CTxIn& vinEternitynode) const;

    void AddEternitynodeVIN(const CTxIn& vinEternitynode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CEternitynodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CEternitynode> vEternitynodes;
    // who's asked for the Eternitynode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForEternitynodeList;
    // who we asked for the Eternitynode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForEternitynodeList;
    // which Eternitynodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForEternitynodeListEntry;
    // who we asked for the eternitynode verification
    std::map<CNetAddr, CEternitynodeVerification> mWeAskedForVerification;

    // these maps are used for eternitynode recovery from ETERNITYNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CEternitynodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CEternitynodeIndex indexEternitynodes;

    CEternitynodeIndex indexEternitynodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when eternitynodes are added, cleared when CGovernanceManager is notified
    bool fEternitynodesAdded;

    /// Set when eternitynodes are removed, cleared when CGovernanceManager is notified
    bool fEternitynodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CEternitynodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CEternitynodeBroadcast> > mapSeenEternitynodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CEternitynodePing> mapSeenEternitynodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CEternitynodeVerification> mapSeenEternitynodeVerification;
    // keep track of dsq count to prevent eternitynodes from gaming spysend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vEternitynodes);
        READWRITE(mAskedUsForEternitynodeList);
        READWRITE(mWeAskedForEternitynodeList);
        READWRITE(mWeAskedForEternitynodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenEternitynodeBroadcast);
        READWRITE(mapSeenEternitynodePing);
        READWRITE(indexEternitynodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CEternitynodeMan();

    /// Add an entry
    bool Add(CEternitynode &mn);

    /// Ask (source) node for mnb
    void AskForEN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all Eternitynodes
    void Check();

    /// Check all Eternitynodes and remove inactive
    void CheckAndRemove();

    /// Clear Eternitynode vector
    void Clear();

    /// Count Eternitynodes filtered by nProtocolVersion.
    /// Eternitynode nProtocolVersion should match or be above the one specified in param here.
    int CountEternitynodes(int nProtocolVersion = -1);
    /// Count enabled Eternitynodes filtered by nProtocolVersion.
    /// Eternitynode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Eternitynodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CEternitynode* Find(const CScript &payee);
    CEternitynode* Find(const CTxIn& vin);
    CEternitynode* Find(const CPubKey& pubKeyEternitynode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyEternitynode, CEternitynode& eternitynode);
    bool Get(const CTxIn& vin, CEternitynode& eternitynode);

    /// Retrieve eternitynode vin by index
    bool Get(int nIndex, CTxIn& vinEternitynode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexEternitynodes.Get(nIndex, vinEternitynode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a eternitynode vin
    int GetEternitynodeIndex(const CTxIn& vinEternitynode) {
        LOCK(cs);
        return indexEternitynodes.GetEternitynodeIndex(vinEternitynode);
    }

    /// Get old index of a eternitynode vin
    int GetEternitynodeIndexOld(const CTxIn& vinEternitynode) {
        LOCK(cs);
        return indexEternitynodesOld.GetEternitynodeIndex(vinEternitynode);
    }

    /// Get eternitynode VIN for an old index value
    bool GetEternitynodeVinForIndexOld(int nEternitynodeIndex, CTxIn& vinEternitynodeOut) {
        LOCK(cs);
        return indexEternitynodesOld.Get(nEternitynodeIndex, vinEternitynodeOut);
    }

    /// Get index of a eternitynode vin, returning rebuild flag
    int GetEternitynodeIndex(const CTxIn& vinEternitynode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexEternitynodes.GetEternitynodeIndex(vinEternitynode);
    }

    void ClearOldEternitynodeIndex() {
        LOCK(cs);
        indexEternitynodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    eternitynode_info_t GetEternitynodeInfo(const CTxIn& vin);

    eternitynode_info_t GetEternitynodeInfo(const CPubKey& pubKeyEternitynode);

    /// Find an entry in the eternitynode list that is next to be paid
    CEternitynode* GetNextEternitynodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CEternitynode* GetNextEternitynodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CEternitynode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CEternitynode> GetFullEternitynodeVector() { return vEternitynodes; }

    std::vector<std::pair<int, CEternitynode> > GetEternitynodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetEternitynodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CEternitynode* GetEternitynodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessEternitynodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CEternitynode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CEternitynodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CEternitynodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CEternitynodeVerification& mnv);

    /// Return the number of (unique) Eternitynodes
    int size() { return vEternitynodes.size(); }

    std::string ToString() const;

    /// Update eternitynode list and maps using provided CEternitynodeBroadcast
    void UpdateEternitynodeList(CEternitynodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateEternitynodeList(CNode* pfrom, CEternitynodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildEternitynodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    bool AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckEternitynode(const CTxIn& vin, bool fForce = false);
    void CheckEternitynode(const CPubKey& pubKeyEternitynode, bool fForce = false);

    int GetEternitynodeState(const CTxIn& vin);
    int GetEternitynodeState(const CPubKey& pubKeyEternitynode);

    bool IsEternitynodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetEternitynodeLastPing(const CTxIn& vin, const CEternitynodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the eternitynode index has been updated.
     * Must be called while not holding the CEternitynodeMan::cs mutex
     */
    void NotifyEternitynodeUpdates();

};

#endif
