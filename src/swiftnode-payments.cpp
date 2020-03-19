// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018-2020 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activeswiftnode.h"
#include "swiftnode-payments.h"
#include "addrman.h"
#include "swiftnode-budget.h"
#include "swiftnode-sync.h"
#include "swiftnodeman.h"
#include "swiftnode-helpers.h"
#include "swiftnodeconfig.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CSwiftnodePayments swiftnodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapSwiftnodeBlocks;
CCriticalSection cs_mapSwiftnodePayeeVotes;

//
// CSwiftnodePaymentDB
//

CSwiftnodePaymentDB::CSwiftnodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "SwiftnodePayments";
}

bool CSwiftnodePaymentDB::Write(const CSwiftnodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // swiftnode cache file specific magic message
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
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("swiftnode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CSwiftnodePaymentDB::ReadResult CSwiftnodePaymentDB::Read(CSwiftnodePayments& objToLoad, bool fDryRun)
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
    } catch (std::exception& e) {
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
        // de-serialize file header (swiftnode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid swiftnode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CSwiftnodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("swiftnode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("swiftnode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("swiftnode","Swiftnode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("swiftnode","Swiftnode payments manager - result:\n");
        LogPrint("swiftnode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpSwiftnodePayments()
{
    int64_t nStart = GetTimeMillis();

    CSwiftnodePaymentDB paymentdb;
    CSwiftnodePayments tempPayments;

    LogPrint("swiftnode","Verifying mnpayments.dat format...\n");
    CSwiftnodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CSwiftnodePaymentDB::FileError)
        LogPrint("swiftnode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CSwiftnodePaymentDB::Ok) {
        LogPrint("swiftnode","Error reading mnpayments.dat: ");
        if (readResult == CSwiftnodePaymentDB::IncorrectFormat)
            LogPrint("swiftnode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("swiftnode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("swiftnode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(swiftnodePayments);

    LogPrint("swiftnode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted, CAmount nBudgetValue)
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
        LogPrint("swiftnode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    if (!swiftnodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    } else { // we're synced and have data so check the budget schedule

        if (!IsSporkActive(SPORK_11_SUPERBLOCKS)) {
            return nMinted <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            return nMinted <= nExpectedValue + nBudgetValue;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight, CAmount& nBudgetPaid)
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;

    if (!swiftnodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (IsSporkActive(SPORK_11_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            transactionStatus = budget.IsTransactionValid(txNew, nBlockHeight, nBudgetPaid);
            if (transactionStatus == TrxValidationStatus::Valid)
                return true;

            if (transactionStatus == TrxValidationStatus::InValid) {
                LogPrint("swiftnode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (IsSporkActive(SPORK_5_SWIFTNODE_BUDGET_ENFORCEMENT))
                    return false;

                LogPrint("swiftnode","Budget enforcement is disabled, accepting block\n");
            }
        }
    }

    // If we end here the transaction was either TrxValidationStatus::InValid and Budget enforcement is disabled, or
    // a double budget payment (status = TrxValidationStatus::DoublePayment) was detected, or no/not enough swiftnode
    // votes (status = TrxValidationStatus::VoteThreshold) for a finalized budget were found
    // In all cases a swiftnode will get the payment for this block

    //check for swiftnode payee
    if (swiftnodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint("swiftnode","Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("swiftnode","Swiftnode payment enforcement is disabled, accepting block\n");

    return true;
}

void FillLotteryPayees(CBlockIndex* pindexPrev, CMutableTransaction& txNew)
{
    if ((pindexPrev->nHeight % nDrawBlocks) == 0) {
        vector<string> winners = {};
        if (HaveLotteryWinners(pindexPrev, winners)) {
            CScript scriptPubKey1 = GetScriptForDestination(CBitcoinAddress(winners[0]).Get());
            CScript scriptPubKey2 = GetScriptForDestination(CBitcoinAddress(winners[1]).Get());
            CScript scriptPubKey3 = GetScriptForDestination(CBitcoinAddress(winners[2]).Get());

            int i = txNew.vout.size();
            txNew.vout.resize(i+3);

            txNew.vout[i].scriptPubKey = scriptPubKey1;
            txNew.vout[i].nValue = pindexPrev->nLotteryJackpot*0.6;
            LogPrintf("FillLotteryPayees(): winner1=%s, nAmount=%d\n", winners[0], txNew.vout[i].nValue);

            txNew.vout[i+1].scriptPubKey = scriptPubKey2;
            txNew.vout[i+1].nValue = pindexPrev->nLotteryJackpot*0.3;
            LogPrintf("FillLotteryPayees(): winner2=%s, nAmount=%d\n", winners[1], txNew.vout[i+1].nValue);

            txNew.vout[i+2].scriptPubKey = scriptPubKey3;
            txNew.vout[i+2].nValue = pindexPrev->nLotteryJackpot*0.1;
            LogPrintf("FillLotteryPayees(): winner3=%s, nAmount=%d\n", winners[2], txNew.vout[i+2].nValue);
        }
    }
}

void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_11_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, fProofOfStake);
    } else {
        swiftnodePayments.FillBlockPayee(txNew, nFees, fProofOfStake);
    }

    FillLotteryPayees(pindexPrev, txNew);
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_11_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return swiftnodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CSwiftnodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!swiftnodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no swiftnode detected
        CSwiftnode* winningNode = mnodeman.GetCurrentSwiftNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("swiftnode","CreateNewBlock: Failed to detect swiftnode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
    CAmount swiftnodePayment = GetSwiftnodePayment(pindexPrev->nHeight, blockValue);

    if (hasPayment) {
        if (fProofOfStake) {
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the swiftnode payment
             */
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = swiftnodePayment;

            //subtract mn payment from the stake reward
            txNew.vout[i - 1].nValue -= swiftnodePayment;
        } else {
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = swiftnodePayment;
            txNew.vout[0].nValue = blockValue - swiftnodePayment;
        }

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("swiftnode","Swiftnode payment of %s to %s\n", FormatMoney(swiftnodePayment).c_str(), address2.ToString().c_str());
    }
}

int CSwiftnodePayments::GetMinSwiftnodePaymentsProto()
{
    if (IsSporkActive(SPORK_8_SWIFTNODE_PAY_UPDATED_NODES))
        return ActiveProtocol();                          // Allow only updated peers
    else
        return MIN_PEER_PROTO_VERSION; // Also allow old peers as long as they are allowed to run
}

void CSwiftnodePayments::ProcessMessageSwiftnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!swiftnodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Swiftnode related functionality


    if (strCommand == "mnget") { //Swiftnode Payments Request Sync
        if (fLiteMode) return;   //disable all Swiftnode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("mnget")) {
                LogPrintf("CSwiftnodePayments::ProcessMessageSwiftnodePayments() : mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("mnget");
        swiftnodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Swiftnode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Swiftnode Payments Declare Winner
        //this is required in litemodef
        CSwiftnodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (swiftnodePayments.mapSwiftnodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            swiftnodeSync.AddedSwiftnodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("swiftnode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!swiftnodePayments.CanVote(winner.vinSwiftnode.prevout, winner.nBlockHeight)) {
            //  LogPrint("swiftnode","mnw - swiftnode already voted - %s\n", winner.vinSwiftnode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            if (swiftnodeSync.IsSynced()) {
                LogPrintf("CSwiftnodePayments::ProcessMessageSwiftnodePayments() : mnw - invalid signature\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced swiftnode
            mnodeman.AskForMN(pfrom, winner.vinSwiftnode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinSwiftnode.prevout.ToStringShort());

        if (swiftnodePayments.AddWinningSwiftnode(winner)) {
            winner.Relay();
            swiftnodeSync.AddedSwiftnodeWinner(winner.GetHash());
        }
    }
}

bool CSwiftnodePaymentWinner::Sign(CKey& keySwiftnode, CPubKey& pubKeySwiftnode)
{
    std::string errorMessage;
    std::string strSwiftNodeSignMessage;

    std::string strMessage = vinSwiftnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             payee.ToString();

    if (!swiftnodeSigner.SignMessage(strMessage, errorMessage, vchSig, keySwiftnode)) {
        LogPrint("swiftnode","CSwiftnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!swiftnodeSigner.VerifyMessage(pubKeySwiftnode, vchSig, strMessage, errorMessage)) {
        LogPrint("swiftnode","CSwiftnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CSwiftnodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapSwiftnodeBlocks.count(nBlockHeight)) {
        return mapSwiftnodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this swiftnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CSwiftnodePayments::IsScheduled(CSwiftnode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapSwiftnodeBlocks);

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
        if (mapSwiftnodeBlocks.count(h)) {
            if (mapSwiftnodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CSwiftnodePayments::AddWinningSwiftnode(CSwiftnodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100))
        return false;

    {
        LOCK2(cs_mapSwiftnodePayeeVotes, cs_mapSwiftnodeBlocks);

        if (mapSwiftnodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapSwiftnodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapSwiftnodeBlocks.count(winnerIn.nBlockHeight)) {
            CSwiftnodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapSwiftnodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapSwiftnodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CSwiftnodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    int nSwiftnode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockValue(nBlockHeight - 1);

    if (IsSporkActive(SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of swiftnodes by ignoring newly activated (< 8000 sec old) swiftnodes
        nSwiftnode_Drift_Count = mnodeman.stable_size() + Params().SwiftnodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same swiftnode count. A allowance of being off our swiftnode count is given
        //we only need to look at an increased swiftnode count because as count increases, the reward decreases. This code only checks
        //for mnPayment >= required, so it only makes sense to check the max node count allowed.
        nSwiftnode_Drift_Count = mnodeman.size() + Params().SwiftnodeCountDrift();
    }

    CAmount requiredSwiftnodePayment = GetSwiftnodePayment(nBlockHeight, nReward, nSwiftnode_Drift_Count);

    //require at least 6 signatures
    BOOST_FOREACH (CSwiftnodePayee& payee, vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH (CSwiftnodePayee& payee, vecPayments) {
        bool found = false;
        BOOST_FOREACH (CTxOut out, txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredSwiftnodePayment)
                    found = true;
                else
                    LogPrint("swiftnode","Swiftnode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredSwiftnodePayment).c_str());
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

    LogPrint("swiftnode","CSwiftnodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredSwiftnodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CSwiftnodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CSwiftnodePayee& payee : vecPayments) {
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

std::string CSwiftnodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapSwiftnodeBlocks);

    if (mapSwiftnodeBlocks.count(nBlockHeight)) {
        return mapSwiftnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CSwiftnodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapSwiftnodeBlocks);

    if (mapSwiftnodeBlocks.count(nBlockHeight)) {
        return mapSwiftnodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CSwiftnodePayments::CleanPaymentList()
{
    LOCK2(cs_mapSwiftnodePayeeVotes, cs_mapSwiftnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CSwiftnodePaymentWinner>::iterator it = mapSwiftnodePayeeVotes.begin();
    while (it != mapSwiftnodePayeeVotes.end()) {
        CSwiftnodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CSwiftnodePayments::CleanPaymentList - Removing old Swiftnode payment - block %d\n", winner.nBlockHeight);
            swiftnodeSync.mapSeenSyncMNW.erase((*it).first);
            mapSwiftnodePayeeVotes.erase(it++);
            mapSwiftnodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CSwiftnodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CSwiftnode* pmn = mnodeman.Find(vinSwiftnode);

    if (!pmn) {
        strError = strprintf("Unknown Swiftnode %s", vinSwiftnode.prevout.hash.ToString());
        LogPrint("swiftnode","CSwiftnodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinSwiftnode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Swiftnode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("swiftnode","CSwiftnodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetSwiftnodeRank(vinSwiftnode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have swiftnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Swiftnode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("swiftnode","CSwiftnodePaymentWinner::IsValid - %s\n", strError);
            //if (swiftnodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CSwiftnodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fSwiftNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetSwiftnodeRank(activeSwiftnode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CSwiftnodePayments::ProcessBlock - Unknown Swiftnode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CSwiftnodePayments::ProcessBlock - Swiftnode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CSwiftnodePaymentWinner newWinner(activeSwiftnode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeSwiftnode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CSwiftnode* pmn = mnodeman.GetNextSwiftnodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() Failed to find swiftnode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeySwiftnode;
    CKey keySwiftnode;

    if (!swiftnodeSigner.SetKey(strSwiftNodePrivKey, errorMessage, keySwiftnode, pubKeySwiftnode)) {
        LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keySwiftnode, pubKeySwiftnode)) {
        LogPrint("swiftnode","CSwiftnodePayments::ProcessBlock() - AddWinningSwiftnode\n");

        if (AddWinningSwiftnode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CSwiftnodePaymentWinner::Relay()
{
    CInv inv(MSG_SWIFTNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CSwiftnodePaymentWinner::SignatureValid()
{
    CSwiftnode* pmn = mnodeman.Find(vinSwiftnode);

    if (pmn != NULL) {
        std::string strMessage = vinSwiftnode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 payee.ToString();

        std::string errorMessage = "";
        if (!swiftnodeSigner.VerifyMessage(pmn->pubKeySwiftnode, vchSig, strMessage, errorMessage)) {
            return error("CSwiftnodePaymentWinner::SignatureValid() - Got bad Swiftnode address signature %s\n", vinSwiftnode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CSwiftnodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapSwiftnodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CSwiftnodePaymentWinner>::iterator it = mapSwiftnodePayeeVotes.begin();
    while (it != mapSwiftnodePayeeVotes.end()) {
        CSwiftnodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_SWIFTNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", SWIFTNODE_SYNC_MNW, nInvCount);
}

std::string CSwiftnodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapSwiftnodePayeeVotes.size() << ", Blocks: " << (int)mapSwiftnodeBlocks.size();

    return info.str();
}


int CSwiftnodePayments::GetOldestBlock()
{
    LOCK(cs_mapSwiftnodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CSwiftnodeBlockPayees>::iterator it = mapSwiftnodeBlocks.begin();
    while (it != mapSwiftnodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CSwiftnodePayments::GetNewestBlock()
{
    LOCK(cs_mapSwiftnodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CSwiftnodeBlockPayees>::iterator it = mapSwiftnodeBlocks.begin();
    while (it != mapSwiftnodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
