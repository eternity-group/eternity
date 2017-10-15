// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeeternitynode.h"
#include "eternitynode.h"
#include "eternitynode-sync.h"
#include "eternitynodeman.h"
#include "protocol.h"

extern CWallet* pwalletMain;

// Keep track of the active Eternitynode
CActiveEternitynode activeEternitynode;

void CActiveEternitynode::ManageState()
{
    LogPrint("eternitynode", "CActiveEternitynode::ManageState -- Start\n");
    if(!fEternityNode) {
        LogPrint("eternitynode", "CActiveEternitynode::ManageState -- Not a eternitynode, returning\n");
        return;
    }

    if(Params().NetworkIDString() != CBaseChainParams::REGTEST && !eternitynodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveEternitynode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if(nState == ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_ETERNITYNODE_INITIAL;
    }

    LogPrint("eternitynode", "CActiveEternitynode::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if(eType == ETERNITYNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if(eType == ETERNITYNODE_REMOTE) {
        ManageStateRemote();
    } else if(eType == ETERNITYNODE_LOCAL) {
        // Try Remote Start first so the started local eternitynode can be restarted without recreate eternitynode broadcast.
        ManageStateRemote();
        if(nState != ACTIVE_ETERNITYNODE_STARTED)
            ManageStateLocal();
    }

    SendEternitynodePing();
}

std::string CActiveEternitynode::GetStateString() const
{
    switch (nState) {
        case ACTIVE_ETERNITYNODE_INITIAL:         return "INITIAL";
        case ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS: return "SYNC_IN_PROCESS";
        case ACTIVE_ETERNITYNODE_INPUT_TOO_NEW:   return "INPUT_TOO_NEW";
        case ACTIVE_ETERNITYNODE_NOT_CAPABLE:     return "NOT_CAPABLE";
        case ACTIVE_ETERNITYNODE_STARTED:         return "STARTED";
        default:                                return "UNKNOWN";
    }
}

std::string CActiveEternitynode::GetStatus() const
{
    switch (nState) {
        case ACTIVE_ETERNITYNODE_INITIAL:         return "Node just started, not yet activated";
        case ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Eternitynode";
        case ACTIVE_ETERNITYNODE_INPUT_TOO_NEW:   return strprintf("Eternitynode input must have at least %d confirmations", Params().GetConsensus().nEternitynodeMinimumConfirmations);
        case ACTIVE_ETERNITYNODE_NOT_CAPABLE:     return "Not capable eternitynode: " + strNotCapableReason;
        case ACTIVE_ETERNITYNODE_STARTED:         return "Eternitynode successfully started";
        default:                                return "Unknown";
    }
}

std::string CActiveEternitynode::GetTypeString() const
{
    std::string strType;
    switch(eType) {
    case ETERNITYNODE_UNKNOWN:
        strType = "UNKNOWN";
        break;
    case ETERNITYNODE_REMOTE:
        strType = "REMOTE";
        break;
    case ETERNITYNODE_LOCAL:
        strType = "LOCAL";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveEternitynode::SendEternitynodePing()
{
    if(!fPingerEnabled) {
        LogPrint("eternitynode", "CActiveEternitynode::SendEternitynodePing -- %s: eternitynode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!mnodeman.Has(vin)) {
        strNotCapableReason = "Eternitynode not in eternitynode list";
        nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        LogPrintf("CActiveEternitynode::SendEternitynodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CEternitynodePing mnp(vin);
    if(!mnp.Sign(keyEternitynode, pubKeyEternitynode)) {
        LogPrintf("CActiveEternitynode::SendEternitynodePing -- ERROR: Couldn't sign Eternitynode Ping\n");
        return false;
    }

    // Update lastPing for our eternitynode in Eternitynode list
    if(mnodeman.IsEternitynodePingedWithin(vin, ETERNITYNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveEternitynode::SendEternitynodePing -- Too early to send Eternitynode Ping\n");
        return false;
    }

    mnodeman.SetEternitynodeLastPing(vin, mnp);

    LogPrintf("CActiveEternitynode::SendEternitynodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveEternitynode::ManageStateInitial()
{
    LogPrint("eternitynode", "CActiveEternitynode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        strNotCapableReason = "Eternitynode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CEternitynode::IsValidNetAddr(service);
        if(!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CEternitynode::IsValidNetAddr(service);
                    if(fFoundLocal) break;
                }
            }
        }
    }

    if(!fFoundLocal) {
        nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(), mainnetDefaultPort);
            LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if(service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(), mainnetDefaultPort);
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveEternitynode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

    if(!ConnectNode((CAddress)service, NULL, true)) {
        nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = ETERNITYNODE_REMOTE;

    // Check if wallet funds are available
    if(!pwalletMain) {
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if(pwalletMain->IsLocked()) {
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if(pwalletMain->GetBalance() < 1000*COIN) {
        LogPrintf("CActiveEternitynode::ManageStateInitial -- %s: Wallet balance is < 1000 ENT\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode
    if(pwalletMain->GetEternitynodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = ETERNITYNODE_LOCAL;
    }

    LogPrint("eternitynode", "CActiveEternitynode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveEternitynode::ManageStateRemote()
{
    LogPrint("eternitynode", "CActiveEternitynode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyEternitynode.GetID() = %s\n", 
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyEternitynode.GetID().ToString());

    mnodeman.CheckEternitynode(pubKeyEternitynode);
    eternitynode_info_t infoMn = mnodeman.GetEternitynodeInfo(pubKeyEternitynode);
    if(infoMn.fInfoValid) {
        if(infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveEternitynode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(service != infoMn.addr) {
            nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this eternitynode changed recently.";
            LogPrintf("CActiveEternitynode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!CEternitynode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Eternitynode in %s state", CEternitynode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveEternitynode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(nState != ACTIVE_ETERNITYNODE_STARTED) {
            LogPrintf("CActiveEternitynode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_ETERNITYNODE_STARTED;
        }
    }
    else {
        nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        strNotCapableReason = "Eternitynode not in eternitynode list";
        LogPrintf("CActiveEternitynode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveEternitynode::ManageStateLocal()
{
    LogPrint("eternitynode", "CActiveEternitynode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
    if(nState == ACTIVE_ETERNITYNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if(pwalletMain->GetEternitynodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if(nInputAge < Params().GetConsensus().nEternitynodeMinimumConfirmations){
            nState = ACTIVE_ETERNITYNODE_INPUT_TOO_NEW;
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveEternitynode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CEternitynodeBroadcast mnb;
        std::string strError;
        if(!CEternitynodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyEternitynode, pubKeyEternitynode, strError, mnb)) {
            nState = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveEternitynode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_ETERNITYNODE_STARTED;

        //update to eternitynode list
        LogPrintf("CActiveEternitynode::ManageStateLocal -- Update Eternitynode List\n");
        mnodeman.UpdateEternitynodeList(mnb);
        mnodeman.NotifyEternitynodeUpdates();

        //send to all peers
        LogPrintf("CActiveEternitynode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.Relay();
    }
}
