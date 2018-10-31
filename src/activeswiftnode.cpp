// Copyright (c) 2014-2016 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeswiftnode.h"
#include "addrman.h"
#include "swiftnode.h"
#include "swiftnodeconfig.h"
#include "swiftnodeman.h"
#include "swiftnode-helpers.h"
#include "protocol.h"
#include "spork.h"

// Keep track of the active Swiftnode
CActiveSwiftnode activeSwiftnode;

//
// Bootup the Swiftnode, look for a SWIFT collateral input and register on the network
//
void CActiveSwiftnode::ManageStatus()
{
    std::string errorMessage;

    if (!fSwiftNode) return;

    if (fDebug) LogPrintf("CActiveSwiftnode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !swiftnodeSync.IsBlockchainSynced()) {
        status = ACTIVE_SWIFTNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveSwiftnode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_SWIFTNODE_SYNC_IN_PROCESS) status = ACTIVE_SWIFTNODE_INITIAL;

    if (status == ACTIVE_SWIFTNODE_INITIAL) {
        CSwiftnode* pmn;
        pmn = mnodeman.Find(pubKeySwiftnode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdSwiftNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_SWIFTNODE_STARTED) {
        // Set defaults
        status = ACTIVE_SWIFTNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveSwiftnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveSwiftnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strSwiftNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the swiftnodeaddr configuration option.";
                LogPrintf("CActiveSwiftnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strSwiftNodeAddr);
        }

        // The service needs the correct default port to work properly
        if(!CSwiftnodeBroadcast::CheckDefaultPort(strSwiftNodeAddr, errorMessage, "CActiveSwiftnode::ManageStatus()"))
            return;

        LogPrintf("CActiveSwiftnode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveSwiftnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetSwiftNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < SWIFTNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_SWIFTNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveSwiftnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeySwiftnode;
            CKey keySwiftnode;

            if (!swiftnodeSigner.SetKey(strSwiftNodePrivKey, errorMessage, keySwiftnode, pubKeySwiftnode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CSwiftnodeBroadcast mnb;
            if (!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keySwiftnode, pubKeySwiftnode, errorMessage, mnb)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("CActiveSwiftnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveSwiftnode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            LogPrintf("CActiveSwiftnode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_SWIFTNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveSwiftnode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendSwiftnodePing(errorMessage)) {
        LogPrintf("CActiveSwiftnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveSwiftnode::GetStatus()
{
    switch (status) {
    case ACTIVE_SWIFTNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_SWIFTNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Swiftnode";
    case ACTIVE_SWIFTNODE_INPUT_TOO_NEW:
        return strprintf("Swiftnode input must have at least %d confirmations", SWIFTNODE_MIN_CONFIRMATIONS);
    case ACTIVE_SWIFTNODE_NOT_CAPABLE:
        return "Not capable swiftnode: " + notCapableReason;
    case ACTIVE_SWIFTNODE_STARTED:
        return "Swiftnode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveSwiftnode::SendSwiftnodePing(std::string& errorMessage)
{
    if (status != ACTIVE_SWIFTNODE_STARTED) {
        errorMessage = "Swiftnode is not in a running status";
        return false;
    }

    CPubKey pubKeySwiftnode;
    CKey keySwiftnode;

    if (!swiftnodeSigner.SetKey(strSwiftNodePrivKey, errorMessage, keySwiftnode, pubKeySwiftnode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveSwiftnode::SendSwiftnodePing() - Relay Swiftnode Ping vin = %s\n", vin.ToString());

    CSwiftnodePing mnp(vin);
    if (!mnp.Sign(keySwiftnode, pubKeySwiftnode)) {
        errorMessage = "Couldn't sign Swiftnode Ping";
        return false;
    }

    // Update lastPing for our swiftnode in Swiftnode list
    CSwiftnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(SWIFTNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Swiftnode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenSwiftnodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenSwiftnodeBroadcast.lastPing is probably outdated, so we'll update it
        CSwiftnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenSwiftnodeBroadcast.count(hash)) mnodeman.mapSeenSwiftnodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    } else {
        // Seems like we are trying to send a ping while the Swiftnode is not registered in the network
        errorMessage = "Swiftnode List doesn't include our Swiftnode, shutting down Swiftnode pinging service! " + vin.ToString();
        status = ACTIVE_SWIFTNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveSwiftnode::CreateBroadcast(std::string strService, std::string strKeySwiftnode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CSwiftnodeBroadcast &mnb, bool fOffline)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeySwiftnode;
    CKey keySwiftnode;

    //need correct blocks to send ping
    if (!fOffline && !swiftnodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Swiftnode";
        LogPrintf("CActiveSwiftnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!swiftnodeSigner.SetKey(strKeySwiftnode, errorMessage, keySwiftnode, pubKeySwiftnode)) {
        errorMessage = strprintf("Can't find keys for swiftnode %s - %s", strService, errorMessage);
        LogPrintf("CActiveSwiftnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!GetSwiftNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for swiftnode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveSwiftnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);

    // The service needs the correct default port to work properly
    if(!CSwiftnodeBroadcast::CheckDefaultPort(strService, errorMessage, "CActiveSwiftnode::CreateBroadcast()"))
        return false;

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keySwiftnode, pubKeySwiftnode, errorMessage, mnb);
}

bool CActiveSwiftnode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keySwiftnode, CPubKey pubKeySwiftnode, std::string& errorMessage, CSwiftnodeBroadcast &mnb)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CSwiftnodePing mnp(vin);
    if (!mnp.Sign(keySwiftnode, pubKeySwiftnode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveSwiftnode::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CSwiftnodeBroadcast();
        return false;
    }

    mnb = CSwiftnodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeySwiftnode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveSwiftnode::CreateBroadcast() - %s\n", errorMessage);
        mnb = CSwiftnodeBroadcast();
        return false;
    }

    return true;
}

bool CActiveSwiftnode::GetSwiftNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetSwiftNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveSwiftnode::GetSwiftNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsSwiftnode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveSwiftnode::GetSwiftNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveSwiftnode::GetSwiftNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

// Extract Swiftnode vin information from output
bool CActiveSwiftnode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveSwiftnode::GetSwiftNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf("CActiveSwiftnode::GetSwiftNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Swiftnode
vector<COutput> CActiveSwiftnode::SelectCoinsSwiftnode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from swiftnode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH (CSwiftnodeConfig::CSwiftnodeEntry mne, swiftnodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if (!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from swiftnode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    for (const COutput& out : vCoins) {
        if (out.tx->vout[out.i].nValue == SWIFTNODE_COLLATERAL * COIN)
            filteredCoins.push_back(out);
    }
    return filteredCoins;
}

// when starting a Swiftnode, this can enable to run as a hot wallet with no funds
bool CActiveSwiftnode::EnableHotColdSwiftNode(CTxIn& newVin, CService& newService)
{
    if (!fSwiftNode) return false;

    status = ACTIVE_SWIFTNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveSwiftnode::EnableHotColdSwiftNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
