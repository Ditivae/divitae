// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PROSPERITYNODEMAN_H
#define PROSPERITYNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "prosperitynode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define PROSPERITYNODES_DUMP_SECONDS (15 * 60)
#define PROSPERITYNODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CProsperitynodeMan;

extern CProsperitynodeMan mnodeman;
void DumpProsperitynodes();

/** Access to the MN database (mncache.dat)
 */
class CProsperitynodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CProsperitynodeDB();
    bool Write(const CProsperitynodeMan& mnodemanToSave);
    ReadResult Read(CProsperitynodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CProsperitynodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CProsperitynode> vProsperitynodes;
    // who's asked for the Prosperitynode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForProsperitynodeList;
    // who we asked for the Prosperitynode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForProsperitynodeList;
    // which Prosperitynodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForProsperitynodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CProsperitynodeBroadcast> mapSeenProsperitynodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CProsperitynodePing> mapSeenProsperitynodePing;

    // keep track of dsq count to prevent prosperitynodes from gaming obfuscation queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vProsperitynodes);
        READWRITE(mAskedUsForProsperitynodeList);
        READWRITE(mWeAskedForProsperitynodeList);
        READWRITE(mWeAskedForProsperitynodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenProsperitynodeBroadcast);
        READWRITE(mapSeenProsperitynodePing);
    }

    CProsperitynodeMan();
    CProsperitynodeMan(CProsperitynodeMan& other);

    /// Add an entry
    bool Add(CProsperitynode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Prosperitynodes
    void Check();

    /// Check all Prosperitynodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Prosperitynode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CProsperitynode* Find(const CScript& payee);
    CProsperitynode* Find(const CTxIn& vin);
    CProsperitynode* Find(const CPubKey& pubKeyProsperitynode);

    /// Find an entry in the prosperitynode list that is next to be paid
    CProsperitynode* GetNextProsperitynodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CProsperitynode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CProsperitynode* GetCurrentFundamentalNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CProsperitynode> GetFullProsperitynodeVector()
    {
        Check();
        return vProsperitynodes;
    }

    std::vector<pair<int, CProsperitynode> > GetProsperitynodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetProsperitynodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CProsperitynode* GetProsperitynodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessProsperitynodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Prosperitynodes
    int size() { return vProsperitynodes.size(); }

    /// Return the number of Prosperitynodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    int GetEstimatedMasternodes(int nBlock);

    /// Update prosperitynode list and maps using provided CProsperitynodeBroadcast
    void UpdateProsperitynodeList(CProsperitynodeBroadcast mnb);
};

#endif
