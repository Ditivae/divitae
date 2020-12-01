// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The DIVIT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeprosperitynode.h"
#include "addrman.h"
#include "prosperitynode.h"
#include "prosperitynodeconfig.h"
#include "prosperitynodeman.h"
#include "protocol.h"
#include "spork.h"

//
// Bootup the Prosperitynode, look for a 10000 DIVIT input and register on the network
//
void CActiveProsperitynode::ManageStatus()
{
    std::string errorMessage;

    if (!fFundamentalNode) return;

    if (fDebug) LogPrintf("CActiveProsperitynode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !prosperitynodeSync.IsBlockchainSynced()) {
        status = ACTIVE_PROSPERITYNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveProsperitynode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_PROSPERITYNODE_SYNC_IN_PROCESS) status = ACTIVE_PROSPERITYNODE_INITIAL;

    if (status == ACTIVE_PROSPERITYNODE_INITIAL) {
        CProsperitynode* pmn;
        pmn = mnodeman.Find(pubKeyProsperitynode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdFundamentalNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_PROSPERITYNODE_STARTED) {
        // Set defaults
        status = ACTIVE_PROSPERITYNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveProsperitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveProsperitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strFundamentalNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the prosperitynodeaddr configuration option.";
                LogPrintf("CActiveProsperitynode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strFundamentalNodeAddr);
        }

        if(!CProsperitynodeBroadcast::CheckDefaultPort(strFundamentalNodeAddr, errorMessage, "CActiveProsperitynode::ManageStatus()"))
            return;

        LogPrintf("CActiveProsperitynode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL, false);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveProsperitynode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetFundamentalNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < PROSPERITYNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_PROSPERITYNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveProsperitynode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyProsperitynode;
            CKey keyProsperitynode;

            if (!obfuScationSigner.SetKey(strFundamentalNodePrivKey, errorMessage, keyProsperitynode, pubKeyProsperitynode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CProsperitynodeBroadcast mnb;
            if (!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyProsperitynode, pubKeyProsperitynode, errorMessage, mnb)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("CActiveProsperitynode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveProsperitynode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            //send to all peers
            LogPrintf("CActiveProsperitynode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            LogPrintf("CActiveProsperitynode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_PROSPERITYNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveProsperitynode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendProsperitynodePing(errorMessage)) {
        LogPrintf("CActiveProsperitynode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveProsperitynode::GetStatus()
{
    switch (status) {
    case ACTIVE_PROSPERITYNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_PROSPERITYNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Prosperitynode";
    case ACTIVE_PROSPERITYNODE_INPUT_TOO_NEW:
        return strprintf("Prosperitynode input must have at least %d confirmations", PROSPERITYNODE_MIN_CONFIRMATIONS);
    case ACTIVE_PROSPERITYNODE_NOT_CAPABLE:
        return "Not capable prosperitynode: " + notCapableReason;
    case ACTIVE_PROSPERITYNODE_STARTED:
        return "Prosperitynode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveProsperitynode::SendProsperitynodePing(std::string& errorMessage)
{
    if (status != ACTIVE_PROSPERITYNODE_STARTED) {
        errorMessage = "Prosperitynode is not in a running status";
        return false;
    }

    CPubKey pubKeyProsperitynode;
    CKey keyProsperitynode;

    if (!obfuScationSigner.SetKey(strFundamentalNodePrivKey, errorMessage, keyProsperitynode, pubKeyProsperitynode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveProsperitynode::SendProsperitynodePing() - Relay Prosperitynode Ping vin = %s\n", vin.ToString());

    CProsperitynodePing mnp(vin);
    if (!mnp.Sign(keyProsperitynode, pubKeyProsperitynode)) {
        errorMessage = "Couldn't sign Prosperitynode Ping";
        return false;
    }

    // Update lastPing for our prosperitynode in Prosperitynode list
    CProsperitynode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(PROSPERITYNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Prosperitynode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenProsperitynodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenProsperitynodeBroadcast.lastPing is probably outdated, so we'll update it
        CProsperitynodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenProsperitynodeBroadcast.count(hash)) mnodeman.mapSeenProsperitynodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        /*
         * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
         * AFTER MIGRATION TO V12 IS DONE
         */

        if (IsSporkActive(SPORK_19_PROSPERITYNODE_PAY_UPDATED_NODES)) return true;
        // for migration purposes ping our node on old prosperitynodes network too
        std::string retErrorMessage;
        std::vector<unsigned char> vchFundamentalNodeSignature;
        int64_t fundamentalNodeSignatureTime = GetAdjustedTime();

        std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(fundamentalNodeSignatureTime) + boost::lexical_cast<std::string>(false);

        if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchFundamentalNodeSignature, keyProsperitynode)) {
            errorMessage = "dseep sign message failed: " + retErrorMessage;
            return false;
        }

        if (!obfuScationSigner.VerifyMessage(pubKeyProsperitynode, vchFundamentalNodeSignature, strMessage, retErrorMessage)) {
            errorMessage = "dseep verify message failed: " + retErrorMessage;
            return false;
        }

        LogPrint("prosperitynode", "dseep - relaying from active mn, %s \n", vin.ToString().c_str());
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes)
            pnode->PushMessage("obseep", vin, vchFundamentalNodeSignature, fundamentalNodeSignatureTime, false);

        /*
         * END OF "REMOVE"
         */

        return true;
    } else {
        // Seems like we are trying to send a ping while the Prosperitynode is not registered in the network
        errorMessage = "Obfuscation Prosperitynode List doesn't include our Prosperitynode, shutting down Prosperitynode pinging service! " + vin.ToString();
        status = ACTIVE_PROSPERITYNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveProsperitynode::CreateBroadcast(std::string strService, std::string strKeyProsperitynode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CProsperitynodeBroadcast &mnb, bool fOffline)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyProsperitynode;
    CKey keyProsperitynode;

    //need correct blocks to send ping
    if (!fOffline && !prosperitynodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Masternode";
        LogPrintf("CActiveProsperitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SetKey(strKeyProsperitynode, errorMessage, keyProsperitynode, pubKeyProsperitynode)) {
        errorMessage = strprintf("Can't find keys for prosperitynode %s - %s", strService, errorMessage);
        LogPrintf("CActiveProsperitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!GetFundamentalNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for prosperitynode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveProsperitynode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);
    if(!CProsperitynodeBroadcast::CheckDefaultPort(strService, errorMessage, "CActiveProsperitynode::CreateBroadcast()"))
        return false;

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyProsperitynode, pubKeyProsperitynode, errorMessage, mnb);
}

bool CActiveProsperitynode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyProsperitynode, CPubKey pubKeyProsperitynode, std::string& errorMessage, CProsperitynodeBroadcast &mnb)
{
	// wait for reindex and/or import to finish
	if (fImporting || fReindex) return false;

    CProsperitynodePing mnp(vin);
    if (!mnp.Sign(keyProsperitynode, pubKeyProsperitynode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveProsperitynode::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CProsperitynodeBroadcast();
        return false;
    }

    mnb = CProsperitynodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyProsperitynode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveProsperitynode::CreateBroadcast() - %s\n", errorMessage);
        mnb = CProsperitynodeBroadcast();
        return false;
    }

    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    if (IsSporkActive(SPORK_19_PROSPERITYNODE_PAY_UPDATED_NODES)) return true;
    // for migration purposes inject our node in old prosperitynodes' list too
    std::string retErrorMessage;
    std::vector<unsigned char> vchFundamentalNodeSignature;
    int64_t fundamentalNodeSignatureTime = GetAdjustedTime();
    std::string donationAddress = "";
    int donationPercantage = 0;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyProsperitynode.begin(), pubKeyProsperitynode.end());

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(fundamentalNodeSignatureTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(PROTOCOL_VERSION) + donationAddress + boost::lexical_cast<std::string>(donationPercantage);

    if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchFundamentalNodeSignature, keyCollateralAddress)) {
        errorMessage = "dsee sign message failed: " + retErrorMessage;
        LogPrintf("CActiveProsperitynode::CreateBroadcast() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, vchFundamentalNodeSignature, strMessage, retErrorMessage)) {
        errorMessage = "dsee verify message failed: " + retErrorMessage;
        LogPrintf("CActiveProsperitynode::CreateBroadcast() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes)
        pnode->PushMessage("obsee", vin, service, vchFundamentalNodeSignature, fundamentalNodeSignatureTime, pubKeyCollateralAddress, pubKeyProsperitynode, -1, -1, fundamentalNodeSignatureTime, PROTOCOL_VERSION, donationAddress, donationPercantage);

    /*
     * END OF "REMOVE"
     */

    return true;
}

bool CActiveProsperitynode::GetFundamentalNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetFundamentalNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveProsperitynode::GetFundamentalNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
	// wait for reindex and/or import to finish
	if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsProsperitynode();
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
            LogPrintf("CActiveProsperitynode::GetFundamentalNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveProsperitynode::GetFundamentalNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Prosperitynode vin information from output
bool CActiveProsperitynode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
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
        LogPrintf("CActiveProsperitynode::GetFundamentalNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf("CActiveProsperitynode::GetFundamentalNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Prosperitynode
vector<COutput> CActiveProsperitynode::SelectCoinsProsperitynode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from prosperitynode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH (CProsperitynodeConfig::CProsperitynodeEntry mne, prosperitynodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from prosperitynode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH (const COutput& out, vCoins) {
        if (out.tx->vout[out.i].nValue == FN_MAGIC_AMOUNT) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Prosperitynode, this can enable to run as a hot wallet with no funds
bool CActiveProsperitynode::EnableHotColdFundamentalNode(CTxIn& newVin, CService& newService)
{
    if (!fFundamentalNode) return false;

    status = ACTIVE_PROSPERITYNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveProsperitynode::EnableHotColdFundamentalNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
