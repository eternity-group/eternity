// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "eternitynode-payments.h"
#include "eternitynode-evolution.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "spysend.h"
#include "util.h"
#include "sync.h"
#include "spork.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** Object for who's going to get paid on which blocks */
CEternitynodePayments eternitynodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapEternitynodeBlocks;
CCriticalSection cs_mapEternitynodePayeeVotes;

//
// CEternitynodePaymentDB
//

CEternitynodePaymentDB::CEternitynodePaymentDB()
{
    pathDB = GetDataDir() / "enpayments.dat";
    strMagicMessage = "EternitynodePayments";
}

bool CEternitynodePaymentDB::Write(const CEternitynodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage; // eternitynode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrintf("Written info to enpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CEternitynodePaymentDB::ReadResult CEternitynodePaymentDB::Read(CEternitynodePayments& objToLoad, bool fDryRun)
{

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (eternitynode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid eternitynode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CEternitynodePayments object
        ssObj >> objToLoad;
    }
    catch (std::exception &e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from enpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", objToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("Eternitynode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrintf("Eternitynode payments manager - result:\n");
        LogPrintf("  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpEternitynodePayments()
{
    int64_t nStart = GetTimeMillis();

    CEternitynodePaymentDB paymentdb;
    CEternitynodePayments tempPayments;

    LogPrintf("Verifying enpayments.dat format...\n");
    CEternitynodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CEternitynodePaymentDB::FileError)
        LogPrintf("Missing evolutions file - enpayments.dat, will try to recreate\n");
    else if (readResult != CEternitynodePaymentDB::Ok)
    {
        LogPrintf("Error reading enpayments.dat: ");
        if(readResult == CEternitynodePaymentDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to enpayments.dat...\n");
    paymentdb.Write(eternitynodePayments);

    LogPrintf("Evolution dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue){
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev == NULL) return true;

    int nHeight = 0;
    if(pindexPrev->GetBlockHash() == block.hashPrevBlock)
    {
        nHeight = pindexPrev->nHeight+1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight+1;
    }

    if(nHeight == 0){
        LogPrintf("IsBlockValueValid() : WARNING: Couldn't find previous block");
    }

    if(!eternitynodeSync.IsSynced()) { //there is no evolution data to use to check anything
        //super blocks will always be on these blocks, max 100 per evolutioning
        if(nHeight % GetEvolutionPaymentCycleBlocks() < 100){
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    } else { // we're synced and have data so check the evolution schedule

        //are these blocks even enabled
        if(!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)){
            return block.vtx[0].GetValueOut() <= nExpectedValue;
        }
        
        if(evolution.IsEvolutionPaymentBlock(nHeight)){
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if(block.vtx[0].GetValueOut() > nExpectedValue) return false;
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight)
{
    if(!eternitynodeSync.IsSynced()) { //there is no evolution data to use to check anything -- find the longest chain
        LogPrint("enpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    //check if it's a evolution block
    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)){
        if(evolution.IsEvolutionPaymentBlock(nBlockHeight)){
            if(evolution.IsTransactionValid(txNew, nBlockHeight)){
                return true;
            } else {
                LogPrintf("Invalid evolution payment detected %s\n", txNew.ToString().c_str());
                if(IsSporkActive(SPORK_9_ETERNITYNODE_EVOLUTION_ENFORCEMENT)){
                    return false;
                } else {
                    LogPrintf("Evolution enforcement is disabled, accepting block\n");
                    return true;
                }
            }
        }
    }

    //check for eternitynode payee
    if(eternitynodePayments.IsTransactionValid(txNew, nBlockHeight))
    {
        return true;
    } else {
        LogPrintf("Invalid mn payment detected %s\n", txNew.ToString().c_str());
        if(IsSporkActive(SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT)){
            return false;
        } else {
            LogPrintf("Eternitynode payment enforcement is disabled, accepting block\n");
            return true;
        }
    }

    return false;
}


void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && evolution.IsEvolutionPaymentBlock(pindexPrev->nHeight+1)){
        evolution.FillBlockPayee(txNew, nFees);
    } else {
        eternitynodePayments.FillBlockPayee(txNew, nFees);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if(IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && evolution.IsEvolutionPaymentBlock(nBlockHeight)){
        return evolution.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return eternitynodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CEternitynodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if(!eternitynodePayments.GetBlockPayee(pindexPrev->nHeight+1, payee)){
        //no eternitynode detected
        CEternitynode* winningNode = enodeman.GetCurrentEternityNode(1);
        if(winningNode){
            payee = GetScriptForDestination(winningNode->pubkey.GetID());
        } else {
            LogPrintf("CreateNewBlock: Failed to detect eternitynode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nBits, pindexPrev->nHeight, nFees);
    CAmount eternitynodePayment = GetEternitynodePayment(pindexPrev->nHeight+1, blockValue);

    txNew.vout[0].nValue = blockValue;

    if(hasPayment){
        txNew.vout.resize(2);

        txNew.vout[1].scriptPubKey = payee;
        txNew.vout[1].nValue = eternitynodePayment;

        txNew.vout[0].nValue -= eternitynodePayment;

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("Eternitynode payment to %s\n", address2.ToString().c_str());
    }
}

int CEternitynodePayments::GetMinEternitynodePaymentsProto() {
    return IsSporkActive(SPORK_10_ETERNITYNODE_PAY_UPDATED_NODES)
            ? MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_2
            : MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_1;
}

void CEternitynodePayments::ProcessMessageEternitynodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(!eternitynodeSync.IsBlockchainSynced()) return;

    if(fLiteMode) return; //disable all Spysend/Eternitynode related functionality


    if (strCommand == "mnget") { //Eternitynode Payments Request Sync
        if(fLiteMode) return; //disable all Spysend/Eternitynode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(Params().NetworkID() == CBaseChainParams::MAIN){
            if(pfrom->HasFulfilledRequest("mnget")) {
                LogPrintf("mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("mnget");
        eternitynodePayments.Sync(pfrom, nCountNeeded);
        LogPrintf("mnget - Sent Eternitynode winners to %s\n", pfrom->addr.ToString().c_str());
    }
    else if (strCommand == "enw") { //Eternitynode Payments Declare Winner
        //this is required in litemodef
        CEternitynodePaymentWinner winner;
        vRecv >> winner;

        if(pfrom->nVersion < MIN_ENW_PEER_PROTO_VERSION) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if(!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if(eternitynodePayments.mapEternitynodePayeeVotes.count(winner.GetHash())){
            LogPrint("enpayments", "enw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            eternitynodeSync.AddedEternitynodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (enodeman.CountEnabled()*1.25);
        if(winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight+20){
            LogPrint("enpayments", "enw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if(!winner.IsValid(pfrom, strError)){
            if(strError != "") LogPrintf("enw - invalid message - %s\n", strError);
            return;
        }

        if(!eternitynodePayments.CanVote(winner.vinEternitynode.prevout, winner.nBlockHeight)){
            LogPrintf("enw - eternitynode already voted - %s\n", winner.vinEternitynode.prevout.ToStringShort());
            return;
        }

        if(!winner.SignatureValid()){
            LogPrintf("enw - invalid signature\n");
            if(eternitynodeSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced eternitynode
            enodeman.AskForMN(pfrom, winner.vinEternitynode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("enpayments", "enw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinEternitynode.prevout.ToStringShort());

        if(eternitynodePayments.AddWinningEternitynode(winner)){
            winner.Relay();
            eternitynodeSync.AddedEternitynodeWinner(winner.GetHash());
        }
    }
}

bool CEternitynodePaymentWinner::Sign(CKey& keyEternitynode, CPubKey& pubKeyEternitynode)
{
    std::string errorMessage;
    std::string strEternityNodeSignMessage;

    std::string strMessage =  vinEternitynode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payee.ToString();

    if(!spySendSigner.SignMessage(strMessage, errorMessage, vchSig, keyEternitynode)) {
        LogPrintf("CEternitynodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if(!spySendSigner.VerifyMessage(pubKeyEternitynode, vchSig, strMessage, errorMessage)) {
        LogPrintf("CEternitynodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CEternitynodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapEternitynodeBlocks.count(nBlockHeight)){
        return mapEternitynodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this eternitynode scheduled to get paid soon? 
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CEternitynodePayments::IsScheduled(CEternitynode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapEternitynodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if(!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubkey.GetID());

    CScript payee;
    for(int64_t h = nHeight; h <= nHeight+8; h++){
        if(h == nNotBlockHeight) continue;
        if(mapEternitynodeBlocks.count(h)){
            if(mapEternitynodeBlocks[h].GetPayee(payee)){
                if(mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CEternitynodePayments::AddWinningEternitynode(CEternitynodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-100)) {
        return false;
    }

    {
        LOCK2(cs_mapEternitynodePayeeVotes, cs_mapEternitynodeBlocks);
    
        if(mapEternitynodePayeeVotes.count(winnerIn.GetHash())){
           return false;
        }

        mapEternitynodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if(!mapEternitynodeBlocks.count(winnerIn.nBlockHeight)){
           CEternitynodeBlockPayees blockPayees(winnerIn.nBlockHeight);
           mapEternitynodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    int n = 1;
    if(IsReferenceNode(winnerIn.vinEternitynode)) n = 100;
    mapEternitynodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, n);

    return true;
}

bool CEternitynodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount eternitynodePayment = GetEternitynodePayment(nBlockHeight, txNew.GetValueOut());

    //require at least 6 signatures

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayments)
        if(payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayments)
    {
        bool found = false;
        BOOST_FOREACH(CTxOut out, txNew.vout){
            if(payee.scriptPubKey == out.scriptPubKey && eternitynodePayment == out.nValue){
                found = true;
            }
        }

        if(payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED){
            if(found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == ""){
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }


    LogPrintf("CEternitynodePayments::IsTransactionValid - Missing required payment - %s\n", strPayeesPossible.c_str());
    return false;
}

std::string CEternitynodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH(CEternitynodePayee& payee, vecPayments)
    {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if(ret != "Unknown"){
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
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

void CEternitynodePayments::CleanPaymentList()
{
    LOCK2(cs_mapEternitynodePayeeVotes, cs_mapEternitynodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if(!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(enodeman.size()*1.25), 1000);

    std::map<uint256, CEternitynodePaymentWinner>::iterator it = mapEternitynodePayeeVotes.begin();
    while(it != mapEternitynodePayeeVotes.end()) {
        CEternitynodePaymentWinner winner = (*it).second;

        if(nHeight - winner.nBlockHeight > nLimit){
            LogPrint("enpayments", "CEternitynodePayments::CleanPaymentList - Removing old Eternitynode payment - block %d\n", winner.nBlockHeight);
            eternitynodeSync.mapSeenSyncENW.erase((*it).first);
            mapEternitynodePayeeVotes.erase(it++);
            mapEternitynodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool IsReferenceNode(CTxIn& vin)
{
    //reference node - hybrid mode
    if(vin.prevout.ToStringShort() == "099c01bea63abd1692f60806bb646fa1d288e2d049281225f17e499024084e28-0") return true; // mainnet
    if(vin.prevout.ToStringShort() == "fbc16ae5229d6d99181802fd76a4feee5e7640164dcebc7f8feb04a7bea026f8-0") return true; // testnet
    if(vin.prevout.ToStringShort() == "e466f5d8beb4c2d22a314310dc58e0ea89505c95409754d0d68fb874952608cc-1") return true; // regtest

    return false;
}

bool CEternitynodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    if(IsReferenceNode(vinEternitynode)) return true;

    CEternitynode* pen = enodeman.Find(vinEternitynode);

    if(!pen)
    {
        strError = strprintf("Unknown Eternitynode %s", vinEternitynode.prevout.ToStringShort());
        LogPrintf ("CEternitynodePaymentWinner::IsValid - %s\n", strError);
        enodeman.AskForMN(pnode, vinEternitynode);
        return false;
    }

    if(pen->protocolVersion < MIN_ENW_PEER_PROTO_VERSION)
    {
        strError = strprintf("Eternitynode protocol too old %d - req %d", pen->protocolVersion, MIN_ENW_PEER_PROTO_VERSION);
        LogPrintf ("CEternitynodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = enodeman.GetEternitynodeRank(vinEternitynode, nBlockHeight-100, MIN_ENW_PEER_PROTO_VERSION);

    if(n > MNPAYMENTS_SIGNATURES_TOTAL)
    {    
        //It's common to have eternitynodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if(n > MNPAYMENTS_SIGNATURES_TOTAL*2)
        {
            strError = strprintf("Eternitynode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, n);
            LogPrintf("CEternitynodePaymentWinner::IsValid - %s\n", strError);
            if(eternitynodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CEternitynodePayments::ProcessBlock(int nBlockHeight)
{
    if(!fEternityNode) return false;

    //reference node - hybrid mode

    if(!IsReferenceNode(activeEternitynode.vin)){
        int n = enodeman.GetEternitynodeRank(activeEternitynode.vin, nBlockHeight-100, MIN_ENW_PEER_PROTO_VERSION);

        if(n == -1)
        {
            LogPrint("enpayments", "CEternitynodePayments::ProcessBlock - Unknown Eternitynode\n");
            return false;
        }

        if(n > MNPAYMENTS_SIGNATURES_TOTAL)
        {
            LogPrint("enpayments", "CEternitynodePayments::ProcessBlock - Eternitynode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
            return false;
        }
    }

    if(nBlockHeight <= nLastBlockHeight) return false;

    CEternitynodePaymentWinner newWinner(activeEternitynode.vin);

    if(evolution.IsEvolutionPaymentBlock(nBlockHeight)){
        //is evolution payment block -- handled by the evolutioning software
    } else {
        LogPrintf("CEternitynodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeEternitynode.vin.ToString().c_str());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CEternitynode *pen = enodeman.GetNextEternitynodeInQueueForPayment(nBlockHeight, true, nCount);
        
        if(pen != NULL)
        {
            LogPrintf("CEternitynodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pen->pubkey.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrintf("CEternitynodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrintf("CEternitynodePayments::ProcessBlock() Failed to find eternitynode to pay\n");
        }

    }

    std::string errorMessage;
    CPubKey pubKeyEternitynode;
    CKey keyEternitynode;

    if(!spySendSigner.SetKey(strEternityNodePrivKey, errorMessage, keyEternitynode, pubKeyEternitynode))
    {
        LogPrintf("CEternitynodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrintf("CEternitynodePayments::ProcessBlock() - Signing Winner\n");
    if(newWinner.Sign(keyEternitynode, pubKeyEternitynode))
    {
        LogPrintf("CEternitynodePayments::ProcessBlock() - AddWinningEternitynode\n");

        if(AddWinningEternitynode(newWinner))
        {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CEternitynodePaymentWinner::Relay()
{
    CInv inv(MSG_ETERNITYNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CEternitynodePaymentWinner::SignatureValid()
{

    CEternitynode* pen = enodeman.Find(vinEternitynode);

    if(pen != NULL)
    {
        std::string strMessage =  vinEternitynode.prevout.ToStringShort() +
                    boost::lexical_cast<std::string>(nBlockHeight) +
                    payee.ToString();

        std::string errorMessage = "";
        if(!spySendSigner.VerifyMessage(pen->pubkey2, vchSig, strMessage, errorMessage)){
            return error("CEternitynodePaymentWinner::SignatureValid() - Got bad Eternitynode address signature %s \n", vinEternitynode.ToString().c_str());
        }

        return true;
    }

    return false;
}

void CEternitynodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapEternitynodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if(!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (enodeman.CountEnabled()*1.25);
    if(nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CEternitynodePaymentWinner>::iterator it = mapEternitynodePayeeVotes.begin();
    while(it != mapEternitynodePayeeVotes.end()) {
        CEternitynodePaymentWinner winner = (*it).second;
        if(winner.nBlockHeight >= nHeight-nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_ETERNITYNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", ETERNITYNODE_SYNC_ENW, nInvCount);
}

std::string CEternitynodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapEternitynodePayeeVotes.size() <<
            ", Blocks: " << (int)mapEternitynodeBlocks.size();

    return info.str();
}



int CEternitynodePayments::GetOldestBlock()
{
    LOCK(cs_mapEternitynodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CEternitynodeBlockPayees>::iterator it = mapEternitynodeBlocks.begin();
    while(it != mapEternitynodeBlocks.end()) {
        if((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}



int CEternitynodePayments::GetNewestBlock()
{
    LOCK(cs_mapEternitynodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CEternitynodeBlockPayees>::iterator it = mapEternitynodeBlocks.begin();
    while(it != mapEternitynodeBlocks.end()) {
        if((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
