// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"

class CSporkMessage;
class CSporkManager;
class CEvolutionManager;

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
static const int SPORK_START                                            = 10001;
static const int SPORK_END                                              = 10013;

static const int SPORK_2_INSTANTSEND_ENABLED                            = 10001;
static const int SPORK_3_INSTANTSEND_BLOCK_FILTERING                    = 10002;
static const int SPORK_5_INSTANTSEND_MAX_VALUE                          = 10004;
static const int SPORK_6_EVOLUTION_PAYMENTS								= 10006;
static const int SPORK_7_EVOLUTION_PAYMENTS_ENFORCEMENT					= 10005;
static const int SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT                 = 10007;
static const int SPORK_9_SUPERBLOCKS_ENABLED                            = 10008;
static const int SPORK_10_ETERNITYNODE_PAY_UPDATED_NODES                  = 10009;
static const int SPORK_12_RECONSIDER_BLOCKS                             = 10011;
static const int SPORK_13_OLD_SUPERBLOCK_FLAG                           = 10012;
static const int SPORK_14_REQUIRE_SENTINEL_FLAG                         = 10013;

static const int64_t SPORK_2_INSTANTSEND_ENABLED_DEFAULT                = 0;            // ON
static const int64_t SPORK_3_INSTANTSEND_BLOCK_FILTERING_DEFAULT        = 0;            // ON
static const int64_t SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT              = 1000;         // 1000 ENT
static const int64_t SPORK_6_EVOLUTION_PAYMENTS_DEFAULT					= 0;			// 0-OFF, 1-ON
static const int64_t SPORK_7_EVOLUTION_PAYMENTS_ENFORCEMENT_DEFAULT		= 0x7FFFFFFF;			// 0-OFF, 1-ON
static const int64_t SPORK_8_ETERNITYNODE_PAYMENT_ENFORCEMENT_DEFAULT     = 4070908800ULL;// OFF
static const int64_t SPORK_9_SUPERBLOCKS_ENABLED_DEFAULT                = 4070908800ULL;// OFF
static const int64_t SPORK_10_ETERNITYNODE_PAY_UPDATED_NODES_DEFAULT      = 4070908800ULL;// OFF
static const int64_t SPORK_12_RECONSIDER_BLOCKS_DEFAULT                 = 0;            // 0 BLOCKS
static const int64_t SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT               = 4070908800ULL;// OFF
static const int64_t SPORK_14_REQUIRE_SENTINEL_FLAG_DEFAULT             = 4070908800ULL;// OFF

extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;
extern CEvolutionManager evolutionManager;

//
// Spork classes
// Keep track of all of the network spork settings
//

class CSporkMessage
{
private:
    std::vector<unsigned char> vchSig;

public:
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;
	std::string	sWEvolution;	

    CSporkMessage(int nSporkID, int64_t nValue, std::string sEvolution, int64_t nTimeSigned) :
        nSporkID(nSporkID),
        nValue(nValue),
        nTimeSigned(nTimeSigned),
		sWEvolution( sEvolution )
        {}

    CSporkMessage() :
        nSporkID(0),
        nValue(0),
        nTimeSigned(0),
		sWEvolution("")
        {}


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
		READWRITE(sWEvolution);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nSporkID;
        ss << nValue;
        ss << nTimeSigned;
		ss << sWEvolution;
        return ss.GetHash();
    }

    bool Sign(std::string strSignKey);
    bool CheckSignature();
    void Relay();
};


class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;
    std::string strMasterPrivKey;
    std::map<int, CSporkMessage> mapSporksActive;

public:

    CSporkManager() {}

    void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ExecuteSpork(int nSporkID, int nValue);
    bool UpdateSpork(int nSporkID, int64_t nValue, std::string sEvol );

    bool IsSporkActive(int nSporkID);
	int64_t getActiveSporkValue( int nSporkID );
	bool IsSporkWorkActive(int nSporkID);
    int64_t GetSporkValue(int nSporkID);
    int GetSporkIDByName(std::string strName);
    std::string GetSporkNameByID(int nSporkID);

    bool SetPrivKey(std::string strPrivKey);
};


class CEvolutionManager
{
private:
	std::map<int, std::string> mapEvolution;

public:

	CEvolutionManager() {}
	
	void setNewEvolutions( const std::string &sEvol );
	std::string getEvolution( int nBlockHeight );
	bool IsTransactionValid( const CTransaction& txNew, int nBlockHeight, CAmount blockCurEvolution );
	bool checkEvolutionString( const std::string &sEvol );
};

#endif
