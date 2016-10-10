// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "db.h"
#include "init.h"
#include "activeeternitynode.h"
#include "eternitynodeman.h"
#include "eternitynode-payments.h"
#include "eternitynode-evolution.h"
#include "eternitynodeconfig.h"
#include "rpcserver.h"
#include "utilmoneystr.h"

#include <fstream>
using namespace json_spirit;

void SendMoney(const CTxDestination &address, CAmount nValue, CWalletTx& wtxNew, AvailableCoinsType coin_type=ALL_COINS)
{
    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > pwalletMain->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    string strError;
    if (pwalletMain->IsLocked())
    {
        strError = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SendMoney() : %s", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Parse Eternity address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    if (!pwalletMain->CreateTransaction(scriptPubKey, nValue, wtxNew, reservekey, nFeeRequired, strError, NULL, coin_type))
    {
        if (nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("SendMoney() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
}

Value spysend(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error(
            "spysend <eternityaddress> <amount>\n"
            "eternityaddress, reset, or auto (AutoDenominate)"
            "<amount> is a real and will be rounded to the next 0.1"
            + HelpRequiringPassphrase());

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    if(params[0].get_str() == "auto"){
        if(fEternityNode)
            return "SpySend is not supported from eternitynodes";

        return "DoAutomaticDenominating " + (spySendPool.DoAutomaticDenominating() ? "successful" : ("failed: " + spySendPool.GetStatus()));
    }

    if(params[0].get_str() == "reset"){
        spySendPool.Reset();
        return "successfully reset spysend";
    }

    if (params.size() != 2)
        throw runtime_error(
            "spysend <eternityaddress> <amount>\n"
            "eternityaddress, denominate, or auto (AutoDenominate)"
            "<amount> is a real and will be rounded to the next 0.1"
            + HelpRequiringPassphrase());

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Eternity address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
//    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nAmount, wtx, ONLY_DENOMINATED);
    SendMoney(address.Get(), nAmount, wtx, ONLY_DENOMINATED);
//    if (strError != "")
//        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    return wtx.GetHash().GetHex();
}


Value getpoolinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpoolinfo\n"
            "Returns an object containing anonymous pool-related information.");

    Object obj;
    obj.push_back(Pair("current_eternitynode",        enodeman.GetCurrentEternityNode()->addr.ToString()));
    obj.push_back(Pair("state",        spySendPool.GetState()));
    obj.push_back(Pair("entries",      spySendPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted",      spySendPool.GetCountEntriesAccepted()));
    return obj;
}


Value eternitynode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "start-all" && strCommand != "start-missing" &&
         strCommand != "start-disabled" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count"  && strCommand != "enforce" &&
        strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" &&
        strCommand != "outputs" && strCommand != "status" && strCommand != "calcscore"))
        throw runtime_error(
                "eternitynode \"command\"... ( \"passphrase\" )\n"
                "Set of commands to execute eternitynode related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "2. \"passphrase\"     (string, optional) The wallet passphrase\n"
                "\nAvailable commands:\n"
                "  count        - Print number of all known eternitynodes (optional: 'ds', 'enabled', 'all', 'qualify')\n"
                "  current      - Print info on current eternitynode winner\n"
                "  debug        - Print eternitynode status\n"
                "  genkey       - Generate new eternitynodeprivkey\n"
                "  enforce      - Enforce eternitynode payments\n"
                "  outputs      - Print eternitynode compatible outputs\n"
                "  start        - Start eternitynode configured in eternity.conf\n"
                "  start-alias  - Start single eternitynode by assigned alias configured in eternitynode.conf\n"
                "  start-<mode> - Start eternitynodes configured in eternitynode.conf (<mode>: 'all', 'missing', 'disabled')\n"
                "  status       - Print eternitynode status information\n"
                "  list         - Print list of all known eternitynodes (see eternitynodelist for more info)\n"
                "  list-conf    - Print eternitynode.conf in JSON format\n"
                "  winners      - Print list of eternitynode winners\n"
                );

    if (strCommand == "list")
    {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return eternitynodelist(newParams, fHelp);
    }

    if (strCommand == "evolution")
    {
        return "Show evolutions";
    }

    if(strCommand == "connect")
    {
        std::string strAddress = "";
        if (params.size() == 2){
            strAddress = params[1].get_str();
        } else {
            throw runtime_error("Eternitynode address required\n");
        }

        CService addr = CService(strAddress);

        CNode *pnode = ConnectNode((CAddress)addr, NULL, false);
        if(pnode){
            pnode->Release();
            return "successfully connected";
        } else {
            throw runtime_error("error connecting\n");
        }
    }

    if (strCommand == "count")
    {
        if (params.size() > 2){
            throw runtime_error("too many parameters\n");
        }
        if (params.size() == 2)
        {
            int nCount = 0;

            if(chainActive.Tip())
                enodeman.GetNextEternitynodeInQueueForPayment(chainActive.Tip()->nHeight, true, nCount);

            if(params[1] == "ds") return enodeman.CountEnabled(MIN_POOL_PEER_PROTO_VERSION);
            if(params[1] == "enabled") return enodeman.CountEnabled();
            if(params[1] == "qualify") return nCount;
            if(params[1] == "all") return strprintf("Total: %d (SS Compatible: %d / Enabled: %d / Qualify: %d)",
                                                    enodeman.size(),
                                                    enodeman.CountEnabled(MIN_POOL_PEER_PROTO_VERSION),
                                                    enodeman.CountEnabled(),
                                                    nCount);
        }
        return enodeman.size();
    }

    if (strCommand == "current")
    {
        CEternitynode* winner = enodeman.GetCurrentEternityNode(1);
        if(winner) {
            Object obj;

            obj.push_back(Pair("IP:port",       winner->addr.ToString()));
            obj.push_back(Pair("protocol",      (int64_t)winner->protocolVersion));
            obj.push_back(Pair("vin",           winner->vin.prevout.hash.ToString()));
            obj.push_back(Pair("pubkey",        CBitcoinAddress(winner->pubkey.GetID()).ToString()));
            obj.push_back(Pair("lastseen",      (winner->lastPing == CEternitynodePing()) ? winner->sigTime :
                                                        (int64_t)winner->lastPing.sigTime));
            obj.push_back(Pair("activeseconds", (winner->lastPing == CEternitynodePing()) ? 0 :
                                                        (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
            return obj;
        }

        return "unknown";
    }

    if (strCommand == "debug")
    {
        if(activeEternitynode.status != ACTIVE_ETERNITYNODE_INITIAL || !eternitynodeSync.IsSynced())
            return activeEternitynode.GetStatus();

        CTxIn vin = CTxIn();
        CPubKey pubkey = CScript();
        CKey key;
        bool found = activeEternitynode.GetEternityNodeVin(vin, pubkey, key);
        if(!found){
            throw runtime_error("Missing eternitynode input, please look at the documentation for instructions on eternitynode creation\n");
        } else {
            return activeEternitynode.GetStatus();
        }
    }

    if(strCommand == "enforce")
    {
        return (uint64_t)enforceEternitynodePaymentsTime;
    }

    if (strCommand == "start")
    {
        if(!fEternityNode) throw runtime_error("you must set eternitynode=1 in the configuration\n");

        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2){
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw runtime_error("Your wallet is locked, passphrase is required\n");
            }

            if(!pwalletMain->Unlock(strWalletPass)){
                throw runtime_error("incorrect passphrase\n");
            }
        }

        if(activeEternitynode.status != ACTIVE_ETERNITYNODE_STARTED){
            activeEternitynode.status = ACTIVE_ETERNITYNODE_INITIAL; // TODO: consider better way
            activeEternitynode.ManageStatus();
            pwalletMain->Lock();
        }

        return activeEternitynode.GetStatus();
    }

    if (strCommand == "start-alias")
    {
        if (params.size() < 2){
            throw runtime_error("command needs at least 2 parameters\n");
        }

        std::string alias = params[1].get_str();

        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 3){
                strWalletPass = params[2].get_str().c_str();
            } else {
                throw runtime_error("Your wallet is locked, passphrase is required\n");
            }

            if(!pwalletMain->Unlock(strWalletPass)){
                throw runtime_error("incorrect passphrase\n");
            }
        }

        bool found = false;

        Object statusObj;
        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
            if(mne.getAlias() == alias) {
                found = true;
                std::string errorMessage;
                CEternitynodeBroadcast enb;

                bool result = activeEternitynode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, enb);

                statusObj.push_back(Pair("result", result ? "successful" : "failed"));
                if(result) {
                    enodeman.UpdateEternitynodeList(enb);
                    enb.Relay();
                } else {
                    statusObj.push_back(Pair("errorMessage", errorMessage));
                }
                break;
            }
        }

        if(!found) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "could not find alias in config. Verify with list-conf."));
        }

        pwalletMain->Lock();
        return statusObj;

    }

    if (strCommand == "start-many" || strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled")
    {
        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2){
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw runtime_error("Your wallet is locked, passphrase is required\n");
            }

            if(!pwalletMain->Unlock(strWalletPass)){
                throw runtime_error("incorrect passphrase\n");
            }
        }

        if((strCommand == "start-missing" || strCommand == "start-disabled") &&
         (eternitynodeSync.RequestedEternitynodeAssets <= ETERNITYNODE_SYNC_LIST ||
          eternitynodeSync.RequestedEternitynodeAssets == ETERNITYNODE_SYNC_FAILED)) {
            throw runtime_error("You can't use this command until eternitynode list is synced\n");
        }

        std::vector<CEternitynodeConfig::CEternitynodeEntry> mnEntries;
        mnEntries = eternitynodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        Object resultsObj;

        BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
            std::string errorMessage;

            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CEternitynode *pen = enodeman.Find(vin);
            CEternitynodeBroadcast enb;

            if(strCommand == "start-missing" && pen) continue;
            if(strCommand == "start-disabled" && pen && pen->IsEnabled()) continue;

            bool result = activeEternitynode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, enb);

            Object statusObj;
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", result ? "successful" : "failed"));

            if(result) {
                successful++;
                enodeman.UpdateEternitynodeList(enb);
                enb.Relay();
            } else {
                failed++;
                statusObj.push_back(Pair("errorMessage", errorMessage));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
        pwalletMain->Lock();

        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d eternitynodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "create")
    {

        throw runtime_error("Not implemented yet, please look at the documentation for instructions on eternitynode creation\n");
    }

    if (strCommand == "genkey")
    {
        CKey secret;
        secret.MakeNewKey(false);

        return CBitcoinSecret(secret).ToString();
    }

    if(strCommand == "list-conf")
    {
        std::vector<CEternitynodeConfig::CEternitynodeEntry> mnEntries;
        mnEntries = eternitynodeConfig.getEntries();

        Object resultObj;

        BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CEternitynode *pen = enodeman.Find(vin);

            std::string strStatus = pen ? pen->Status() : "MISSING";

            Object mnObj;
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
            mnObj.push_back(Pair("txHash", mne.getTxHash()));
            mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("eternitynode", mnObj));
        }

        return resultObj;
    }

    if (strCommand == "outputs"){
        // Find possible candidates
        vector<COutput> possibleCoins = activeEternitynode.SelectCoinsEternitynode();

        Object obj;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
        }

        return obj;

    }

    if(strCommand == "status")
    {
        if(!fEternityNode) throw runtime_error("This is not a eternitynode\n");

        Object mnObj;
        CEternitynode *pen = enodeman.Find(activeEternitynode.vin);

        mnObj.push_back(Pair("vin", activeEternitynode.vin.ToString()));
        mnObj.push_back(Pair("service", activeEternitynode.service.ToString()));
        if (pen) mnObj.push_back(Pair("pubkey", CBitcoinAddress(pen->pubkey.GetID()).ToString()));
        mnObj.push_back(Pair("status", activeEternitynode.GetStatus()));
        return mnObj;
    }

    if (strCommand == "winners")
    {
        int nLast = 10;

        if (params.size() >= 2){
            nLast = atoi(params[1].get_str());
        }

        Object obj;

        for(int nHeight = chainActive.Tip()->nHeight-nLast; nHeight < chainActive.Tip()->nHeight+20; nHeight++)
        {
            obj.push_back(Pair(strprintf("%d", nHeight), GetRequiredPaymentsString(nHeight)));
        }

        return obj;
    }

    /*
        Shows which eternitynode wins by score each block
    */
    if (strCommand == "calcscore")
    {

        int nLast = 10;

        if (params.size() >= 2){
            nLast = atoi(params[1].get_str());
        }
        Object obj;

        std::vector<CEternitynode> vEternitynodes = enodeman.GetFullEternitynodeVector();
        for(int nHeight = chainActive.Tip()->nHeight-nLast; nHeight < chainActive.Tip()->nHeight+20; nHeight++){
            uint256 nHigh = 0;
            CEternitynode *pBestEternitynode = NULL;
            BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
                uint256 n = mn.CalculateScore(1, nHeight-100);
                if(n > nHigh){
                    nHigh = n;
                    pBestEternitynode = &mn;
                }
            }
            if(pBestEternitynode)
                obj.push_back(Pair(strprintf("%d", nHeight), pBestEternitynode->vin.prevout.ToStringShort().c_str()));
        }

        return obj;
    }

    return Value::null;
}

Value eternitynodelist(const Array& params, bool fHelp)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    if (fHelp ||
            (strMode != "status" && strMode != "vin" && strMode != "pubkey" && strMode != "lastseen" && strMode != "activeseconds" && strMode != "rank" && strMode != "addr"
                && strMode != "protocol" && strMode != "full" && strMode != "lastpaid"))
    {
        throw runtime_error(
                "eternitynodelist ( \"mode\" \"filter\" )\n"
                "Get a list of eternitynodes in different modes\n"
                "\nArguments:\n"
                "1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
                "2. \"filter\"    (string, optional) Filter results. Partial match by IP by default in all modes,\n"
                "                                    additional matches in some modes are also available\n"
                "\nAvailable modes:\n"
                "  activeseconds  - Print number of seconds eternitynode recognized by the network as enabled\n"
                "                   (since latest issued \"eternitynode start/start-many/start-alias\")\n"
                "  addr           - Print ip address associated with a eternitynode (can be additionally filtered, partial match)\n"
                "  full           - Print info in format 'status protocol pubkey IP lastseen activeseconds lastpaid'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  lastseen       - Print timestamp of when a eternitynode was last seen on the network\n"
                "  lastpaid       - The last time a node was paid on the network\n"
                "  protocol       - Print protocol of a eternitynode (can be additionally filtered, exact match))\n"
                "  pubkey         - Print public key associated with a eternitynode (can be additionally filtered,\n"
                "                   partial match)\n"
                "  rank           - Print rank of a eternitynode based on current block\n"
                "  status         - Print eternitynode status: ENABLED / EXPIRED / VIN_SPENT / REMOVE / POS_ERROR\n"
                "                   (can be additionally filtered, partial match)\n"
                );
    }

    Object obj;
    if (strMode == "rank") {
        std::vector<pair<int, CEternitynode> > vEternitynodeRanks = enodeman.GetEternitynodeRanks(chainActive.Tip()->nHeight);
        BOOST_FOREACH(PAIRTYPE(int, CEternitynode)& s, vEternitynodeRanks) {
            std::string strVin = s.second.vin.prevout.ToStringShort();
            if(strFilter !="" && strVin.find(strFilter) == string::npos) continue;
            obj.push_back(Pair(strVin,       s.first));
        }
    } else {
        std::vector<CEternitynode> vEternitynodes = enodeman.GetFullEternitynodeVector();
        BOOST_FOREACH(CEternitynode& mn, vEternitynodes) {
            std::string strVin = mn.vin.prevout.ToStringShort();
            if (strMode == "activeseconds") {
                if(strFilter !="" && strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,       (int64_t)(mn.lastPing.sigTime - mn.sigTime)));
            } else if (strMode == "addr") {
                if(strFilter !="" && mn.vin.prevout.hash.ToString().find(strFilter) == string::npos &&
                    strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,       mn.addr.ToString()));
            } else if (strMode == "full") {
                std::ostringstream addrStream;
                addrStream << setw(21) << strVin;

                std::ostringstream stringStream;
                stringStream << setw(9) <<
                               mn.Status() << " " <<
                               mn.protocolVersion << " " <<
                               CBitcoinAddress(mn.pubkey.GetID()).ToString() << " " << setw(21) <<
                               mn.addr.ToString() << " " <<
                               (int64_t)mn.lastPing.sigTime << " " << setw(8) <<
                               (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " <<
                               (int64_t)mn.GetLastPaid();
                std::string output = stringStream.str();
                stringStream << " " << strVin;
                if(strFilter !="" && stringStream.str().find(strFilter) == string::npos &&
                        strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(addrStream.str(), output));
            } else if (strMode == "lastseen") {
                if(strFilter !="" && strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,       (int64_t)mn.lastPing.sigTime));
            } else if (strMode == "lastpaid"){
                if(strFilter !="" && mn.vin.prevout.hash.ToString().find(strFilter) == string::npos &&
                    strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,      (int64_t)mn.GetLastPaid()));
            } else if (strMode == "protocol") {
                if(strFilter !="" && strFilter != strprintf("%d", mn.protocolVersion) &&
                    strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,       (int64_t)mn.protocolVersion));
            } else if (strMode == "pubkey") {
                CBitcoinAddress address(mn.pubkey.GetID());

                if(strFilter !="" && address.ToString().find(strFilter) == string::npos &&
                    strVin.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,       address.ToString()));
            } else if(strMode == "status") {
                std::string strStatus = mn.Status();
                if(strFilter !="" && strVin.find(strFilter) == string::npos && strStatus.find(strFilter) == string::npos) continue;
                obj.push_back(Pair(strVin,       strStatus));
            }
        }
    }
    return obj;

}

bool DecodeHexVecEnb(std::vector<CEternitynodeBroadcast>& vecEnb, std::string strHexEnb) {

    if (!IsHex(strHexEnb))
        return false;

    vector<unsigned char> enbData(ParseHex(strHexEnb));
    CDataStream ssData(enbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecEnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

Value eternitynodebroadcast(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "create-alias" && strCommand != "create-all" && strCommand != "decode" && strCommand != "relay"))
        throw runtime_error(
                "eternitynodebroadcast \"command\"... ( \"passphrase\" )\n"
                "Set of commands to create and relay eternitynode broadcast messages\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "2. \"passphrase\"     (string, optional) The wallet passphrase\n"
                "\nAvailable commands:\n"
                "  create-alias  - Create single remote eternitynode broadcast message by assigned alias configured in eternitynode.conf\n"
                "  create-all    - Create remote eternitynode broadcast messages for all eternitynodes configured in eternitynode.conf\n"
                "  decode        - Decode eternitynode broadcast message\n"
                "  relay         - Relay eternitynode broadcast message to the network\n"
                + HelpRequiringPassphrase());

    if (strCommand == "create-alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Command needs at least 2 parameters");

        std::string alias = params[1].get_str();

        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 3){
                strWalletPass = params[2].get_str().c_str();
            } else {
                throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Your wallet is locked, passphrase is required");
            }
 
            if(!pwalletMain->Unlock(strWalletPass)){
                throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "The wallet passphrase entered was incorrect");
            }
        }

        bool found = false;

        Object statusObj;
        std::vector<CEternitynodeBroadcast> vecEnb;

        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
            if(mne.getAlias() == alias) {
                found = true;
                std::string errorMessage;
                CEternitynodeBroadcast enb;

                bool result = activeEternitynode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, enb, true);

                statusObj.push_back(Pair("result", result ? "successful" : "failed"));
                if(result) {
                    vecEnb.push_back(enb);
                    CDataStream ssVecEnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecEnb << vecEnb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecEnb.begin(), ssVecEnb.end())));
                } else {
                    statusObj.push_back(Pair("errorMessage", errorMessage));
                }
                break;
            }
        }

        if(!found) {
            statusObj.push_back(Pair("result", "not found"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        pwalletMain->Lock();
        return statusObj;

    }

    if (strCommand == "create-all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2){
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Your wallet is locked, passphrase is required");
            }

            if(!pwalletMain->Unlock(strWalletPass)){
                throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "The wallet passphrase entered was incorrect");
            }
        }

        std::vector<CEternitynodeConfig::CEternitynodeEntry> mnEntries;
        mnEntries = eternitynodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        Object resultsObj;
        std::vector<CEternitynodeBroadcast> vecEnb;

        BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
            std::string errorMessage;

            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CEternitynodeBroadcast enb;

            bool result = activeEternitynode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, enb, true);

            Object statusObj;
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", result ? "successful" : "failed"));

            if(result) {
                successful++;
                vecEnb.push_back(enb);
            } else {
                failed++;
                statusObj.push_back(Pair("errorMessage", errorMessage));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
        pwalletMain->Lock();

        CDataStream ssVecEnb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecEnb << vecEnb;
        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d eternitynodes, failed to create %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));
        returnObj.push_back(Pair("hex", HexStr(ssVecEnb.begin(), ssVecEnb.end())));

        return returnObj;
    }

    if (strCommand == "decode")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'eternitynodebroadcast decode \"hexstring\"'");

        int successful = 0;
        int failed = 0;

        std::vector<CEternitynodeBroadcast> vecEnb;
        Object returnObj;

        if (!DecodeHexVecEnb(vecEnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Eternitynode broadcast message decode failed");

        BOOST_FOREACH(CEternitynodeBroadcast& enb, vecEnb) {
            Object resultObj;

            if(enb.VerifySignature()) {
                successful++;
                resultObj.push_back(Pair("vin", enb.vin.ToString()));
                resultObj.push_back(Pair("addr", enb.addr.ToString()));
                resultObj.push_back(Pair("pubkey", CBitcoinAddress(enb.pubkey.GetID()).ToString()));
                resultObj.push_back(Pair("pubkey2", CBitcoinAddress(enb.pubkey2.GetID()).ToString()));
                resultObj.push_back(Pair("vchSig", EncodeBase64(&enb.sig[0], enb.sig.size())));
                resultObj.push_back(Pair("sigTime", enb.sigTime));
                resultObj.push_back(Pair("protocolVersion", enb.protocolVersion));
                resultObj.push_back(Pair("nLastDsq", enb.nLastDsq));

                Object lastPingObj;
                lastPingObj.push_back(Pair("vin", enb.lastPing.vin.ToString()));
                lastPingObj.push_back(Pair("blockHash", enb.lastPing.blockHash.ToString()));
                lastPingObj.push_back(Pair("sigTime", enb.lastPing.sigTime));
                lastPingObj.push_back(Pair("vchSig", EncodeBase64(&enb.lastPing.vchSig[0], enb.lastPing.vchSig.size())));

                resultObj.push_back(Pair("lastPing", lastPingObj));
            } else {
                failed++;
                resultObj.push_back(Pair("errorMessage", "Eternitynode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(enb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully decoded broadcast messages for %d eternitynodes, failed to decode %d, total %d", successful, failed, successful + failed)));

        return returnObj;
    }

    if (strCommand == "relay")
    {
        if (params.size() < 2 || params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,   "eternitynodebroadcast relay \"hexstring\" ( fast )\n"
                                                        "\nArguments:\n"
                                                        "1. \"hex\"      (string, required) Broadcast messages hex string\n"
                                                        "2. fast       (string, optional) If none, using safe method\n");

        int successful = 0;
        int failed = 0;
        bool fSafe = params.size() == 2;

        std::vector<CEternitynodeBroadcast> vecEnb;
        Object returnObj;

        if (!DecodeHexVecEnb(vecEnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Eternitynode broadcast message decode failed");

        // verify all signatures first, bailout if any of them broken
        BOOST_FOREACH(CEternitynodeBroadcast& enb, vecEnb) {
            Object resultObj;

            resultObj.push_back(Pair("vin", enb.vin.ToString()));
            resultObj.push_back(Pair("addr", enb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (enb.VerifySignature()) {
                if (fSafe) {
                    fResult = enodeman.CheckEnbAndUpdateEternitynodeList(enb, nDos);
                } else {
                    enodeman.UpdateEternitynodeList(enb);
                    enb.Relay();
                    fResult = true;
                }
            } else fResult = false;

            if(fResult) {
                successful++;
                enodeman.UpdateEternitynodeList(enb);
                enb.Relay();
                resultObj.push_back(Pair(enb.GetHash().ToString(), "successful"));
            } else {
                failed++;
                resultObj.push_back(Pair("errorMessage", "Eternitynode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(enb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully relayed broadcast messages for %d eternitynodes, failed to relay %d, total %d", successful, failed, successful + failed)));

        return returnObj;
    }

    return Value::null;
}
