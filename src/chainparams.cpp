// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016 The Eternity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

/**
 * Main network
 */

//! Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

static Checkpoints::MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (    0, uint256("0x00000b602d00ef2d0466aa9156a905f59c6e5944a088782614a90e58089070c4"))
	(17247, uint256("0x00000000001a84bc05b8348382b74cc02d990d70688cd6874eeb45279f9cc46c"))
        ;
static const Checkpoints::CCheckpointData data = {
        &mapCheckpoints,
        1476872302, // * UNIX timestamp of last checkpoint block
        23376,    // * total number of transactions between genesis and last checkpoint
              //   (the tx=... number in the SetBestChain debug.log lines)
        2800  // * estimated number of transactions per day after checkpoint
    };

static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 0, uint256("000004be80c8a589024cb741114475d7d83ab0473c85d6131443f67f8d6f3f30"))
        ;
static const Checkpoints::CCheckpointData dataTestnet = {
        &mapCheckpointsTestnet,
        1474140701,
        0,
        0
    };

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
        boost::assign::map_list_of
        ( 0, uint256("0x60a4151bc509d3d85eeabc0b28fcbd115c4274cc07fd165fe6b69d73a476a7c2"))
        ;
static const Checkpoints::CCheckpointData dataRegtest = {
        &mapCheckpointsRegtest,
        1474140702,
        0,
        0
    };

class CMainParams : public CChainParams {
public:
    CMainParams() {
        networkID = CBaseChainParams::MAIN;
        strNetworkID = "main";
        /** 
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0x8f;
        pchMessageStart[1] = 0xf7;
        pchMessageStart[2] = 0x4d;
        pchMessageStart[3] = 0x2e;
        vAlertPubKey = ParseHex("04a503e93be1b29e587840df32335b107dfd8a08bd7f8a8710b7e4cb7881e38535239ad0daa03aa1b32c96738f5ee29f725df6b04afd38c2d473d998663c06db7d");
        nDefaultPort = 4855;
        bnProofOfWorkLimit = ~uint256(0) >> 20;  // Eternity starting difficulty is 1 / 2^12
        nSubsidyHalvingInterval = 210000;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 0;
        nTargetTimespan = 24 * 60 * 60; // Eternity: 1 day
        nTargetSpacing = 2.5 * 60; // Eternity: 2.5 minutes
        
        const char* pszTimestamp = "17/09/2016 euronews.com  - Russia casts doubt on US commitment to Syria ceasefire";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 50 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1474140700;
        genesis.nBits    = 0x1e0ffff0;
        genesis.nNonce   = 1590768;

	
        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x00000b602d00ef2d0466aa9156a905f59c6e5944a088782614a90e58089070c4"));
        assert(genesis.hashMerkleRoot == uint256("0xb35b2551a3dc3c69177dd167ca8faae415bca87b1227aded604d254555044c30"));

        vSeeds.push_back(CDNSSeedData("eternity-group.org", "144.76.33.134"));
	vSeeds.push_back(CDNSSeedData("95.31.211.13", "95.31.211.13"));
	vSeeds.push_back(CDNSSeedData("92.255.229.121", "92.255.229.121"));
	vSeeds.push_back(CDNSSeedData("176.215.13.48", "176.215.13.48"));


	#if __cplusplus > 199711L
		base58Prefixes[PUBKEY_ADDRESS] = {33};                    // Eternity addresses start with 'E'
		base58Prefixes[SCRIPT_ADDRESS] = {8};                    // Eternity script addresses start with '4'
		base58Prefixes[SECRET_KEY] =     {101};                    // Eternity private keys start with 
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x88,0xB2,0x1E}; // Eternity BIP32 pubkeys start with 'xpub'
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x88,0xAD,0xE4}; // Eternity BIP32 prvkeys start with 'xprv'
		base58Prefixes[EXT_COIN_TYPE]  = {0x05,0x00,0x00,0x80};             // Eternity BIP44 coin type is '5'
	#else
		base58Prefixes[PUBKEY_ADDRESS] = list_of( 33);                    // Eternity addresses start with 'E'
		base58Prefixes[SCRIPT_ADDRESS] = list_of(  8);                    // Eternity script addresses start with '4'
		base58Prefixes[SECRET_KEY] =     list_of(101);                    // Eternity private keys start with 
		base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x04)(0x88)(0xB2)(0x1E); // Eternity BIP32 pubkeys start with 'xpub'
		base58Prefixes[EXT_SECRET_KEY] = list_of(0x04)(0x88)(0xAD)(0xE4); // Eternity BIP32 prvkeys start with 'xprv'
		base58Prefixes[EXT_COIN_TYPE]  = list_of(0x80000005);             // Eternity BIP44 coin type is '5'
	#endif
		
        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = false;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        strSporkKey = "04bdeb03313acc086c882166f66a4dfa64b765131f350235e2780cca87024735a12561609f391d2791eaf7984fc0bb3f41c82d51d5ac0e29c764ea9fb8dddb6217";
        strEternitynodePaymentsPubKey = "04bdeb03313acc086c882166f66a4dfa64b765131f350235e2780cca87024735a12561609f391d2791eaf7984fc0bb3f41c82d51d5ac0e29c764ea9fb8dddb6217";
        strSpysendPoolDummyAddress = "EVMsMLRzz6YJeLAPodDZdPA5mjCt1GhfYp";
        nStartEternitynodePayments = 1474140700; 
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return data;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        networkID = CBaseChainParams::TESTNET;
        strNetworkID = "test";
        pchMessageStart[0] = 0xc3;
        pchMessageStart[1] = 0xb3;
        pchMessageStart[2] = 0xea;
        pchMessageStart[3] = 0x5b;
        vAlertPubKey = ParseHex("040c08e4102239546f2f9634779664058e8f5a0d10ae289e8b404ac8352ee242cddec55d337af29331f0af5c732c50b8da200f6f6f246ce8293e63b49a9f8630fc");
        nDefaultPort = 14855 ;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        nTargetTimespan = 24 * 60 * 60; // Eternity: 1 day
        nTargetSpacing = 2.5 * 60; // Eternity: 2.5 minutes

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime = 1474140701;
        genesis.nNonce = 42601;

	
        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x000004be80c8a589024cb741114475d7d83ab0473c85d6131443f67f8d6f3f30"));

        vFixedSeeds.clear();
        vSeeds.clear();

        vSeeds.push_back(CDNSSeedData("eternity-group.org", "144.76.33.134"));
	#if __cplusplus > 199711L
		base58Prefixes[PUBKEY_ADDRESS] = {93};                    // Testnet eternity addresses start with 'e'
		base58Prefixes[SCRIPT_ADDRESS] = {10};                    // Testnet eternity script addresses start with '8' or '9'
		base58Prefixes[SECRET_KEY]     = {239};                    // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = {0x04,0x35,0x87,0xCF}; // Testnet eternity BIP32 pubkeys start with 'tpub'
		base58Prefixes[EXT_SECRET_KEY] = {0x04,0x35,0x83,0x94}; // Testnet eternity BIP32 prvkeys start with 'tprv'
		base58Prefixes[EXT_COIN_TYPE]  = {0x01,0x00,0x00,0x80};             // Testnet eternity BIP44 coin type is '5' (All coin's testnet default)
	#else
		base58Prefixes[PUBKEY_ADDRESS] = list_of( 93);                    // Testnet eternity addresses start with 'e'
		base58Prefixes[SCRIPT_ADDRESS] = list_of( 10);                    // Testnet eternity script addresses start with '8' or '9'
		base58Prefixes[SECRET_KEY]     = list_of(239);                    // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
		base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x04)(0x35)(0x87)(0xCF); // Testnet eternity BIP32 pubkeys start with 'tpub'
		base58Prefixes[EXT_SECRET_KEY] = list_of(0x04)(0x35)(0x83)(0x94); // Testnet eternity BIP32 prvkeys start with 'tprv'
		base58Prefixes[EXT_COIN_TYPE]  = list_of(0x80000001);             // Testnet eternity BIP44 coin type is '5' (All coin's testnet default)		
	#endif
		
		
        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 2;
        strSporkKey = "049ffae56340579049f0d276059e4ded3576cf937ee5ce3e27ad84dbfdb4c47bbccf3e07abe5c32354cf970a88304f6f41c5345083a75cb4b12c56041c1425fedb";
        strEternitynodePaymentsPubKey = "049ffae56340579049f0d276059e4ded3576cf937ee5ce3e27ad84dbfdb4c47bbccf3e07abe5c32354cf970a88304f6f41c5345083a75cb4b12c56041c1425fedb";
        strSpysendPoolDummyAddress = "eWfLe1k5X7eku7K78Ss6VNnastQ4t6y6fE";
        nStartEternitynodePayments = 1474140701; 
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataTestnet;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        networkID = CBaseChainParams::REGTEST;
        strNetworkID = "regtest";
        pchMessageStart[0] = 0x24;
        pchMessageStart[1] = 0x5d;
        pchMessageStart[2] = 0xc7;
        pchMessageStart[3] = 0xb4;
        nSubsidyHalvingInterval = 150;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        nTargetTimespan = 24 * 60 * 60; // Eternity: 1 day
        nTargetSpacing = 2.5 * 60; // Eternity: 2.5 minutes
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        genesis.nTime = 1474140702;
        genesis.nBits = 0x207fffff;
        genesis.nNonce = 0;
			
		
        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 14885;
        assert(hashGenesisBlock == uint256("0x60a4151bc509d3d85eeabc0b28fcbd115c4274cc07fd165fe6b69d73a476a7c2"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        return dataRegtest;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams {
public:
    CUnitTestParams() {
        networkID = CBaseChainParams::UNITTEST;
        strNetworkID = "unittest";
        nDefaultPort = 14385;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Unit test mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const 
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval)  { nSubsidyHalvingInterval=anSubsidyHalvingInterval; }
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority)  { nEnforceBlockUpgradeMajority=anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority)  { nRejectBlockOutdatedMajority=anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority)  { nToCheckBlockUpgradeMajority=anToCheckBlockUpgradeMajority; }
    virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks)  { fDefaultConsistencyChecks=afDefaultConsistencyChecks; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) {  fAllowMinDifficultyBlocks=afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;


static CChainParams *pCurrentParams = 0;

CModifiableParams *ModifiableParams()
{
   assert(pCurrentParams);
   assert(pCurrentParams==&unitTestParams);
   return (CModifiableParams*)&unitTestParams;
}

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        case CBaseChainParams::UNITTEST:
            return unitTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
