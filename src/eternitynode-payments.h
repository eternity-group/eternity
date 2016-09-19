

// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ETERNITYNODE_PAYMENTS_H
#define ETERNITYNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "eternitynode.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapEternitynodeBlocks;
extern CCriticalSection cs_mapEternitynodePayeeVotes;

class CEternitynodePayments;
class CEternitynodePaymentWinner;
class CEternitynodeBlockPayees;

extern CEternitynodePayments eternitynodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED           6
#define MNPAYMENTS_SIGNATURES_TOTAL              10

void ProcessMessageEternitynodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsReferenceNode(CTxIn& vin);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue);
void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees);

void DumpEternitynodePayments();

/** Save Eternitynode Payment Data (enpayments.dat)
 */
class CEternitynodePaymentDB
{
private:
    boost::filesystem::path pathDB;
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

    CEternitynodePaymentDB();
    bool Write(const CEternitynodePayments &objToSave);
    ReadResult Read(CEternitynodePayments& objToLoad, bool fDryRun = false);
};

class CEternitynodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CEternitynodePayee() {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CEternitynodePayee(CScript payee, int nVotesIn) {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
     }
};

// Keep track of votes for payees from eternitynodes
class CEternitynodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CEternitynodePayee> vecPayments;

    CEternitynodeBlockPayees(){
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CEternitynodeBlockPayees(int nBlockHeightIn) {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement){
        LOCK(cs_vecPayments);

        BOOST_FOREACH(CEternitynodePayee& payee, vecPayments){
            if(payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CEternitynodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH(CEternitynodePayee& p, vecPayments){
            if(p.nVotes > nVotes){
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH(CEternitynodePayee& p, vecPayments){
            if(p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
     }
};

// for storing the winning payments
class CEternitynodePaymentWinner
{
public:
    CTxIn vinEternitynode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CEternitynodePaymentWinner() {
        nBlockHeight = 0;
        vinEternitynode = CTxIn();
        payee = CScript();
    }

    CEternitynodePaymentWinner(CTxIn vinIn) {
        nBlockHeight = 0;
        vinEternitynode = vinIn;
        payee = CScript();
    }

    uint256 GetHash(){
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinEternitynode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyEternitynode, CPubKey& pubKeyEternitynode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn){
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinEternitynode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinEternitynode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Eternitynode Payments Class
// Keeps track of who should get paid for which blocks
//

class CEternitynodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CEternitynodePaymentWinner> mapEternitynodePayeeVotes;
    std::map<int, CEternitynodeBlockPayees> mapEternitynodeBlocks;
    std::map<uint256, int> mapEternitynodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CEternitynodePayments() {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear() {
        LOCK2(cs_mapEternitynodeBlocks, cs_mapEternitynodePayeeVotes);
        mapEternitynodeBlocks.clear();
        mapEternitynodePayeeVotes.clear();
    }

    bool AddWinningEternitynode(CEternitynodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CEternitynode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CEternitynode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outEternitynode, int nBlockHeight) {
        LOCK(cs_mapEternitynodePayeeVotes);

        if(mapEternitynodesLastVote.count(outEternitynode.hash + outEternitynode.n)) {
            if(mapEternitynodesLastVote[outEternitynode.hash + outEternitynode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this eternitynode voted
        mapEternitynodesLastVote[outEternitynode.hash + outEternitynode.n] = nBlockHeight;
        return true;
    }

    int GetMinEternitynodePaymentsProto();
    void ProcessMessageEternitynodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapEternitynodePayeeVotes);
        READWRITE(mapEternitynodeBlocks);
    }
};



#endif
