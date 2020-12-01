// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "prosperitynodeman.h"
#include "activeprosperitynode.h"
#include "addrman.h"
#include "prosperitynode.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > PROSPERITYNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** prosperitynode manager */
CProsperitynodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CProsperitynode>& t1,
        const pair<int64_t, CProsperitynode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CProsperitynodeDB
//

CProsperitynodeDB::CProsperitynodeDB()
{
    pathMN = GetDataDir() / "fncache.dat";
    strMagicMessage = "ProsperitynodeCache";
}

bool CProsperitynodeDB::Write(const CProsperitynodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssProsperitynodes(SER_DISK, CLIENT_VERSION);
    ssProsperitynodes << strMagicMessage;                   // prosperitynode cache file specific magic message
    ssProsperitynodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssProsperitynodes << mnodemanToSave;
    uint256 hash = Hash(ssProsperitynodes.begin(), ssProsperitynodes.end());
    ssProsperitynodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssProsperitynodes;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("prosperitynode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("prosperitynode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CProsperitynodeDB::ReadResult CProsperitynodeDB::Read(CProsperitynodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (const std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssProsperitynodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssProsperitynodes.begin(), ssProsperitynodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (prosperitynode cache file specific magic message) and ..

        ssProsperitynodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid prosperitynode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssProsperitynodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CProsperitynodeMan object
        ssProsperitynodes >> mnodemanToLoad;
    } catch (const std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("prosperitynode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("prosperitynode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("prosperitynode","Prosperitynode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("prosperitynode","Prosperitynode manager - result:\n");
        LogPrint("prosperitynode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpProsperitynodes()
{
    int64_t nStart = GetTimeMillis();

    CProsperitynodeDB mndb;
    CProsperitynodeMan tempMnodeman;

    LogPrint("prosperitynode","Verifying mncache.dat format...\n");
    CProsperitynodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CProsperitynodeDB::FileError)
        LogPrint("prosperitynode","Missing prosperitynode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CProsperitynodeDB::Ok) {
        LogPrint("prosperitynode","Error reading mncache.dat: ");
        if (readResult == CProsperitynodeDB::IncorrectFormat)
            LogPrint("prosperitynode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("prosperitynode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("prosperitynode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("prosperitynode","Prosperitynode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CProsperitynodeMan::CProsperitynodeMan()
{
    nDsqCount = 0;
}

bool CProsperitynodeMan::Add(CProsperitynode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CProsperitynode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("prosperitynode", "CProsperitynodeMan: Adding new Prosperitynode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vProsperitynodes.push_back(mn);
        return true;
    }

    return false;
}

void CProsperitynodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForProsperitynodeListEntry.find(vin.prevout);
    if (i != mWeAskedForProsperitynodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the fnb info once from the node that sent fnp

    LogPrint("prosperitynode", "CProsperitynodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("obseg", vin);
    int64_t askAgain = GetTime() + PROSPERITYNODE_MIN_MNP_SECONDS;
    mWeAskedForProsperitynodeListEntry[vin.prevout] = askAgain;
}

void CProsperitynodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        mn.Check();
    }
}

void CProsperitynodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CProsperitynode>::iterator it = vProsperitynodes.begin();
    while (it != vProsperitynodes.end()) {
        if ((*it).activeState == CProsperitynode::PROSPERITYNODE_REMOVE ||
            (*it).activeState == CProsperitynode::PROSPERITYNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CProsperitynode::PROSPERITYNODE_EXPIRED) ||
            (*it).protocolVersion < prosperitynodePayments.GetMinProsperitynodePaymentsProto()) {
            LogPrint("prosperitynode", "CProsperitynodeMan: Removing inactive Prosperitynode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new fnb
            map<uint256, CProsperitynodeBroadcast>::iterator it3 = mapSeenProsperitynodeBroadcast.begin();
            while (it3 != mapSeenProsperitynodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    prosperitynodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenProsperitynodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this prosperitynode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForProsperitynodeListEntry.begin();
            while (it2 != mWeAskedForProsperitynodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForProsperitynodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vProsperitynodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Prosperitynode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForProsperitynodeList.begin();
    while (it1 != mAskedUsForProsperitynodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForProsperitynodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Prosperitynode list
    it1 = mWeAskedForProsperitynodeList.begin();
    while (it1 != mWeAskedForProsperitynodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForProsperitynodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Prosperitynodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForProsperitynodeListEntry.begin();
    while (it2 != mWeAskedForProsperitynodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForProsperitynodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenProsperitynodeBroadcast
    map<uint256, CProsperitynodeBroadcast>::iterator it3 = mapSeenProsperitynodeBroadcast.begin();
    while (it3 != mapSeenProsperitynodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (PROSPERITYNODE_REMOVAL_SECONDS * 2)) {
            mapSeenProsperitynodeBroadcast.erase(it3++);
            prosperitynodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenProsperitynodePing
    map<uint256, CProsperitynodePing>::iterator it4 = mapSeenProsperitynodePing.begin();
    while (it4 != mapSeenProsperitynodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (PROSPERITYNODE_REMOVAL_SECONDS * 2)) {
            mapSeenProsperitynodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CProsperitynodeMan::Clear()
{
    LOCK(cs);
    vProsperitynodes.clear();
    mAskedUsForProsperitynodeList.clear();
    mWeAskedForProsperitynodeList.clear();
    mWeAskedForProsperitynodeListEntry.clear();
    mapSeenProsperitynodeBroadcast.clear();
    mapSeenProsperitynodePing.clear();
    nDsqCount = 0;
}

int CProsperitynodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nProsperitynode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nProsperitynode_Age = 0;

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive (SPORK_8_PROSPERITYNODE_PAYMENT_ENFORCEMENT)) {
            nProsperitynode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nProsperitynode_Age) < nProsperitynode_Min_Age) {
                continue; // Skip prosperitynodes younger than (default) 8000 sec (MUST be > PROSPERITYNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled prosperitynodes

        nStable_size++;
    }

    return nStable_size;
}

int CProsperitynodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? prosperitynodePayments.GetMinProsperitynodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CProsperitynodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? prosperitynodePayments.GetMinProsperitynodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CProsperitynodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForProsperitynodeList.find(pnode->addr);
            if (it != mWeAskedForProsperitynodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("prosperitynode", "obseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("obseg", CTxIn());
    int64_t askAgain = GetTime() + PROSPERITYNODES_DSEG_SECONDS;
    mWeAskedForProsperitynodeList[pnode->addr] = askAgain;
}

CProsperitynode* CProsperitynodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CProsperitynode* CProsperitynodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CProsperitynode* CProsperitynodeMan::Find(const CPubKey& pubKeyProsperitynode)
{
    LOCK(cs);

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        if (mn.pubKeyProsperitynode == pubKeyProsperitynode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best prosperitynode to pay on the network
//
CProsperitynode* CProsperitynodeMan::GetNextProsperitynodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CProsperitynode* pBestProsperitynode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecProsperitynodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < prosperitynodePayments.GetMinProsperitynodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (prosperitynodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are prosperitynodes
        if (mn.GetProsperitynodeInputAge() < nMnCount) continue;

        vecProsperitynodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecProsperitynodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextProsperitynodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecProsperitynodeLastPaid.rbegin(), vecProsperitynodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecProsperitynodeLastPaid) {
        CProsperitynode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestProsperitynode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestProsperitynode;
}

CProsperitynode* CProsperitynodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? prosperitynodePayments.GetMinProsperitynodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("prosperitynode", "CProsperitynodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("prosperitynode", "CProsperitynodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CProsperitynode* CProsperitynodeMan::GetCurrentFundamentalNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CProsperitynode* winner = NULL;

    // scan for winner
    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Prosperitynode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CProsperitynodeMan::GetProsperitynodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecProsperitynodeScores;
    int64_t nProsperitynode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nProsperitynode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("prosperitynode","Skipping Prosperitynode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_PROSPERITYNODE_PAYMENT_ENFORCEMENT)) {
            nProsperitynode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nProsperitynode_Age) < nProsperitynode_Min_Age) {
                if (fDebug) LogPrint("prosperitynode","Skipping just activated Prosperitynode. Age: %ld\n", nProsperitynode_Age);
                continue;                                                   // Skip prosperitynodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecProsperitynodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecProsperitynodeScores.rbegin(), vecProsperitynodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecProsperitynodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CProsperitynode> > CProsperitynodeMan::GetProsperitynodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CProsperitynode> > vecProsperitynodeScores;
    std::vector<pair<int, CProsperitynode> > vecProsperitynodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecProsperitynodeRanks;

    // scan for winner
    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecProsperitynodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecProsperitynodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecProsperitynodeScores.rbegin(), vecProsperitynodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CProsperitynode) & s, vecProsperitynodeScores) {
        rank++;
        vecProsperitynodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecProsperitynodeRanks;
}

CProsperitynode* CProsperitynodeMan::GetProsperitynodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecProsperitynodeScores;

    // scan for winner
    BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecProsperitynodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecProsperitynodeScores.rbegin(), vecProsperitynodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecProsperitynodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CProsperitynodeMan::ProcessProsperitynodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (pnode->fObfuScationMaster) {
            if (obfuScationPool.pSubmittedToProsperitynode != NULL && pnode->addr == obfuScationPool.pSubmittedToProsperitynode->addr) continue;
            LogPrint("prosperitynode","Closing Prosperitynode connection peer=%i \n", pnode->GetId());
            pnode->fObfuScationMaster = false;
            pnode->Release();
        }
    }
}

void CProsperitynodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Prosperitynode related functionality
    if (!prosperitynodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "fnb") { //Prosperitynode Broadcast
        CProsperitynodeBroadcast fnb;
        vRecv >> fnb;

        if (mapSeenProsperitynodeBroadcast.count(fnb.GetHash())) { //seen
            prosperitynodeSync.AddedProsperitynodeList(fnb.GetHash());
            return;
        }
        mapSeenProsperitynodeBroadcast.insert(make_pair(fnb.GetHash(), fnb));

        int nDoS = 0;
        if (!fnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        uint256 hashBlock =0 ;
        CTransaction tx;
        // make sure the vout that was signed is related to the transaction that spawned the Prosperitynode
        //  - this is expensive, so it's only done once per Prosperitynode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(fnb.vin, fnb.pubKeyCollateralAddress, tx, hashBlock)) {
            LogPrint("prosperitynode","fnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (fnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(fnb.addr), pfrom->addr, 2 * 60 * 60);
            prosperitynodeSync.AddedProsperitynodeList(fnb.GetHash());
        } else {
            LogPrint("prosperitynode","fnb - Rejected Prosperitynode entry %s\n", fnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "fnp") { //Prosperitynode Ping
        CProsperitynodePing fnp;
        vRecv >> fnp;

        LogPrint("prosperitynode", "fnp - Prosperitynode ping, vin: %s\n", fnp.vin.prevout.hash.ToString());

        if (mapSeenProsperitynodePing.count(fnp.GetHash())) return; //seen
        mapSeenProsperitynodePing.insert(make_pair(fnp.GetHash(), fnp));

        int nDoS = 0;
        if (fnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Prosperitynode list
            CProsperitynode* pmn = Find(fnp.vin);
            // if it's known, don't ask for the fnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a prosperitynode entry once
        AskForMN(pfrom, fnp.vin);

    } else if (strCommand == "obseg") { //Get Prosperitynode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForProsperitynodeList.find(pfrom->addr);
                if (i != mAskedUsForProsperitynodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrint("prosperitynode","obseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + PROSPERITYNODES_DSEG_SECONDS;
                mAskedUsForProsperitynodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CProsperitynode& mn, vProsperitynodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("prosperitynode", "obseg - Sending Prosperitynode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CProsperitynodeBroadcast fnb = CProsperitynodeBroadcast(mn);
                    uint256 hash = fnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_PROSPERITYNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenProsperitynodeBroadcast.count(hash)) mapSeenProsperitynodeBroadcast.insert(make_pair(hash, fnb));

                    if (vin == mn.vin) {
                        LogPrint("prosperitynode", "obseg - Sent 1 Prosperitynode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", PROSPERITYNODE_SYNC_LIST, nInvCount);
            LogPrint("prosperitynode", "obseg - Sent %d Prosperitynode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    // Light version for OLD MASSTERNODES - fake pings, no self-activation
    else if (strCommand == "obsee") { //ObfuScation Election Entry

        if (IsSporkActive(SPORK_19_PROSPERITYNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript donationAddress;
        int donationPercentage;
        std::string strMessage;

        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> donationAddress >> donationPercentage;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint("prosperitynode","obsee - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion) + donationAddress.ToString() + boost::lexical_cast<std::string>(donationPercentage);

        if (protocolVersion < prosperitynodePayments.GetMinProsperitynodePaymentsProto()) {
            LogPrint("prosperitynode","obsee - ignoring outdated Prosperitynode %s protocol version %d < %d\n", vin.prevout.hash.ToString(), protocolVersion, prosperitynodePayments.GetMinProsperitynodePaymentsProto());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(pubkey.GetID());

        if (pubkeyScript.size() != 25) {
            LogPrint("prosperitynode","obsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

        if (pubkeyScript2.size() != 25) {
            LogPrint("prosperitynode","obsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (!vin.scriptSig.empty()) {
            LogPrint("prosperitynode","obsee - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)) {
            LogPrint("prosperitynode","obsee - Got bad Prosperitynode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (addr.GetPort() != 9765) return;
        } else if (addr.GetPort() == 9765)
            return;

        //search existing Prosperitynode list, this is where we update existing Prosperitynodes with new obsee broadcasts
        CProsperitynode* pmn = this->Find(vin);
        if (pmn != NULL) {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if (count == -1 && pmn->pubKeyCollateralAddress == pubkey && (GetAdjustedTime() - pmn->nLastDsee > PROSPERITYNODE_MIN_MNB_SECONDS)) {
                if (pmn->protocolVersion > GETHEADERS_VERSION && sigTime - pmn->lastPing.sigTime < PROSPERITYNODE_MIN_MNB_SECONDS) return;
                if (pmn->nLastDsee < sigTime) { //take the newest entry
                    LogPrint("prosperitynode", "obsee - Got updated entry for %s\n", vin.prevout.hash.ToString());
                    if (pmn->protocolVersion < GETHEADERS_VERSION) {
                        pmn->pubKeyProsperitynode = pubkey2;
                        pmn->sigTime = sigTime;
                        pmn->sig = vchSig;
                        pmn->protocolVersion = protocolVersion;
                        pmn->addr = addr;
                        //fake ping
                        pmn->lastPing = CProsperitynodePing(vin);
                    }
                    pmn->nLastDsee = sigTime;
                    pmn->Check();
                    if (pmn->IsEnabled()) {
                        TRY_LOCK(cs_vNodes, lockNodes);
                        if (!lockNodes) return;
                        BOOST_FOREACH (CNode* pnode, vNodes)
                            if (pnode->nVersion >= prosperitynodePayments.GetMinProsperitynodePaymentsProto())
                                pnode->PushMessage("obsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
                    }
                }
            }

            return;
        }

        static std::map<COutPoint, CPubKey> mapSeenobsee;
        if (mapSeenobsee.count(vin.prevout) && mapSeenobsee[vin.prevout] == pubkey) {
            LogPrint("prosperitynode", "obsee - already seen this vin %s\n", vin.prevout.ToString());
            return;
        }
        mapSeenobsee.insert(make_pair(vin.prevout, pubkey));
        // make sure the vout that was signed is related to the transaction that spawned the Prosperitynode
        //  - this is expensive, so it's only done once per Prosperitynode

        uint256 hashBlock = 0;
        CTransaction tx;


        if (!obfuScationSigner.IsVinAssociatedWithPubkey(vin, pubkey, tx, hashBlock )) {
            LogPrint("prosperitynode","obsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }


        LogPrint("prosperitynode", "obsee - Got NEW OLD Prosperitynode entry %s\n", vin.prevout.hash.ToString());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()

        CValidationState state;
        /*CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(9999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);*/

        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;
            fAcceptable = AcceptableFundamentalTxn(mempool, state, tx);
        }

        if (fAcceptable) {
            if (GetInputAge(vin) < PROSPERITYNODE_MIN_CONFIRMATIONS) {
                LogPrint("prosperitynode","obsee - Input must have least %d confirmations\n", PROSPERITYNODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 1000 DIVIT tx got PROSPERITYNODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            CTransaction tx2;
            GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
            BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 10000 DIVIT tx -> 1 confirmation
                CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + PROSPERITYNODE_MIN_CONFIRMATIONS - 1]; // block where tx got PROSPERITYNODE_MIN_CONFIRMATIONS
                if (pConfIndex->GetBlockTime() > sigTime) {
                    LogPrint("prosperitynode","fnb - Bad sigTime %d for Prosperitynode %s (%i conf block is at %d)\n",
                        sigTime, vin.prevout.hash.ToString(), PROSPERITYNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2 * 60 * 60);

            // add Prosperitynode
            CProsperitynode mn = CProsperitynode();
            mn.addr = addr;
            mn.vin = vin;
            mn.pubKeyCollateralAddress = pubkey;
            mn.sig = vchSig;
            mn.sigTime = sigTime;
            mn.pubKeyProsperitynode = pubkey2;
            mn.protocolVersion = protocolVersion;
            // fake ping
            mn.lastPing = CProsperitynodePing(vin);
            mn.Check(true);
            // add v11 prosperitynodes, v12 should be added by fnb only
            if (protocolVersion < GETHEADERS_VERSION) {
                LogPrint("prosperitynode", "obsee - Accepted OLD Prosperitynode entry %i %i\n", count, current);
                Add(mn);
            }
            if (mn.IsEnabled()) {
                TRY_LOCK(cs_vNodes, lockNodes);
                if (!lockNodes) return;
                BOOST_FOREACH (CNode* pnode, vNodes)
                    if (pnode->nVersion >= prosperitynodePayments.GetMinProsperitynodePaymentsProto())
                        pnode->PushMessage("obsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
            }
        } else {
            LogPrint("prosperitynode","obsee - Rejected Prosperitynode entry %s\n", vin.prevout.hash.ToString());

            int nDoS = 0;
            if (state.IsInvalid(nDoS)) {
                LogPrint("prosperitynode","obsee - %s from %i %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->GetId(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == "obseep") { //ObfuScation Election Entry Ping

        if (IsSporkActive(SPORK_19_PROSPERITYNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //LogPrint("prosperitynode","obseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint("prosperitynode","obseep - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrint("prosperitynode","obseep - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForProsperitynodeListEntry.find(vin.prevout);
        if (i != mWeAskedForProsperitynodeListEntry.end()) {
            int64_t t = (*i).second;
            if (GetTime() < t) return; // we've asked recently
        }

        // see if we have this Prosperitynode
        CProsperitynode* pmn = this->Find(vin);
        if (pmn != NULL && pmn->protocolVersion >= prosperitynodePayments.GetMinProsperitynodePaymentsProto()) {
            // LogPrint("prosperitynode","obseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if (sigTime - pmn->nLastDseep > PROSPERITYNODE_MIN_MNP_SECONDS) {
                std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                std::string errorMessage = "";
                if (!obfuScationSigner.VerifyMessage(pmn->pubKeyProsperitynode, vchSig, strMessage, errorMessage)) {
                    LogPrint("prosperitynode","obseep - Got bad Prosperitynode address signature %s \n", vin.prevout.hash.ToString());
                    //Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                // fake ping for v11 prosperitynodes, ignore for v12
                if (pmn->protocolVersion < GETHEADERS_VERSION) pmn->lastPing = CProsperitynodePing(vin);
                pmn->nLastDseep = sigTime;
                pmn->Check();
                if (pmn->IsEnabled()) {
                    TRY_LOCK(cs_vNodes, lockNodes);
                    if (!lockNodes) return;
                    LogPrint("prosperitynode", "obseep - relaying %s \n", vin.prevout.hash.ToString());
                    BOOST_FOREACH (CNode* pnode, vNodes)
                        if (pnode->nVersion >= prosperitynodePayments.GetMinProsperitynodePaymentsProto())
                            pnode->PushMessage("obseep", vin, vchSig, sigTime, stop);
                }
            }
            return;
        }

        LogPrint("prosperitynode", "obseep - Couldn't find Prosperitynode entry %s peer=%i\n", vin.prevout.hash.ToString(), pfrom->GetId());

        AskForMN(pfrom, vin);
    }

    /*
     * END OF "REMOVE"
     */
}

void CProsperitynodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CProsperitynode>::iterator it = vProsperitynodes.begin();
    while (it != vProsperitynodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("prosperitynode", "CProsperitynodeMan: Removing Prosperitynode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vProsperitynodes.erase(it);
            break;
        }
        ++it;
    }
}

void CProsperitynodeMan::UpdateProsperitynodeList(CProsperitynodeBroadcast fnb)
{
    mapSeenProsperitynodePing.insert(std::make_pair(fnb.lastPing.GetHash(), fnb.lastPing));
    mapSeenProsperitynodeBroadcast.insert(std::make_pair(fnb.GetHash(), fnb));
	prosperitynodeSync.AddedProsperitynodeList(fnb.GetHash());

    LogPrint("prosperitynode","CProsperitynodeMan::UpdateProsperitynodeList() -- prosperitynode=%s\n", fnb.vin.prevout.ToString());

    CProsperitynode* pmn = Find(fnb.vin);
    if (pmn == NULL) {
        CProsperitynode mn(fnb);
        Add(mn);
    } else {
        pmn->UpdateFromNewBroadcast(fnb);
    }
}

std::string CProsperitynodeMan::ToString() const
{
    std::ostringstream info;

    info << "Prosperitynodes: " << (int)vProsperitynodes.size() << ", peers who asked us for Prosperitynode list: " << (int)mAskedUsForProsperitynodeList.size() << ", peers we asked for Prosperitynode list: " << (int)mWeAskedForProsperitynodeList.size() << ", entries in Prosperitynode list we asked for: " << (int)mWeAskedForProsperitynodeListEntry.size() << ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
