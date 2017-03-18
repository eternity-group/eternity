// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ETERNITYNODEMAN_H
#define ETERNITYNODEMAN_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "eternitynode.h"

#define ETERNITYNODES_DUMP_SECONDS               (15*60)
#define ETERNITYNODES_DSEG_SECONDS               (3*60*60)

using namespace std;

class CEternitynodeMan;

extern CEternitynodeMan enodeman;
void DumpEternitynodes();

/** Access to the MN database (encache.dat)
 */
class CEternitynodeDB
{
private:
    boost::filesystem::path pathEN;
    std::string strMagicMessage;
public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CEternitynodeDB();
    bool Write(const CEternitynodeMan &enodemanToSave);
    ReadResult Read(CEternitynodeMan& enodemanToLoad, bool fDryRun = false);
};

class CEternitynodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CEternitynode> vEternitynodes;
    // who's asked for the Eternitynode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForEternitynodeList;
    // who we asked for the Eternitynode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForEternitynodeList;
    // which Eternitynodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForEternitynodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CEternitynodeBroadcast> mapSeenEternitynodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CEternitynodePing> mapSeenEternitynodePing;
    
    // keep track of dsq count to prevent eternitynodes from gaming spysend queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vEternitynodes);
        READWRITE(mAskedUsForEternitynodeList);
        READWRITE(mWeAskedForEternitynodeList);
        READWRITE(mWeAskedForEternitynodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenEternitynodeBroadcast);
        READWRITE(mapSeenEternitynodePing);
    }

    CEternitynodeMan();
    CEternitynodeMan(CEternitynodeMan& other);

    /// Add an entry
    bool Add(CEternitynode &mn);

    /// Ask (source) node for enb
    void AskForMN(CNode *pnode, CTxIn &vin);

    /// Check all Eternitynodes
    void Check();

    /// Check all Eternitynodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Eternitynode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CEternitynode* Find(const CScript &payee);
    CEternitynode* Find(const CTxIn& vin);
    CEternitynode* Find(const CPubKey& pubKeyEternitynode);

    /// Find an entry in the eternitynode list that is next to be paid
    CEternitynode* GetNextEternitynodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CEternitynode* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CEternitynode* GetCurrentEternityNode(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

    std::vector<CEternitynode> GetFullEternitynodeVector() { Check(); return vEternitynodes; }

    std::vector<pair<int, CEternitynode> > GetEternitynodeRanks(int64_t nBlockHeight, int minProtocol=0);
    int GetEternitynodeRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
    CEternitynode* GetEternitynodeByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

    void ProcessEternitynodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Eternitynodes
    int size() { return vEternitynodes.size(); }

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update eternitynode list and maps using provided CEternitynodeBroadcast
    void UpdateEternitynodeList(CEternitynodeBroadcast enb);
    /// Perform complete check and only then update list and maps
    bool CheckEnbAndUpdateEternitynodeList(CEternitynodeBroadcast enb, int& nDos);

};

#endif
