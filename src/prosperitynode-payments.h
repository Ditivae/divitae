// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PROSPERITYNODE_PAYMENTS_H
#define PROSPERITYNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "prosperitynode.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapProsperitynodeBlocks;
extern CCriticalSection cs_mapProsperitynodePayeeVotes;

class CProsperitynodePayments;
class CProsperitynodePaymentWinner;
class CProsperitynodeBlockPayees;

extern CProsperitynodePayments prosperitynodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageProsperitynodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake, bool IsMasternode );

void DumpProsperitynodePayments();

/** Save Prosperitynode Payment Data (fnpayments.dat)
 */
class CProsperitynodePaymentDB
{
private:
    boost::filesystem::path pathDB;
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

    CProsperitynodePaymentDB();
    bool Write(const CProsperitynodePayments& objToSave);
    ReadResult Read(CProsperitynodePayments& objToLoad, bool fDryRun = false);
};

class CProsperitynodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CProsperitynodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CProsperitynodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from prosperitynodes
class CProsperitynodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CProsperitynodePayee> vecPayments;

    CProsperitynodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CProsperitynodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CProsperitynodePayee& payee, vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CProsperitynodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH (CProsperitynodePayee& p, vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CProsperitynodePayee& p, vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CProsperitynodePaymentWinner
{
public:
    CTxIn vinProsperitynode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CProsperitynodePaymentWinner()
    {
        nBlockHeight = 0;
        vinProsperitynode = CTxIn();
        payee = CScript();
    }

    CProsperitynodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinProsperitynode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinProsperitynode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyProsperitynode, CPubKey& pubKeyProsperitynode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinProsperitynode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinProsperitynode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// prosperitynode Payments Class
// Keeps track of who should get paid for which blocks
//

class CProsperitynodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CProsperitynodePaymentWinner> mapProsperitynodePayeeVotes;
    std::map<int, CProsperitynodeBlockPayees> mapProsperitynodeBlocks;
    std::map<uint256, int> mapProsperitynodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CProsperitynodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapProsperitynodeBlocks, cs_mapProsperitynodePayeeVotes);
        mapProsperitynodeBlocks.clear();
        mapProsperitynodePayeeVotes.clear();
    }

    bool AddWinningProsperitynode(CProsperitynodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CProsperitynode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CProsperitynode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outProsperitynode, int nBlockHeight)
    {
        LOCK(cs_mapProsperitynodePayeeVotes);

        if (mapProsperitynodesLastVote.count(outProsperitynode.hash + outProsperitynode.n)) {
            if (mapProsperitynodesLastVote[outProsperitynode.hash + outProsperitynode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this prosperitynode voted
        mapProsperitynodesLastVote[outProsperitynode.hash + outProsperitynode.n] = nBlockHeight;
        return true;
    }

    int GetMinProsperitynodePaymentsProto();
    void ProcessMessageProsperitynodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake, bool IsMasternode);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapProsperitynodePayeeVotes);
        READWRITE(mapProsperitynodeBlocks);
    }
};


#endif
