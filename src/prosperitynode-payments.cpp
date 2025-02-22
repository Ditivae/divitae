// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "prosperitynode-payments.h"
#include "addrman.h"
#include "prosperitynode-budget.h"
#include "prosperitynode-sync.h"
#include "prosperitynodeman.h"
#include "obfuscation.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"

#include "masternode-pos.h"
#include "masternode.h"
#include "masternodeman.h"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CProsperitynodePayments prosperitynodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapProsperitynodeBlocks;
CCriticalSection cs_mapProsperitynodePayeeVotes;

//
// CProsperitynodePaymentDB
//

CProsperitynodePaymentDB::CProsperitynodePaymentDB()
{
    pathDB = GetDataDir() / "fnpayments.dat";
    strMagicMessage = "ProsperitynodePayments";
}

bool CProsperitynodePaymentDB::Write(const CProsperitynodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // prosperitynode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("prosperitynode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CProsperitynodePaymentDB::ReadResult CProsperitynodePaymentDB::Read(CProsperitynodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (prosperitynode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid prosperitynode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CProsperitynodePayments object
        ssObj >> objToLoad;
    } catch (const std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("prosperitynode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("prosperitynode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("prosperitynode","Prosperitynode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("prosperitynode","Prosperitynode payments manager - result:\n");
        LogPrint("prosperitynode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpProsperitynodePayments()
{
    int64_t nStart = GetTimeMillis();

    CProsperitynodePaymentDB paymentdb;
    CProsperitynodePayments tempPayments;

    LogPrint("prosperitynode","Verifying mnpayments.dat format...\n");
    CProsperitynodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CProsperitynodePaymentDB::FileError)
        LogPrint("prosperitynode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CProsperitynodePaymentDB::Ok) {
        LogPrint("prosperitynode","Error reading mnpayments.dat: ");
        if (readResult == CProsperitynodePaymentDB::IncorrectFormat)
            LogPrint("prosperitynode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("prosperitynode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("prosperitynode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(prosperitynodePayments);

    LogPrint("prosperitynode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("prosperitynode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    //LogPrintf("XX69----------> IsBlockValueValid(): nMinted: %d, nExpectedValue: %d\n", FormatMoney(nMinted), FormatMoney(nExpectedValue));

    if (!prosperitynodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            return nMinted <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;

    if (!prosperitynodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

	bool MasternodePayments = false;


    if(block.nTime > START_MASTERNODE_PAYMENTS) MasternodePayments = true;

    if(!IsMNSporkActive(MN_SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT)){
        MasternodePayments = false; //
        if(fDebug) LogPrintf("CheckBlock() : Masternode payment enforcement is off\n");
    }

    if(MasternodePayments)
    {
        LOCK2(cs_main, mempool.cs);

        CBlockIndex *pindex = chainActive.Tip();
        if(pindex != NULL){
            if(pindex->GetBlockHash() == block.hashPrevBlock){
                CAmount stakeReward = GetBlockValue(pindex->nHeight + 1);
                CAmount masternodePaymentAmount = GetMasternodePayment(pindex->nHeight+1, stakeReward, 0, false);//todo++

                bool fIsInitialDownload = IsInitialBlockDownload();

                // If we don't already have its previous block, skip masternode payment step
                if (!fIsInitialDownload && pindex != NULL)
                {
                    bool foundPaymentAmount = false;
                    bool foundPayee = false;
                    bool foundPaymentAndPayee = false;
                    CScript payee;
                    if(!masternodePayments.GetBlockPayee(pindex->nHeight+1, payee) || payee == CScript()){
                        foundPayee = true; //doesn't require a specific payee
                        foundPaymentAmount = true;
                        foundPaymentAndPayee = true;
                        LogPrintf("CheckBlock() : Using non-specific masternode payments %d\n", pindex->nHeight+1);
                    }
                    // todo-- must notice block.vtx[]. to block.vtx[]->
                    // Funtion no Intitial Download
                    for (unsigned int i = 0; i < block.vtx[1].vout.size(); i++) {
                        if(block.vtx[1].vout[i].nValue == masternodePaymentAmount ){
                            foundPaymentAmount = true;
                        }
                        if(block.vtx[1].vout[i].scriptPubKey == payee ){
                            foundPayee = true;
                        }
                        if(block.vtx[1].vout[i].nValue == masternodePaymentAmount && block.vtx[1].vout[i].scriptPubKey == payee){
                            foundPaymentAndPayee = true;
                        }
                    }

                    CTxDestination address1;
                    ExtractDestination(payee, address1);
                    CBitcoinAddress address2(address1);

                    if(!foundPaymentAndPayee) {
                        LogPrintf("CheckBlock() : !!Couldn't find masternode payment(%d|%d) or payee(%d|%s) nHeight %d. \n", foundPaymentAmount, masternodePaymentAmount, foundPayee, address2.ToString().c_str(), chainActive.Tip()->nHeight+1);
                        return false;//state.DoS(100, error("CheckBlock() : Couldn't find masternode payment or payee"));//todo++
                    } else {
                        LogPrintf("CheckBlock() : Found payment(%d|%d) or payee(%d|%s) nHeight %d. \n", foundPaymentAmount, masternodePaymentAmount, foundPayee, address2.ToString().c_str(), chainActive.Tip()->nHeight+1);
                    }
                } else {
                    LogPrintf("CheckBlock() : Is initial download, skipping masternode payment check %d\n", chainActive.Tip()->nHeight+1);
                }
            } else {
                LogPrintf("CheckBlock() : Skipping masternode payment check - nHeight %d Hash %s\n", chainActive.Tip()->nHeight+1, block.GetHash().ToString().c_str());
            }
        } else {
            LogPrintf("CheckBlock() : pindex is null, skipping masternode payment check\n");
        }
    } else {
        LogPrintf("CheckBlock() : skipping masternode payment checks\n");
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            transactionStatus = budget.IsTransactionValid(txNew, nBlockHeight);
            if (transactionStatus == TrxValidationStatus::Valid) {
                return true;
            }

            if (transactionStatus == TrxValidationStatus::InValid) {
                LogPrint("prosperitynode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (IsSporkActive(SPORK_9_PROSPERITYNODE_BUDGET_ENFORCEMENT))
                    return false;

                LogPrint("prosperitynode","Budget enforcement is disabled, accepting block\n");
            }
        }
    }

    // If we end here the transaction was either TrxValidationStatus::InValid and Budget enforcement is disabled, or
    // a double budget payment (status = TrxValidationStatus::DoublePayment) was detected, or no/not enough masternode
    // votes (status = TrxValidationStatus::VoteThreshold) for a finalized budget were found
    // In all cases a masternode will get the payment for this block

    //check for prosperitynode payee
    if (prosperitynodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint("prosperitynode","Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_8_PROSPERITYNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("prosperitynode","Prosperitynode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake, bool IsMasternode)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, fProofOfStake);
    } else {
        prosperitynodePayments.FillBlockPayee(txNew, nFees, fProofOfStake, IsMasternode);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return prosperitynodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CProsperitynodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake, bool IsMasternode)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if(! fProofOfStake && Params().NetworkID() == CBaseChainParams::TESTNET) { // for testnet
        txNew.vout[0].nValue = GetBlockValue(pindexPrev->nHeight);
        return;
    }

    bool hasPayment = true;
    bool hasMnPayment = true;
    CScript payee;
    CScript mn_payee;

    //spork
    if (!prosperitynodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no prosperitynode detected
        CProsperitynode* winningNode = mnodeman.GetCurrentFundamentalNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("prosperitynode","CreateNewBlock: Failed to detect prosperitynode to pay\n");
            hasPayment = false;
        }
    }

    //    bool bMasterNodePayment = false;

    //    if ( Params().NetworkID() == CBaseChainParams::TESTNET ){
    //        if (GetTimeMicros() > START_PROSPERITYNODE_PAYMENTS_TESTNET ){
    //            bMasterNodePayment = true;
    //        }
    //    }else{
    //        if (GetTimeMicros() > START_PROSPERITYNODE_PAYMENTS){
    //            bMasterNodePayment = true;
    //        }
    //    }//

    if(!masternodePayments.GetBlockPayee(pindexPrev->nHeight+1, mn_payee)){
        //no masternode detected
        CMasternode* winningNode = m_nodeman.GetCurrentMasterNode(1);
        if(winningNode){
            mn_payee = GetScriptForDestination(winningNode->pubkey.GetID());
        } else {
            LogPrintf("CreateNewBlock: Failed to detect masternode to pay\n");
            hasMnPayment = false;
        }
    }


    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
    CAmount prosperitynodePayment = GetProsperitynodePayment(pindexPrev->nHeight + 1, blockValue);
    CAmount masternodepayment = GetMasternodePayment(pindexPrev->nHeight +1 , blockValue, 0, false);

    //txNew.vout[0].nValue = blockValue;

    if (hasPayment) {
        if(IsMasternode && hasMnPayment){
            if (fProofOfStake) {
                /**For Proof Of Stake vout[0] must be null
                 * Stake reward can be split into many different outputs, so we must
                 * use vout.size() to align with several different cases.
                 * An additional output is appended as the prosperitynode payment
                 */
                unsigned int i = txNew.vout.size();
                txNew.vout.resize(i + 2);
                txNew.vout[i].scriptPubKey = payee;
                txNew.vout[i].nValue = prosperitynodePayment;
                txNew.vout[i + 1].scriptPubKey = mn_payee;
                txNew.vout[i + 1].nValue = masternodepayment;

                //subtract mn payment from the stake reward
                txNew.vout[i - 1].nValue -= (prosperitynodePayment + masternodepayment);
            } else {
                txNew.vout.resize(3);
                txNew.vout[2].scriptPubKey = mn_payee;
                txNew.vout[2].nValue = masternodepayment;
                txNew.vout[1].scriptPubKey = payee;
                txNew.vout[1].nValue = prosperitynodePayment;
                txNew.vout[0].nValue = blockValue - (prosperitynodePayment + masternodepayment);
            }

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("prosperitynode","Prosperitynode payment of %s to %s\n", FormatMoney(prosperitynodePayment).c_str(), address2.ToString().c_str());


            CTxDestination address3;
            ExtractDestination(mn_payee, address3);
            CBitcoinAddress address4(address3);

            LogPrint("masternode","Masternode payment of %s to %s\n", FormatMoney(masternodepayment).c_str(), address4.ToString().c_str());

        } else {

            if (fProofOfStake) {
                /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the prosperitynode payment
             */
                unsigned int i = txNew.vout.size();
                txNew.vout.resize(i + 1);
                txNew.vout[i].scriptPubKey = payee;
                txNew.vout[i].nValue = prosperitynodePayment;

                //subtract mn payment from the stake reward
                txNew.vout[i - 1].nValue -= prosperitynodePayment;
            } else {
                txNew.vout.resize(2);
                txNew.vout[1].scriptPubKey = payee;
                txNew.vout[1].nValue = prosperitynodePayment;
                txNew.vout[0].nValue = blockValue - prosperitynodePayment;
            }

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("prosperitynode","Prosperitynode payment of %s to %s\n", FormatMoney(prosperitynodePayment).c_str(), address2.ToString().c_str());
        }
    } else {

        if(IsMasternode && hasMnPayment){
            if (fProofOfStake) {
                /**For Proof Of Stake vout[0] must be null
                 * Stake reward can be split into many different outputs, so we must
                 * use vout.size() to align with several different cases.
                 * An additional output is appended as the prosperitynode payment
                 */
                unsigned int i = txNew.vout.size();
                txNew.vout.resize(i + 1);
                txNew.vout[i ].scriptPubKey = mn_payee;
                txNew.vout[i ].nValue = masternodepayment;

                //subtract mn payment from the stake reward
                txNew.vout[i - 1].nValue -= ( masternodepayment);
            } else {
                txNew.vout.resize(2);
                txNew.vout[1].scriptPubKey = mn_payee;
                txNew.vout[1].nValue = masternodepayment;

                txNew.vout[0].nValue = blockValue - ( masternodepayment);
            }


            CTxDestination address3;
            ExtractDestination(mn_payee, address3);
            CBitcoinAddress address4(address3);

            LogPrint("masternode","Masternode payment of %s to %s\n", FormatMoney(masternodepayment).c_str(), address4.ToString().c_str());



        } else {


            if (fProofOfStake) {
                /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the prosperitynode payment
             */

            } else {

                txNew.vout[0].nValue = blockValue ;
            }

        }

    }
}

int CProsperitynodePayments::GetMinProsperitynodePaymentsProto()
{
    if (IsSporkActive(SPORK_19_PROSPERITYNODE_PAY_UPDATED_NODES))
        return ActiveProtocol();                          // Allow only updated peers
    else
        return MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT; // Also allow old peers as long as they are allowed to run
}

void CProsperitynodePayments::ProcessMessageProsperitynodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!prosperitynodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Obfuscation/Prosperitynode related functionality


    if (strCommand == "fnget") { //Prosperitynode Payments Request Sync
        if (fLiteMode) return;   //disable all Obfuscation/Prosperitynode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("fnget")) {
                LogPrint("prosperitynode","fnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("fnget");
        prosperitynodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "fnget - Sent Prosperitynode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "fnw") { //Prosperitynode Payments Declare Winner
        //this is required in litemodef
        CProsperitynodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (prosperitynodePayments.mapProsperitynodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "fnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            prosperitynodeSync.AddedProsperitynodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "fnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("prosperitynode","fnw - invalid message - %s\n", strError);
            return;
        }

        if (!prosperitynodePayments.CanVote(winner.vinProsperitynode.prevout, winner.nBlockHeight)) {
            //  LogPrint("prosperitynode","fnw - prosperitynode already voted - %s\n", winner.vinProsperitynode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            // LogPrint("prosperitynode","fnw - invalid signature\n");
            if (prosperitynodeSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced prosperitynode
            mnodeman.AskForMN(pfrom, winner.vinProsperitynode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "fnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinProsperitynode.prevout.ToStringShort());

        if (prosperitynodePayments.AddWinningProsperitynode(winner)) {
            winner.Relay();
            prosperitynodeSync.AddedProsperitynodeWinner(winner.GetHash());
        }
    }
}

bool CProsperitynodePaymentWinner::Sign(CKey& keyProsperitynode, CPubKey& pubKeyProsperitynode)
{
    std::string errorMessage;
    std::string strFundamentalNodeSignMessage;

    std::string strMessage = vinProsperitynode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             payee.ToString();

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyProsperitynode)) {
        LogPrint("prosperitynode","CProsperitynodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyProsperitynode, vchSig, strMessage, errorMessage)) {
        LogPrint("prosperitynode","CProsperitynodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CProsperitynodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapProsperitynodeBlocks.count(nBlockHeight)) {
        return mapProsperitynodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this prosperitynode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CProsperitynodePayments::IsScheduled(CProsperitynode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapProsperitynodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapProsperitynodeBlocks.count(h)) {
            if (mapProsperitynodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CProsperitynodePayments::AddWinningProsperitynode(CProsperitynodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapProsperitynodePayeeVotes, cs_mapProsperitynodeBlocks);

        if (mapProsperitynodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapProsperitynodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapProsperitynodeBlocks.count(winnerIn.nBlockHeight)) {
            CProsperitynodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapProsperitynodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapProsperitynodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CProsperitynodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    int nProsperitynode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockValue(nBlockHeight);

    if (IsSporkActive(SPORK_8_PROSPERITYNODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of prosperitynodes by ignoring newly activated (< 8000 sec old) prosperitynodes
        nProsperitynode_Drift_Count = mnodeman.stable_size() + Params().ProsperitynodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same prosperitynode count. A allowance of being off our prosperitynode count is given
        //we only need to look at an increased prosperitynode count because as count increases, the reward decreases. This code only checks
        //for mnPayment >= required, so it only makes sense to check the max node count allowed.
        nProsperitynode_Drift_Count = mnodeman.size() + Params().ProsperitynodeCountDrift();
    }

    CAmount requiredProsperitynodePayment = GetProsperitynodePayment(nBlockHeight, nReward, nProsperitynode_Drift_Count);

    //require at least 6 signatures
    BOOST_FOREACH (CProsperitynodePayee& payee, vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH (CProsperitynodePayee& payee, vecPayments) {
        bool found = false;
        BOOST_FOREACH (CTxOut out, txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredProsperitynodePayment)
                    found = true;
                else
                    LogPrint("prosperitynode","Prosperitynode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredProsperitynodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint("prosperitynode","CProsperitynodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredProsperitynodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CProsperitynodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH (CProsperitynodePayee& payee, vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CProsperitynodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapProsperitynodeBlocks);

    if (mapProsperitynodeBlocks.count(nBlockHeight)) {
        return mapProsperitynodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CProsperitynodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapProsperitynodeBlocks);

    if (mapProsperitynodeBlocks.count(nBlockHeight)) {
        return mapProsperitynodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CProsperitynodePayments::CleanPaymentList()
{
    LOCK2(cs_mapProsperitynodePayeeVotes, cs_mapProsperitynodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CProsperitynodePaymentWinner>::iterator it = mapProsperitynodePayeeVotes.begin();
    while (it != mapProsperitynodePayeeVotes.end()) {
        CProsperitynodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CProsperitynodePayments::CleanPaymentList - Removing old Prosperitynode payment - block %d\n", winner.nBlockHeight);
            prosperitynodeSync.mapSeenSyncMNW.erase((*it).first);
            mapProsperitynodePayeeVotes.erase(it++);
            mapProsperitynodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CProsperitynodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CProsperitynode* pmn = mnodeman.Find(vinProsperitynode);

    if (!pmn) {
        strError = strprintf("Unknown Prosperitynode %s", vinProsperitynode.prevout.hash.ToString());
        LogPrint("prosperitynode","CProsperitynodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinProsperitynode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Prosperitynode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("prosperitynode","CProsperitynodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetProsperitynodeRank(vinProsperitynode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have prosperitynodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Prosperitynode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("prosperitynode","CProsperitynodePaymentWinner::IsValid - %s\n", strError);
            //if (prosperitynodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CProsperitynodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fFundamentalNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetProsperitynodeRank(activeProsperitynode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CProsperitynodePayments::ProcessBlock - Unknown Prosperitynode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CProsperitynodePayments::ProcessBlock - Prosperitynode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CProsperitynodePaymentWinner newWinner(activeProsperitynode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeProsperitynode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CProsperitynode* pmn = mnodeman.GetNextProsperitynodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() Failed to find prosperitynode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyProsperitynode;
    CKey keyProsperitynode;

    if (!obfuScationSigner.SetKey(strFundamentalNodePrivKey, errorMessage, keyProsperitynode, pubKeyProsperitynode)) {
        LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyProsperitynode, pubKeyProsperitynode)) {
        LogPrint("prosperitynode","CProsperitynodePayments::ProcessBlock() - AddWinningProsperitynode\n");

        if (AddWinningProsperitynode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CProsperitynodePaymentWinner::Relay()
{
    CInv inv(MSG_PROSPERITYNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CProsperitynodePaymentWinner::SignatureValid()
{
    CProsperitynode* pmn = mnodeman.Find(vinProsperitynode);

    if (pmn != NULL) {
        std::string strMessage = vinProsperitynode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 payee.ToString();

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pmn->pubKeyProsperitynode, vchSig, strMessage, errorMessage)) {
            return error("CProsperitynodePaymentWinner::SignatureValid() - Got bad Prosperitynode address signature %s\n", vinProsperitynode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CProsperitynodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapProsperitynodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CProsperitynodePaymentWinner>::iterator it = mapProsperitynodePayeeVotes.begin();
    while (it != mapProsperitynodePayeeVotes.end()) {
        CProsperitynodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_PROSPERITYNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", PROSPERITYNODE_SYNC_MNW, nInvCount);
}

std::string CProsperitynodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapProsperitynodePayeeVotes.size() << ", Blocks: " << (int)mapProsperitynodeBlocks.size();

    return info.str();
}


int CProsperitynodePayments::GetOldestBlock()
{
    LOCK(cs_mapProsperitynodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CProsperitynodeBlockPayees>::iterator it = mapProsperitynodeBlocks.begin();
    while (it != mapProsperitynodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CProsperitynodePayments::GetNewestBlock()
{
    LOCK(cs_mapProsperitynodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CProsperitynodeBlockPayees>::iterator it = mapProsperitynodeBlocks.begin();
    while (it != mapProsperitynodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
