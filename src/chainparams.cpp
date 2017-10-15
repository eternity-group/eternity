// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "17/09/2016 euronews.com  - Russia casts doubt on US commitment to Syria ceasefire";
    const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210240; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.nEternitynodePaymentsStartBlock = 10; // not true, but it's ok as long as it's less then nEternitynodePaymentsIncreaseBlock
        consensus.nEternitynodePaymentsIncreaseBlock = 11; // actual historical value
        consensus.nEternitynodePaymentsIncreasePeriod = 576*30; // 17280 - actual historical value
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 328008; // actual historical value
        consensus.nBudgetPaymentsCycleBlocks = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nBudgetProposalEstablishingTime = 60*60*24;
        consensus.nSuperblockStartBlock = 1; // The block at which 12.1 goes live (end of final 12.0 budget cycle)
        consensus.nSuperblockCycle = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nEternitynodeMinimumConfirmations = 15;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x00000b602d00ef2d0466aa9156a905f59c6e5944a088782614a90e58089070c4");
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Eternity: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Eternity: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1486252800; // Feb 5th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1517788800; // Feb 5th, 2018

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
		pchMessageStart[0] = 0x8f;
        pchMessageStart[1] = 0xf7;
        pchMessageStart[2] = 0x4d;
        pchMessageStart[3] = 0x2e;
		
        vAlertPubKey = ParseHex("04e4f6185c218e4dc6427910b22d144dfe79310f5d0036c0fed23122c3de79d9e025defb443675da809c5b3e56286e7b8b732af93e9aa40b6d9db299cd044f6f06");
        nDefaultPort = 4855;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1474140700, 1590768, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000b602d00ef2d0466aa9156a905f59c6e5944a088782614a90e58089070c4"));
        assert(genesis.hashMerkleRoot == uint256S("0xb35b2551a3dc3c69177dd167ca8faae415bca87b1227aded604d254555044c30"));


        vSeeds.push_back(CDNSSeedData("eternity-group.org", "dnsseed.eternity-group.org"));

        // Eternity addresses start with 'E'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,33);
        // Eternity script addresses start with '4'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,8);
        // Eternity private keys start with 
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,101);
        // Eternity BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        // Eternity BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        // Eternity BIP44 coin type is '5'
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x80)(0x00)(0x00)(0x05).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "04e3c7cdbe02a924109f273edb2f6ae3e240e2a08ff0b2b15d84a2729e1ce711e61f70bcf2069f3fad1c40655bb68db3e29563319de2ea3127ecc27acad5e45731";
        strEternitynodePaymentsPubKey = "04e3c7cdbe02a924109f273edb2f6ae3e240e2a08ff0b2b15d84a2729e1ce711e61f70bcf2069f3fad1c40655bb68db3e29563319de2ea3127ecc27acad5e45731";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
			( 17247, uint256S("0x00000000001a84bc05b8348382b74cc02d990d70688cd6874eeb45279f9cc46c"))
			( 27335, uint256S("0x0000000001ad3bcefe6ae335a03f844cc4d4c6de6711babda26172e26d6b3fb5"))
			( 39710, uint256S("0x0000000006dc0a7be7adb3c5147c5783e9750e368f0c0bf2c225573782f40a1f"))
			(178540, uint256S("0x0000000000192d6bdb38651ebc984dfae6ad79962546a9a6cc54e0f16974ca36"))
			(178540, uint256S("0x0000000000075ff8f73d81f795c9696aa9f0e46cb172d989a772edea986c4a9b")),
			1508079875, // * UNIX timestamp of last checkpoint block
            285347,    // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            2800        // * estimated number of transactions per day after checkpoint
        };
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210240;
        consensus.nEternitynodePaymentsStartBlock = 1; // not true, but it's ok as long as it's less then nEternitynodePaymentsIncreaseBlock
        consensus.nEternitynodePaymentsIncreaseBlock = 2;
        consensus.nEternitynodePaymentsIncreasePeriod = 576;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nEternitynodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x000004be80c8a589024cb741114475d7d83ab0473c85d6131443f67f8d6f3f30");
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Eternity: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Eternity: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        pchMessageStart[0] = 0xc3;
        pchMessageStart[1] = 0xb3;
        pchMessageStart[2] = 0xea;
        pchMessageStart[3] = 0x5b;
        vAlertPubKey = ParseHex("046e67a3b8855c98e6a9fe4412b602afc98bf8720a61588d306aaf5e460038e24995027283768841078d8584e2f5ece25b776e0f5f17e446323e1174bb81345d2b");
        nDefaultPort = 14855;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1474140701UL, 42601UL, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000004be80c8a589024cb741114475d7d83ab0473c85d6131443f67f8d6f3f30"));
        assert(genesis.hashMerkleRoot == uint256S("0xb35b2551a3dc3c69177dd167ca8faae415bca87b1227aded604d254555044c30"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("eternity-group.org", "144.76.33.134"));

        // Testnet Eternity addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,93);
        // Testnet Eternity script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,10);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Eternity BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Testnet Eternity BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Testnet Eternity BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x01)(0x00)(0x00)(0x80).convert_to_container<std::vector<unsigned char> >();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "046e67a3b8855c98e6a9fe4412b602afc98bf8720a61588d306aaf5e460038e24995027283768841078d8584e2f5ece25b776e0f5f17e446323e1174bb81345d2b";
        strEternitynodePaymentsPubKey = "046e67a3b8855c98e6a9fe4412b602afc98bf8720a61588d306aaf5e460038e24995027283768841078d8584e2f5ece25b776e0f5f17e446323e1174bb81345d2b";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            ( 0, uint256S("000004be80c8a589024cb741114475d7d83ab0473c85d6131443f67f8d6f3f30")),

            1474140701, // * UNIX timestamp of last checkpoint block
            0,     // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0         // * estimated number of transactions per day after checkpoint
        };

    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nEternitynodePaymentsStartBlock = 240;
        consensus.nEternitynodePaymentsIncreaseBlock = 350;
        consensus.nEternitynodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockCycle = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nEternitynodeMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP34Hash = uint256();
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Eternity: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Eternity: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;

        pchMessageStart[0] = 0x24;
        pchMessageStart[1] = 0x5d;
        pchMessageStart[2] = 0xc7;
        pchMessageStart[3] = 0xb4;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDefaultPort = 14885;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1474140702, 0, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x60a4151bc509d3d85eeabc0b28fcbd115c4274cc07fd165fe6b69d73a476a7c2"));
        assert(genesis.hashMerkleRoot == uint256S("0xb35b2551a3dc3c69177dd167ca8faae415bca87b1227aded604d254555044c30"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0x60a4151bc509d3d85eeabc0b28fcbd115c4274cc07fd165fe6b69d73a476a7c2")),
            1474140702,
            0,
            0
        };
        // Regtest Eternity addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Regtest Eternity script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest Eternity BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Regtest Eternity BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Regtest Eternity BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE]  = boost::assign::list_of(0x80)(0x00)(0x00)(0x01).convert_to_container<std::vector<unsigned char> >();
   }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}
