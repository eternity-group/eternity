
#include "addrman.h"
#include "protocol.h"
#include "activeeternitynode.h"
#include "eternitynodeman.h"
#include "eternitynode.h"
#include "eternitynodeconfig.h"
#include "spork.h"

//
// Bootup the Eternitynode, look for a 1000DRK input and register on the network
//
void CActiveEternitynode::ManageStatus()
{    
    std::string errorMessage;

    if(!fEternityNode) return;

    if (fDebug) LogPrintf("CActiveEternitynode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if(Params().NetworkID() != CBaseChainParams::REGTEST && !eternitynodeSync.IsBlockchainSynced()) {
        status = ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveEternitynode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if(status == ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS) status = ACTIVE_ETERNITYNODE_INITIAL;

    if(status == ACTIVE_ETERNITYNODE_INITIAL) {
        CEternitynode *pen;
        pen = enodeman.Find(pubKeyEternitynode);
        if(pen != NULL) {
            pen->Check();
            if(pen->IsEnabled() && pen->protocolVersion == PROTOCOL_VERSION) EnableHotColdEternityNode(pen->vin, pen->addr);
        }
    }

    if(status != ACTIVE_ETERNITYNODE_STARTED) {

        // Set defaults
        status = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        notCapableReason = "";

        if(pwalletMain->IsLocked()){
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveEternitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if(pwalletMain->GetBalance() == 0){
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveEternitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if(strEternityNodeAddr.empty()) {
            if(!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the eternitynodeaddr configuration option.";
                LogPrintf("CActiveEternitynode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strEternityNodeAddr);
        }

        if(Params().NetworkID() == CBaseChainParams::MAIN) {
            if(service.GetPort() != 4855) {
                notCapableReason = strprintf("Invalid port: %u - only 4855 is supported on mainnet.", service.GetPort());
                LogPrintf("CActiveEternitynode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if(service.GetPort() == 4855) {
            notCapableReason = strprintf("Invalid port: %u - 4855 is only supported on mainnet.", service.GetPort());
            LogPrintf("CActiveEternitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveEternitynode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode *pnode = ConnectNode((CAddress)service, NULL, false);
        if(!pnode){
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveEternitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if(GetEternityNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {

            if(GetInputAge(vin) < ETERNITYNODE_MIN_CONFIRMATIONS){
                status = ACTIVE_ETERNITYNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveEternitynode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyEternitynode;
            CKey keyEternitynode;

            if(!spySendSigner.SetKey(strEternityNodePrivKey, errorMessage, keyEternitynode, pubKeyEternitynode))
            {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CEternitynodeBroadcast enb;
            if(!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyEternitynode, pubKeyEternitynode, errorMessage, enb)) {
                notCapableReason = "Error on CreateBroadcast: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveEternitynode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            enb.Relay();

            LogPrintf("CActiveEternitynode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_ETERNITYNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveEternitynode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if(!SendEternitynodePing(errorMessage)) {
        LogPrintf("CActiveEternitynode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveEternitynode::GetStatus() {
    switch (status) {
    case ACTIVE_ETERNITYNODE_INITIAL: return "Node just started, not yet activated";
    case ACTIVE_ETERNITYNODE_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Eternitynode";
    case ACTIVE_ETERNITYNODE_INPUT_TOO_NEW: return strprintf("Eternitynode input must have at least %d confirmations", ETERNITYNODE_MIN_CONFIRMATIONS);
    case ACTIVE_ETERNITYNODE_NOT_CAPABLE: return "Not capable eternitynode: " + notCapableReason;
    case ACTIVE_ETERNITYNODE_STARTED: return "Eternitynode successfully started";
    default: return "unknown";
    }
}

bool CActiveEternitynode::SendEternitynodePing(std::string& errorMessage) {
    if(status != ACTIVE_ETERNITYNODE_STARTED) {
        errorMessage = "Eternitynode is not in a running status";
        return false;
    }

    CPubKey pubKeyEternitynode;
    CKey keyEternitynode;

    if(!spySendSigner.SetKey(strEternityNodePrivKey, errorMessage, keyEternitynode, pubKeyEternitynode))
    {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveEternitynode::SendEternitynodePing() - Relay Eternitynode Ping vin = %s\n", vin.ToString());
    
    CEternitynodePing mnp(vin);
    if(!mnp.Sign(keyEternitynode, pubKeyEternitynode))
    {
        errorMessage = "Couldn't sign Eternitynode Ping";
        return false;
    }

    // Update lastPing for our eternitynode in Eternitynode list
    CEternitynode* pen = enodeman.Find(vin);
    if(pen != NULL)
    {
        if(pen->IsPingedWithin(ETERNITYNODE_PING_SECONDS, mnp.sigTime)){
            errorMessage = "Too early to send Eternitynode Ping";
            return false;
        }

        pen->lastPing = mnp;
        enodeman.mapSeenEternitynodePing.insert(make_pair(mnp.GetHash(), mnp));

        //enodeman.mapSeenEternitynodeBroadcast.lastPing is probably outdated, so we'll update it
        CEternitynodeBroadcast enb(*pen);
        uint256 hash = enb.GetHash();
        if(enodeman.mapSeenEternitynodeBroadcast.count(hash)) enodeman.mapSeenEternitynodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    }
    else
    {
        // Seems like we are trying to send a ping while the Eternitynode is not registered in the network
        errorMessage = "Spysend Eternitynode List doesn't include our Eternitynode, shutting down Eternitynode pinging service! " + vin.ToString();
        status = ACTIVE_ETERNITYNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }

}

bool CActiveEternitynode::CreateBroadcast(std::string strService, std::string strKeyEternitynode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CEternitynodeBroadcast &enb, bool fOffline) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyEternitynode;
    CKey keyEternitynode;

    //need correct blocks to send ping
    if(!fOffline && !eternitynodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Eternitynode";
        LogPrintf("CActiveEternitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if(!spySendSigner.SetKey(strKeyEternitynode, errorMessage, keyEternitynode, pubKeyEternitynode))
    {
        errorMessage = strprintf("Can't find keys for eternitynode %s - %s", strService, errorMessage);
        LogPrintf("CActiveEternitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if(!GetEternityNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for eternitynode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveEternitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);
    if(Params().NetworkID() == CBaseChainParams::MAIN) {
        if(service.GetPort() != 4855) {
            errorMessage = strprintf("Invalid port %u for eternitynode %s - only 4855 is supported on mainnet.", service.GetPort(), strService);
            LogPrintf("CActiveEternitynode::CreateBroadcast() - %s\n", errorMessage);
            return false;
        }
    } else if(service.GetPort() == 4855) {
        errorMessage = strprintf("Invalid port %u for eternitynode %s - 4855 is only supported on mainnet.", service.GetPort(), strService);
        LogPrintf("CActiveEternitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2*60*60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyEternitynode, pubKeyEternitynode, errorMessage, enb);
}

bool CActiveEternitynode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyEternitynode, CPubKey pubKeyEternitynode, std::string &errorMessage, CEternitynodeBroadcast &enb) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CEternitynodePing mnp(vin);
    if(!mnp.Sign(keyEternitynode, pubKeyEternitynode)){
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveEternitynode::CreateBroadcast() -  %s\n", errorMessage);
        enb = CEternitynodeBroadcast();
        return false;
    }

    enb = CEternitynodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyEternitynode, PROTOCOL_VERSION);
    enb.lastPing = mnp;
    if(!enb.Sign(keyCollateralAddress)){
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveEternitynode::CreateBroadcast() - %s\n", errorMessage);
        enb = CEternitynodeBroadcast();
        return false;
    }

    return true;
}

bool CActiveEternitynode::GetEternityNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    return GetEternityNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveEternitynode::GetEternityNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if(!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsEternitynode();
    COutput *selectedOutput;

    // Find the vin
    if(!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex = atoi(strOutputIndex.c_str());
        bool found = false;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            if(out.tx->GetHash() == txHash && out.i == outputIndex)
            {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if(!found) {
            LogPrintf("CActiveEternitynode::GetEternityNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if(possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveEternitynode::GetEternityNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Eternitynode vin information from output
bool CActiveEternitynode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(),out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveEternitynode::GetEternityNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf ("CActiveEternitynode::GetEternityNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Eternitynode
vector<COutput> CActiveEternitynode::SelectCoinsEternitynode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from eternitynode.conf
    if(GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, atoi(mne.getOutputIndex().c_str()));
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from eternitynode.conf back if they where temporary unlocked
    if(!confLockedCoins.empty()) {
        BOOST_FOREACH(COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].nValue == 1000*COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Eternitynode, this can enable to run as a hot wallet with no funds
bool CActiveEternitynode::EnableHotColdEternityNode(CTxIn& newVin, CService& newService)
{
    if(!fEternityNode) return false;

    status = ACTIVE_ETERNITYNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveEternitynode::EnableHotColdEternityNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
