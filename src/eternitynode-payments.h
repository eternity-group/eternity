// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ETERNITYNODE_PAYMENTS_H
#define ETERNITYNODE_PAYMENTS_H

#include "util.h"
#include "core_io.h"
#include "key.h"
#include "main.h"
#include "eternitynode.h"
#include "utilstrencodings.h"

class CEternitynodePayments;
class CEternitynodePaymentVote;
class CEternitynodeBlockPayees;

static const int ENPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int ENPAYMENTS_SIGNATURES_TOTAL            = 10;

//! minimum peer version that can receive and send eternitynode payment messages,
//  vote for eternitynode and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_1 = 70206;
static const int MIN_ETERNITYNODE_PAYMENT_PROTO_VERSION_2 = 70206;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapEternitynodeBlocks;
extern CCriticalSection cs_mapEternitynodePayeeVotes;

extern CEternitynodePayments enpayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount blockEvolution, CTxOut& txoutEternitynodeRet, std::vector<CTxOut>& voutSuperblockRet);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CEternitynodePayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;

public:
    CEternitynodePayee() :
        scriptPubKey(),
        vecVoteHashes()
        {}

    CEternitynodePayee(CScript payee, uint256 hashIn) :
        scriptPubKey(payee),
        vecVoteHashes()
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() { return vecVoteHashes.size(); }
};

// Keep track of votes for payees from eternitynodes
class CEternitynodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CEternitynodePayee> vecPayees;

    CEternitynodeBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CEternitynodeBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CEternitynodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet);
    bool HasPayeeWithVotes(CScript payeeIn, int nVotesReq);

    bool IsTransactionValid(const CTransaction& txNew);

    std::string GetRequiredPaymentsString();
};

// vote for the winning payment
class CEternitynodePaymentVote
{
public:
    CTxIn vinEternitynode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CEternitynodePaymentVote() :
        vinEternitynode(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CEternitynodePaymentVote(CTxIn vinEternitynode, int nBlockHeight, CScript payee) :
        vinEternitynode(vinEternitynode),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinEternitynode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinEternitynode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyEternitynode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError);
    void Relay();

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Eternitynode Payments Class
// Keeps track of who should get paid for which blocks
//

class CEternitynodePayments
{
private:
    // eternitynode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

public:
    std::map<uint256, CEternitynodePaymentVote> mapEternitynodePaymentVotes;
    std::map<int, CEternitynodeBlockPayees> mapEternitynodeBlocks;
    std::map<COutPoint, int> mapEternitynodesLastVote;

    CEternitynodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapEternitynodePaymentVotes);
        READWRITE(mapEternitynodeBlocks);
    }

    void Clear();

    bool AddPaymentVote(const CEternitynodePaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node);
    void RequestLowDataPaymentBlocks(CNode* pnode);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CEternitynode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outEternitynode, int nBlockHeight);

    int GetMinEternitynodePaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutEternitynodeRet);
    std::string ToString() const;

    int GetBlockCount() { return mapEternitynodeBlocks.size(); }
    int GetVoteCount() { return mapEternitynodePaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();
	static void CreateEvolution(CMutableTransaction& txNewRet, int nBlockHeight, CAmount blockEvolution, std::vector<CTxOut>& voutSuperblockRet);	
	
    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
