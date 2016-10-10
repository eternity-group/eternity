// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACTIVEETERNITYNODE_H
#define ACTIVEETERNITYNODE_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "init.h"
#include "wallet.h"
#include "spysend.h"
#include "eternitynode.h"

#define ACTIVE_ETERNITYNODE_INITIAL                     0 // initial state
#define ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS             1
#define ACTIVE_ETERNITYNODE_INPUT_TOO_NEW               2
#define ACTIVE_ETERNITYNODE_NOT_CAPABLE                 3
#define ACTIVE_ETERNITYNODE_STARTED                     4

// Responsible for activating the Eternitynode and pinging the network
class CActiveEternitynode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Eternitynode
    bool SendEternitynodePing(std::string& errorMessage);

    /// Create Eternitynode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyEternitynode, CPubKey pubKeyEternitynode, std::string &errorMessage, CEternitynodeBroadcast &enb);

    /// Get 1000DRK input that can be used for the Eternitynode
    bool GetEternityNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
	// Initialized by init.cpp
	// Keys for the main Eternitynode
	CPubKey pubKeyEternitynode;

	// Initialized while registering Eternitynode
	CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveEternitynode()
    {        
        status = ACTIVE_ETERNITYNODE_INITIAL;
    }

    /// Manage status of main Eternitynode
    void ManageStatus(); 
    std::string GetStatus();

    /// Create Eternitynode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CEternitynodeBroadcast &enb, bool fOffline = false);

    /// Get 1000DRK input that can be used for the Eternitynode
    bool GetEternityNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsEternitynode();

    /// Enable cold wallet mode (run a Eternitynode with no funds)
    bool EnableHotColdEternityNode(CTxIn& vin, CService& addr);
};

#endif
