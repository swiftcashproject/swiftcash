// Copyright (c) 2014-2016 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVESWIFTNODE_H
#define ACTIVESWIFTNODE_H

#include "init.h"
#include "key.h"
#include "swiftnode.h"
#include "net.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_SWIFTNODE_INITIAL 0 // initial state
#define ACTIVE_SWIFTNODE_SYNC_IN_PROCESS 1
#define ACTIVE_SWIFTNODE_INPUT_TOO_NEW 2
#define ACTIVE_SWIFTNODE_NOT_CAPABLE 3
#define ACTIVE_SWIFTNODE_STARTED 4

// Responsible for activating the Swiftnode and pinging the network
class CActiveSwiftnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Swiftnode
    bool SendSwiftnodePing(std::string& errorMessage);

    /// Create Swiftnode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keySwiftnode, CPubKey pubKeySwiftnode, std::string& errorMessage, CSwiftnodeBroadcast &mnb);

    /// Get SWIFT collateral input that can be used for the Swiftnode
    bool GetSwiftNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Swiftnode
    CPubKey pubKeySwiftnode;

    // Initialized while registering Swiftnode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveSwiftnode()
    {
        status = ACTIVE_SWIFTNODE_INITIAL;
    }

    /// Manage status of main Swiftnode
    void ManageStatus();
    std::string GetStatus();

    /// Create Swiftnode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CSwiftnodeBroadcast &mnb, bool fOffline = false);

    /// Get SWIFT collateral input that can be used for the Swiftnode
    bool GetSwiftNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsSwiftnode();

    /// Enable cold wallet mode (run a Swiftnode with no funds)
    bool EnableHotColdSwiftNode(CTxIn& vin, CService& addr);
};

extern CActiveSwiftnode activeSwiftnode;

#endif
