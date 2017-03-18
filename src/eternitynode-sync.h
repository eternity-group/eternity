// Copyright (c) 2016 The Eternity developers

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ETERNITYNODE_SYNC_H
#define ETERNITYNODE_SYNC_H

#define ETERNITYNODE_SYNC_INITIAL           0
#define ETERNITYNODE_SYNC_SPORKS            1
#define ETERNITYNODE_SYNC_LIST              2
#define ETERNITYNODE_SYNC_ENW               3
#define ETERNITYNODE_SYNC_EVOLUTION            4
#define ETERNITYNODE_SYNC_EVOLUTION_PROP       10
#define ETERNITYNODE_SYNC_EVOLUTION_FIN        11
#define ETERNITYNODE_SYNC_FAILED            998
#define ETERNITYNODE_SYNC_FINISHED          999

#define ETERNITYNODE_SYNC_TIMEOUT           5
#define ETERNITYNODE_SYNC_THRESHOLD         2

class CEternitynodeSync;
extern CEternitynodeSync eternitynodeSync;

//
// CEternitynodeSync : Sync eternitynode assets in stages
//

class CEternitynodeSync
{
public:
    std::map<uint256, int> mapSeenSyncENB;
    std::map<uint256, int> mapSeenSyncENW;
    std::map<uint256, int> mapSeenSyncEvolution;

    int64_t lastEternitynodeList;
    int64_t lastEternitynodeWinner;
    int64_t lastEvolutionItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumEternitynodeList;
    int sumEternitynodeWinner;
    int sumEvolutionItemProp;
    int sumEvolutionItemFin;
    // peers that reported counts
    int countEternitynodeList;
    int countEternitynodeWinner;
    int countEvolutionItemProp;
    int countEvolutionItemFin;

    // Count peers we've requested the list from
    int RequestedEternitynodeAssets;
    int RequestedEternitynodeAttempt;

    // Time when current eternitynode asset sync started
    int64_t nAssetSyncStarted;

    CEternitynodeSync();

    void AddedEternitynodeList(uint256 hash);
    void AddedEternitynodeWinner(uint256 hash);
    void AddedEvolutionItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsEvolutionFinEmpty();
    bool IsEvolutionPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    void ClearFulfilledRequest();
};

#endif
