// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "prosperitynode.h"
#include "addrman.h"
#include "prosperitynodeman.h"
#include "obfuscation.h"
#include "sync.h"
#include "util.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenProsperitynodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CProsperitynode::CProsperitynode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyProsperitynode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = PROSPERITYNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CProsperitynodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = PROSPERITYNODE_ENABLED,
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

CProsperitynode::CProsperitynode(const CProsperitynode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyProsperitynode = other.pubKeyProsperitynode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    nActiveState = PROSPERITYNODE_ENABLED,
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nLastDsee = other.nLastDsee;   // temporary, do not save. Remove after migration to v12
    nLastDseep = other.nLastDseep; // temporary, do not save. Remove after migration to v12
}

CProsperitynode::CProsperitynode(const CProsperitynodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeyProsperitynode = mnb.pubKeyProsperitynode;
    sig = mnb.sig;
    activeState = PROSPERITYNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = PROSPERITYNODE_ENABLED,
    protocolVersion = mnb.protocolVersion;
    nLastDsq = mnb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

//
// When a new prosperitynode broadcast is sent, update our information
//
bool CProsperitynode::UpdateFromNewBroadcast(CProsperitynodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeyProsperitynode = mnb.pubKeyProsperitynode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CProsperitynodePing() || (mnb.lastPing != CProsperitynodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenProsperitynodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Prosperitynode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CProsperitynode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrint("prosperitynode","CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CProsperitynode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < PROSPERITYNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == PROSPERITYNODE_VIN_SPENT) return;


    if (!IsPingedWithin(PROSPERITYNODE_REMOVAL_SECONDS)) {
        activeState = PROSPERITYNODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(PROSPERITYNODE_EXPIRATION_SECONDS)) {
        activeState = PROSPERITYNODE_EXPIRED;
        return;
    }

    if(lastPing.sigTime - sigTime < PROSPERITYNODE_MIN_MNP_SECONDS){
    	activeState = PROSPERITYNODE_PRE_ENABLED;
    	return;
    }

    if (!unitTest) {
        /*CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(9999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = PROSPERITYNODE_VIN_SPENT;
                return;
            }
        }*/
    }

    activeState = PROSPERITYNODE_ENABLED; // OK
}

int64_t CProsperitynode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CProsperitynode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = mnodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (prosperitynodePayments.mapProsperitynodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (prosperitynodePayments.mapProsperitynodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CProsperitynode::GetStatus()
{
    switch (nActiveState) {
    case CProsperitynode::PROSPERITYNODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CProsperitynode::PROSPERITYNODE_ENABLED:
        return "ENABLED";
    case CProsperitynode::PROSPERITYNODE_EXPIRED:
        return "EXPIRED";
    case CProsperitynode::PROSPERITYNODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CProsperitynode::PROSPERITYNODE_REMOVE:
        return "REMOVE";
    case CProsperitynode::PROSPERITYNODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CProsperitynode::PROSPERITYNODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CProsperitynode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
           (IsReachable(addr) && addr.IsRoutable());
}

CProsperitynodeBroadcast::CProsperitynodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyProsperitynode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = PROSPERITYNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CProsperitynodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CProsperitynodeBroadcast::CProsperitynodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyProsperitynodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyProsperitynode = pubKeyProsperitynodeNew;
    sig = std::vector<unsigned char>();
    activeState = PROSPERITYNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CProsperitynodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CProsperitynodeBroadcast::CProsperitynodeBroadcast(const CProsperitynode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeyProsperitynode = mn.pubKeyProsperitynode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CProsperitynodeBroadcast::Create(std::string strService, std::string strKeyProsperitynode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CProsperitynodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyProsperitynodeNew;
    CKey keyProsperitynodeNew;

    //need correct blocks to send ping
    if (!fOffline && !prosperitynodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Prosperitynode";
        LogPrint("prosperitynode","CProsperitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!obfuScationSigner.GetKeysFromSecret(strKeyProsperitynode, keyProsperitynodeNew, pubKeyProsperitynodeNew)) {
        strErrorRet = strprintf("Invalid prosperitynode key %s", strKeyProsperitynode);
        LogPrint("prosperitynode","CProsperitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetProsperitynodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for prosperitynode %s", strTxHash, strOutputIndex, strService);
        LogPrint("prosperitynode","CProsperitynodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    // The service needs the correct default port to work properly
    if(!CheckDefaultPort(strService, strErrorRet, "CProsperitynodeBroadcast::Create"))
        return false;


    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyProsperitynodeNew, pubKeyProsperitynodeNew, strErrorRet, mnbRet);
}

bool CProsperitynodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyProsperitynodeNew, CPubKey pubKeyProsperitynodeNew, std::string& strErrorRet, CProsperitynodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("prosperitynode", "CProsperitynodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyProsperitynodeNew.GetID() = %s\n",
        CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
        pubKeyProsperitynodeNew.GetID().ToString());

    CProsperitynodePing mnp(txin);
    if (!mnp.Sign(keyProsperitynodeNew, pubKeyProsperitynodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, prosperitynode=%s", txin.prevout.hash.ToString());
        LogPrint("prosperitynode","CProsperitynodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CProsperitynodeBroadcast();
        return false;
    }

    mnbRet = CProsperitynodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyProsperitynodeNew, PROTOCOL_VERSION);

//    if (!mnbRet.IsValidNetAddr()) {
//        strErrorRet = strprintf("Invalid IP address %s, prosperitynode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
//        LogPrint("prosperitynode","CProsperitynodeBroadcast::Create -- %s\n", strErrorRet);
//        mnbRet = CProsperitynodeBroadcast();
//        return false;
//    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, prosperitynode=%s", txin.prevout.hash.ToString());
        LogPrint("prosperitynode","CProsperitynodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CProsperitynodeBroadcast();
        return false;
    }

    return true;
}

bool CProsperitynodeBroadcast::CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext)
{
    CService service = CService(strService);
    int nDefaultPort = Params().GetDefaultPort();
    
    if (service.GetPort() != nDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for prosperitynode %s, only %d is supported on %s-net.", 
                                        service.GetPort(), strService, nDefaultPort, Params().NetworkIDString());
        LogPrint("prosperitynode", "%s - %s\n", strContext, strErrorRet);
        return false;
    }
 
    return true;
}

bool CProsperitynodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("prosperitynode","mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    // incorrect ping or its sigTime
    if(lastPing == CProsperitynodePing() || !lastPing.CheckAndUpdate(nDos, false, true))
        return false;

    if (protocolVersion < prosperitynodePayments.GetMinProsperitynodePaymentsProto()) {
        LogPrint("prosperitynode","mnb - ignoring outdated Prosperitynode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrint("prosperitynode","mnb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyProsperitynode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrint("prosperitynode","mnb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("prosperitynode","mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetNewStrMessage(), errorMessage)
    		&& !obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetOldStrMessage(), errorMessage)) {
        // don't ban for old prosperitynodes, their sigs could be broken because of the bug
        nDos = protocolVersion < MIN_PEER_MNANNOUNCE ? 0 : 100;
        return error("CProsperitynodeBroadcast::CheckAndUpdate - Got bad Prosperitynode address signature\n");
    }

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 9765) return false;
    } else if (addr.GetPort() == 9765)
        return false;

    //search existing Prosperitynode list, this is where we update existing Prosperitynodes with new mnb broadcasts
    CProsperitynode* pmn = mnodeman.Find(vin);

    // no such prosperitynode, nothing to update
    if (pmn == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
	// unless someone is doing something fishy
	// (mapSeenMasternodeBroadcast in CMasternodeMan::ProcessMessage should filter legit duplicates)
	if(pmn->sigTime >= sigTime) {
		return error("CProsperitynodeBroadcast::CheckAndUpdate - Bad sigTime %d for Prosperitynode %20s %105s (existing broadcast is at %d)\n",
					  sigTime, addr.ToString(), vin.ToString(), pmn->sigTime);
    }

    // prosperitynode is not enabled yet/already, nothing to update
    if (!pmn->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(PROSPERITYNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("prosperitynode","mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        prosperitynodeSync.AddedProsperitynodeList(GetHash());
    }

    return true;
}

bool CProsperitynodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a prosperitynode with the same vin (i.e. already activated) and this mnb is ours (matches our Prosperitynode privkey)
    // so nothing to do here for us
    if (fFundamentalNode && vin.prevout == activeProsperitynode.vin.prevout && pubKeyProsperitynode == activeProsperitynode.pubKeyProsperitynode)
        return true;

    // incorrect ping or its sigTime
    if(lastPing == CProsperitynodePing() || !lastPing.CheckAndUpdate(nDoS, false, true)) return false;

    // search existing Prosperitynode list
    CProsperitynode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this prosperitynode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    CValidationState state;
    uint256 hashBlock = 0;
    CTransaction tx2, tx1;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            mnodeman.mapSeenProsperitynodeBroadcast.erase(GetHash());
            prosperitynodeSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        int64_t nValueIn = 0;

        BOOST_FOREACH (const CTxIn& txin, tx2.vin) {
            // First try finding the previous transaction in database
            CTransaction txPrev;
            uint256 hashBlockPrev;
            if (!GetTransaction(txin.prevout.hash, txPrev, hashBlockPrev, true)) {
                LogPrintf("CheckInputsAndAdd: failed to find vin transaction \n");
                continue; // previous transaction not in main chain
            }

           nValueIn += txPrev.vout[txin.prevout.n].nValue;

        }

        if(nValueIn - tx2.GetValueOut() < PROSPERITYNODE_AMOUNT - FN_MAGIC_AMOUNT){
            state.IsInvalid(nDoS);
            return false;
        }



        /*if (!AcceptableFundamentalTxn(mempool, state, CTransaction(tx2))) {
            //set nDos
            LogPrintf("AcceptableFN is false tx hash = %s \n", tx2.GetHash().GetHex());
            state.IsInvalid(nDoS);
            return false;
        }*/
    }

    LogPrint("prosperitynode", "mnb - Accepted Prosperitynode entry\n");

    if (GetInputAge(vin) < PROSPERITYNODE_MIN_CONFIRMATIONS) {
        LogPrint("prosperitynode","mnb - Input must have at least %d confirmations\n", PROSPERITYNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenProsperitynodeBroadcast.erase(GetHash());
        prosperitynodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 DIVIT tx got PROSPERITYNODE_MIN_CONFIRMATIONS


    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 DIVIT tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + PROSPERITYNODE_MIN_CONFIRMATIONS - 1]; // block where tx got PROSPERITYNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("prosperitynode","mnb - Bad sigTime %d for Prosperitynode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), PROSPERITYNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("prosperitynode","mnb - Got NEW Prosperitynode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CProsperitynode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Prosperitynode privkey, then we've been remotely activated
    if (pubKeyProsperitynode == activeProsperitynode.pubKeyProsperitynode && protocolVersion == PROTOCOL_VERSION) {
        activeProsperitynode.EnableHotColdFundamentalNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CProsperitynodeBroadcast::Relay()
{
    CInv inv(MSG_PROSPERITYNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CProsperitynodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;
    sigTime = GetAdjustedTime();

    std::string strMessage;
    if(chainActive.Height() < Params().Zerocoin_Block_V2_Start())
    	strMessage = GetOldStrMessage();
    else
    	strMessage = GetNewStrMessage();

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        return error("CProsperitynodeBroadcast::Sign() - Error: %s\n", errorMessage);
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        return error("CProsperitynodeBroadcast::Sign() - Error: %s\n", errorMessage);
    }

    return true;
}

bool CProsperitynodeBroadcast::VerifySignature()
{
    std::string errorMessage;

    if(!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetNewStrMessage(), errorMessage)
    		&& !obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetOldStrMessage(), errorMessage)) {
        return error("CProsperitynodeBroadcast::VerifySignature() - Error: %s\n", errorMessage);
    }

    return true;
}

std::string CProsperitynodeBroadcast::GetOldStrMessage()
{
    std::string strMessage;

	std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
	std::string vchPubKey2(pubKeyProsperitynode.begin(), pubKeyProsperitynode.end());
	strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    return strMessage;
}

std:: string CProsperitynodeBroadcast::GetNewStrMessage()
{
	std::string strMessage;

	strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + pubKeyCollateralAddress.GetID().ToString() + pubKeyProsperitynode.GetID().ToString() + boost::lexical_cast<std::string>(protocolVersion);

	return strMessage;
}

CProsperitynodePing::CProsperitynodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CProsperitynodePing::CProsperitynodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CProsperitynodePing::Sign(CKey& keyProsperitynode, CPubKey& pubKeyProsperitynode)
{
    std::string errorMessage;
    std::string strFundamentalNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyProsperitynode)) {
        LogPrint("prosperitynode","CProsperitynodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyProsperitynode, vchSig, strMessage, errorMessage)) {
        LogPrint("prosperitynode","CProsperitynodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CProsperitynodePing::VerifySignature(CPubKey& pubKeyProsperitynode, int &nDos) {
	std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
	std::string errorMessage = "";

	if(!obfuScationSigner.VerifyMessage(pubKeyProsperitynode, vchSig, strMessage, errorMessage)){
		nDos = 33;
		return error("CProsperitynodePing::VerifySignature - Got bad Prosperitynode ping signature %s Error: %s\n", vin.ToString(), errorMessage);
	}
	return true;
}

bool CProsperitynodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("prosperitynode","CProsperitynodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("prosperitynode","CProsperitynodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if(fCheckSigTimeOnly) {
    	CProsperitynode* pmn = mnodeman.Find(vin);
    	if(pmn) return VerifySignature(pmn->pubKeyProsperitynode, nDos);
    	return true;
    }

    LogPrint("prosperitynode", "CProsperitynodePing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this Prosperitynode
    CProsperitynode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= prosperitynodePayments.GetMinProsperitynodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrint("prosperitynode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this prosperitynode or
        // last ping was more then PROSPERITYNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(PROSPERITYNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        	if (!VerifySignature(pmn->pubKeyProsperitynode, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("prosperitynode","CProsperitynodePing::CheckAndUpdate - Prosperitynode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Prosperitynode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("prosperitynode","CProsperitynodePing::CheckAndUpdate - Prosperitynode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenProsperitynodeBroadcast.lastPing is probably outdated, so we'll update it
            CProsperitynodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenProsperitynodeBroadcast.count(hash)) {
                mnodeman.mapSeenProsperitynodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("prosperitynode", "CProsperitynodePing::CheckAndUpdate - Prosperitynode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("prosperitynode", "CProsperitynodePing::CheckAndUpdate - Prosperitynode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("prosperitynode", "CProsperitynodePing::CheckAndUpdate - Couldn't find compatible Prosperitynode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CProsperitynodePing::Relay()
{
    CInv inv(MSG_PROSPERITYNODE_PING, GetHash());
    RelayInv(inv);
}
