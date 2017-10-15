// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEETERNITYNODE_H
#define ACTIVEETERNITYNODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

class CActiveEternitynode;

static const int ACTIVE_ETERNITYNODE_INITIAL          = 0; // initial state
static const int ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_ETERNITYNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_ETERNITYNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_ETERNITYNODE_STARTED          = 4;

extern CActiveEternitynode activeEternitynode;

// Responsible for activating the Eternitynode and pinging the network
class CActiveEternitynode
{
public:
    enum eternitynode_type_enum_t {
        ETERNITYNODE_UNKNOWN = 0,
        ETERNITYNODE_REMOTE  = 1,
        ETERNITYNODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    eternitynode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Eternitynode
    bool SendEternitynodePing();

public:
    // Keys for the active Eternitynode
    CPubKey pubKeyEternitynode;
    CKey keyEternitynode;

    // Initialized while registering Eternitynode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_ETERNITYNODE_XXXX
    std::string strNotCapableReason;

    CActiveEternitynode()
        : eType(ETERNITYNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyEternitynode(),
          keyEternitynode(),
          vin(),
          service(),
          nState(ACTIVE_ETERNITYNODE_INITIAL)
    {}

    /// Manage state of active Eternitynode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif
