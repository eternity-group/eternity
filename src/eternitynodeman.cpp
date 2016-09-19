// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "eternitynodeman.h"
#include "eternitynode.h"
#include "activeeternitynode.h"
#include "spysend.h"
#include "util.h"
#include "addrman.h"
#include "spork.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** Eternitynode manager */
CEternitynodeMan mnodeman;

struct CompareLastPaid
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN
{
    bool operator()(const pair<int64_t, CEternitynode>& t1,
                    const pair<int64_t, CEternitynode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CEternitynodeDB
//

CEternitynodeDB::CEternitynodeDB()
{
    pathEN = GetDataDir() / "encache.dat";
    strMagicMessage = "EternitynodeCache";
}

bool CEternitynodeDB::Write(const CEternitynodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssEternitynodes(SER_DISK, CLIENT_VERSION);
    ssEternitynodes << strMagicMessage; // eternitynode cache file specific magic message
    ssEternitynodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssEternitynodes << mnodemanToSave;
    uint256 hash = Hash(ssEternitynodes.begin(), ssEternitynodes.end());
    ssEternitynodes << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathEN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathEN.string());

    // Write and commit header, data
    try {
        fileout << ssEternitynodes;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
//    FileCommit(fileout);
    fileout.fclose();

    LogPrintf("Written info to encache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", mnodemanToSave.ToString());

    return true;
}

CEternitynodeDB::ReadResult CEternitynodeDB::Read(CEternitynodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathEN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathEN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathEN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (std::exception &e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssEternitynodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssEternitynodes.begin(), ssEternitynodes.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (eternitynode cache file specific magic message) and ..

        ssEternitynodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid eternitynode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssEternitynodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CEternitynodeMan object
        ssEternitynodes >> mnodemanToLoad;
    }
    catch (std::exception &e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from encache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", mnodemanToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("Eternitynode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrintf("Eternitynode manager - result:\n");
        LogPrintf("  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpEternitynodes()
{
    int64_t nStart = GetTimeMillis();

    CEternitynodeDB mndb;
    CEternitynodeMan tempMnodeman;

    LogPrintf("Verifying encache.dat format...\n");
    CEternitynodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CEternitynodeDB::FileError)
        LogPrintf("Missing eternitynode cache file - encache.dat, will try to recreate\n");
    else if (readResult != CEternitynodeDB::Ok)
    {
        LogPrintf("Error reading encache.dat: ");
        if(readResult == CEternitynodeDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to encache.dat...\n");
    mndb.Write(mnodeman);

    LogPrintf("Eternitynode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CEternitynodeMan::CEternitynodeMan() {
    nDsqCount = 0;
}

bool CEternitynodeMan::Add(CEternitynode &mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CEternitynode *pen = Find(mn.vin);
    if (pen == NULL)
    {
        LogPrint("eternitynode", "CEternitynodeMan: Adding new Eternitynode %s - %i now\n", mn.addr.ToString(), size() + 1);
        vEternitynodes.push_back(mn);
        return true;
    }

    return false;
}

void CEternitynodeMan::AskForMN(CNode* pnode, CTxIn &vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForEternitynodeListEntry.find(vin.prevout);
    if (i != mWeAskedForEternitynodeListEntry.end())
    {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrintf("CEternitynodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + ETERNITYNODE_MIN_MNP_SECONDS;
    mWeAskedForEternitynodeListEntry[vin.prevout] = askAgain;
}

void CEternitynodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        mn.Check();
    }
}

void CEternitynodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CEternitynode>::iterator it = vEternitynodes.begin();
    while(it != vEternitynodes.end()){
        if((*it).activeState == CEternitynode::ETERNITYNODE_REMOVE ||
                (*it).activeState == CEternitynode::ETERNITYNODE_VIN_SPENT ||
                (forceExpiredRemoval && (*it).activeState == CEternitynode::ETERNITYNODE_EXPIRED) ||
                (*it).protocolVersion < eternitynodePayments.GetMinEternitynodePaymentsProto()) {
            LogPrint("eternitynode", "CEternitynodeMan: Removing inactive Eternitynode %s - %i now\n", (*it).addr.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them 
            //    sending a brand new mnb
            map<uint256, CEternitynodeBroadcast>::iterator it3 = mapSeenEternitynodeBroadcast.begin();
            while(it3 != mapSeenEternitynodeBroadcast.end()){
                if((*it3).second.vin == (*it).vin){
                    eternitynodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenEternitynodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this eternitynode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForEternitynodeListEntry.begin();
            while(it2 != mWeAskedForEternitynodeListEntry.end()){
                if((*it2).first == (*it).vin.prevout){
                    mWeAskedForEternitynodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vEternitynodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Eternitynode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForEternitynodeList.begin();
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
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForEternitynodeListEntry.begin();
    while(it2 != mWeAskedForEternitynodeListEntry.end()){
        if((*it2).second < GetTime()){
            mWeAskedForEternitynodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenEternitynodeBroadcast
    map<uint256, CEternitynodeBroadcast>::iterator it3 = mapSeenEternitynodeBroadcast.begin();
    while(it3 != mapSeenEternitynodeBroadcast.end()){
        if((*it3).second.lastPing.sigTime < GetTime() - ETERNITYNODE_REMOVAL_SECONDS*2){
            LogPrint("eternitynode", "CEternitynodeMan::CheckAndRemove - Removing expired Eternitynode broadcast %s\n", (*it3).second.GetHash().ToString());
            eternitynodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
            mapSeenEternitynodeBroadcast.erase(it3++);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenEternitynodePing
    map<uint256, CEternitynodePing>::iterator it4 = mapSeenEternitynodePing.begin();
    while(it4 != mapSeenEternitynodePing.end()){
        if((*it4).second.sigTime < GetTime()-(ETERNITYNODE_REMOVAL_SECONDS*2)){
            mapSeenEternitynodePing.erase(it4++);
        } else {
            ++it4;
        }
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
}

int CEternitynodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? eternitynodePayments.GetMinEternitynodePaymentsProto() : protocolVersion;

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        mn.Check();
        if(mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CEternitynodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())){
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForEternitynodeList.find(pnode->addr);
            if (it != mWeAskedForEternitynodeList.end())
            {
                if (GetTime() < (*it).second) {
                    LogPrintf("dseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                    return;
                }
            }
        }
    }
    
    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + ETERNITYNODES_DSEG_SECONDS;
    mWeAskedForEternitynodeList[pnode->addr] = askAgain;
}

CEternitynode *CEternitynodeMan::Find(const CScript &payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        payee2 = GetScriptForDestination(mn.pubkey.GetID());
        if(payee2 == payee)
            return &mn;
    }
    return NULL;
}

CEternitynode *CEternitynodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CEternitynode *CEternitynodeMan::Find(const CPubKey &pubKeyEternitynode)
{
    LOCK(cs);

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        if(mn.pubkey2 == pubKeyEternitynode)
            return &mn;
    }
    return NULL;
}

// 
// Deterministically select the oldest/best eternitynode to pay on the network
//
CEternitynode* CEternitynodeMan::GetNextEternitynodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CEternitynode *pBestEternitynode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecEternitynodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH(CEternitynode &mn, vEternitynodes)
    {
        mn.Check();
        if(!mn.IsEnabled()) continue;

        // //check protocol version
        if(mn.protocolVersion < eternitynodePayments.GetMinEternitynodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(eternitynodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && mn.sigTime + (nMnCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are eternitynodes
        if(mn.GetEternitynodeInputAge() < nMnCount) continue;

        vecEternitynodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecEternitynodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount/3) return GetNextEternitynodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecEternitynodeLastPaid.rbegin(), vecEternitynodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled()/10;
    int nCountTenth = 0; 
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecEternitynodeLastPaid){
        CEternitynode* pen = Find(s.second);
        if(!pen) break;

        uint256 n = pen->CalculateScore(1, nBlockHeight-100);
        if(n > nHigh){
            nHigh = n;
            pBestEternitynode = pen;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestEternitynode;
}

CEternitynode *CEternitynodeMan::FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? eternitynodePayments.GetMinEternitynodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrintf("CEternitynodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if(nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrintf("CEternitynodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH(CEternitynode &mn, vEternitynodes) {
        if(mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH(CTxIn &usedVin, vecToExclude) {
            if(mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if(found) continue;
        if(--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CEternitynode* CEternitynodeMan::GetCurrentEternityNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CEternitynode* winner = NULL;

    // scan for winner
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        mn.Check();
        if(mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Eternitynode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if(n2 > score){
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CEternitynodeMan::GetEternitynodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecEternitynodeScores;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
        if(mn.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            mn.Check();
            if(!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecEternitynodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecEternitynodeScores.rbegin(), vecEternitynodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecEternitynodeScores){
        rank++;
        if(s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CEternitynode> > CEternitynodeMan::GetEternitynodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CEternitynode> > vecEternitynodeScores;
    std::vector<pair<int, CEternitynode> > vecEternitynodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if(!GetBlockHash(hash, nBlockHeight)) return vecEternitynodeRanks;

    // scan for winner
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {

        mn.Check();

        if(mn.protocolVersion < minProtocol) continue;
        if(!mn.IsEnabled()) {
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecEternitynodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecEternitynodeScores.rbegin(), vecEternitynodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CEternitynode)& s, vecEternitynodeScores){
        rank++;
        vecEternitynodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecEternitynodeRanks;
}

CEternitynode* CEternitynodeMan::GetEternitynodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecEternitynodeScores;

    // scan for winner
    BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {

        if(mn.protocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            mn.Check();
            if(!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecEternitynodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecEternitynodeScores.rbegin(), vecEternitynodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn)& s, vecEternitynodeScores){
        rank++;
        if(rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CEternitynodeMan::ProcessEternitynodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fSpySendMaster){
            if(spySendPool.pSubmittedToEternitynode != NULL && pnode->addr == spySendPool.pSubmittedToEternitynode->addr) continue;
            LogPrintf("Closing Eternitynode connection %s \n", pnode->addr.ToString());
            pnode->fSpySendMaster = false;
            pnode->Release();
        }
    }
}

void CEternitynodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    if(fLiteMode) return; //disable all Spysend/Eternitynode related functionality
    if(!eternitynodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Eternitynode Broadcast
        CEternitynodeBroadcast mnb;
        vRecv >> mnb;

        int nDoS = 0;
        if (CheckMnbAndUpdateEternitynodeList(mnb, nDoS)) {
            // use announced Eternitynode as a peer
             addrman.Add(CAddress(mnb.addr), pfrom->addr, 2*60*60);
        } else {
            if(nDoS > 0) Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //Eternitynode Ping
        CEternitynodePing mnp;
        vRecv >> mnp;

        LogPrint("eternitynode", "mnp - Eternitynode ping, vin: %s\n", mnp.vin.ToString());

        if(mapSeenEternitynodePing.count(mnp.GetHash())) return; //seen
        mapSeenEternitynodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if(mnp.CheckAndUpdate(nDoS)) return;

        if(nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Eternitynode list
            CEternitynode* pen = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if(pen != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a eternitynode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Eternitynode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForEternitynodeList.find(pfrom->addr);
                if (i != mAskedUsForEternitynodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("dseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + ETERNITYNODES_DSEG_SECONDS;
                mAskedUsForEternitynodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
            if(mn.addr.IsRFC1918()) continue; //local network

            if(mn.IsEnabled()) {
                LogPrint("eternitynode", "dseg - Sending Eternitynode entry - %s \n", mn.addr.ToString());
                if(vin == CTxIn() || vin == mn.vin){
                    CEternitynodeBroadcast mnb = CEternitynodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_ETERNITYNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if(!mapSeenEternitynodeBroadcast.count(hash)) mapSeenEternitynodeBroadcast.insert(make_pair(hash, mnb));

                    if(vin == mn.vin) {
                        LogPrintf("dseg - Sent 1 Eternitynode entries to %s\n", pfrom->addr.ToString());
                        return;
                    }
                }
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage("ssc", ETERNITYNODE_SYNC_LIST, nInvCount);
            LogPrintf("dseg - Sent %d Eternitynode entries to %s\n", nInvCount, pfrom->addr.ToString());
        }
    }

}

void CEternitynodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CEternitynode>::iterator it = vEternitynodes.begin();
    while(it != vEternitynodes.end()){
        if((*it).vin == vin){
            LogPrint("eternitynode", "CEternitynodeMan: Removing Eternitynode %s - %i now\n", (*it).addr.ToString(), size() - 1);
            vEternitynodes.erase(it);
            break;
        }
        ++it;
    }
}

std::string CEternitynodeMan::ToString() const
{
    std::ostringstream info;

    info << "Eternitynodes: " << (int)vEternitynodes.size() <<
            ", peers who asked us for Eternitynode list: " << (int)mAskedUsForEternitynodeList.size() <<
            ", peers we asked for Eternitynode list: " << (int)mWeAskedForEternitynodeList.size() <<
            ", entries in Eternitynode list we asked for: " << (int)mWeAskedForEternitynodeListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CEternitynodeMan::UpdateEternitynodeList(CEternitynodeBroadcast mnb) {
    mapSeenEternitynodePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenEternitynodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    eternitynodeSync.AddedEternitynodeList(mnb.GetHash());

    LogPrintf("CEternitynodeMan::UpdateEternitynodeList() - addr: %s\n    vin: %s\n", mnb.addr.ToString(), mnb.vin.ToString());

    CEternitynode* pen = Find(mnb.vin);
    if(pen == NULL)
    {
        CEternitynode mn(mnb);
        Add(mn);
    } else {
        pen->UpdateFromNewBroadcast(mnb);
    }
}

bool CEternitynodeMan::CheckMnbAndUpdateEternitynodeList(CEternitynodeBroadcast mnb, int& nDos) {
    nDos = 0;
    LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList - Eternitynode broadcast, vin: %s\n", mnb.vin.ToString());

    if(mapSeenEternitynodeBroadcast.count(mnb.GetHash())) { //seen
        eternitynodeSync.AddedEternitynodeList(mnb.GetHash());
        return true;
    }
    mapSeenEternitynodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

    LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList - Eternitynode broadcast, vin: %s new\n", mnb.vin.ToString());

    if(!mnb.CheckAndUpdate(nDos)){
        LogPrint("eternitynode", "CEternitynodeMan::CheckMnbAndUpdateEternitynodeList - Eternitynode broadcast, vin: %s CheckAndUpdate failed\n", mnb.vin.ToString());
        return false;
    }

    // make sure the vout that was signed is related to the transaction that spawned the Eternitynode
    //  - this is expensive, so it's only done once per Eternitynode
    if(!spySendSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubkey)) {
        LogPrintf("CEternitynodeMan::CheckMnbAndUpdateEternitynodeList - Got mismatched pubkey and vin\n");
        nDos = 33;
        return false;
    }

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckSpySendPool()
    if(mnb.CheckInputsAndAdd(nDos)) {
        eternitynodeSync.AddedEternitynodeList(mnb.GetHash());
    } else {
        LogPrintf("CEternitynodeMan::CheckMnbAndUpdateEternitynodeList - Rejected Eternitynode entry %s\n", mnb.addr.ToString());
        return false;
    }

    return true;
}