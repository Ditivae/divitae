// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The DIVIT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEPROSPERITYNODE_H
#define ACTIVEPROSPERITYNODE_H

#include "init.h"
#include "key.h"
#include "prosperitynode.h"
#include "net.h"
#include "obfuscation.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_PROSPERITYNODE_INITIAL 0 // initial state
#define ACTIVE_PROSPERITYNODE_SYNC_IN_PROCESS 1
#define ACTIVE_PROSPERITYNODE_INPUT_TOO_NEW 2
#define ACTIVE_PROSPERITYNODE_NOT_CAPABLE 3
#define ACTIVE_PROSPERITYNODE_STARTED 4

// Responsible for activating the Prosperitynode and pinging the network
class CActiveProsperitynode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Prosperitynode
    bool SendProsperitynodePing(std::string& errorMessage);

    /// Create Prosperitynode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyProsperitynode, CPubKey pubKeyProsperitynode, std::string& errorMessage, CProsperitynodeBroadcast &mnb);

    /// Get 10000 DIVIT input that can be used for the Prosperitynode
    bool GetFundamentalNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Prosperitynode
    CPubKey pubKeyProsperitynode;

    // Initialized while registering Prosperitynode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveProsperitynode()
    {
        status = ACTIVE_PROSPERITYNODE_INITIAL;
    }

    /// Manage status of main Prosperitynode
    void ManageStatus();
    std::string GetStatus();

    /// Create Prosperitynode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CProsperitynodeBroadcast &mnb, bool fOffline = false);

    /// Get 10000 DIVIT input that can be used for the Prosperitynode
    bool GetFundamentalNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsProsperitynode();

    /// Enable cold wallet mode (run a Prosperitynode with no funds)
    bool EnableHotColdFundamentalNode(CTxIn& vin, CService& addr);
};

#endif
