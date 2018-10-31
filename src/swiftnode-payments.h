// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTNODE_PAYMENTS_H
#define SWIFTNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "swiftnode.h"
#include "clientversion.h"

#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapSwiftnodeBlocks;
extern CCriticalSection cs_mapSwiftnodePayeeVotes;

class CSwiftnodePayments;
class CSwiftnodePaymentWinner;
class CSwiftnodeBlockPayees;

extern CSwiftnodePayments swiftnodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageSwiftnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake);

void DumpSwiftnodePayments();

/** Save Swiftnode Payment Data (mnpayments.dat)
 */
class CSwiftnodePaymentDB
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

    CSwiftnodePaymentDB();
    bool Write(const CSwiftnodePayments& objToSave);
    ReadResult Read(CSwiftnodePayments& objToLoad, bool fDryRun = false);
};

class CSwiftnodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CSwiftnodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CSwiftnodePayee(CScript payee, int nVotesIn)
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

// Keep track of votes for payees from swiftnodes
class CSwiftnodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CSwiftnodePayee> vecPayments;

    CSwiftnodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CSwiftnodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        for (CSwiftnodePayee& payee : vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CSwiftnodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        for (CSwiftnodePayee& p : vecPayments) {
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

        for (CSwiftnodePayee& p : vecPayments) {
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
class CSwiftnodePaymentWinner
{
public:
    CTxIn vinSwiftnode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CSwiftnodePaymentWinner()
    {
        nBlockHeight = 0;
        vinSwiftnode = CTxIn();
        payee = CScript();
    }

    CSwiftnodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinSwiftnode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinSwiftnode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keySwiftnode, CPubKey& pubKeySwiftnode);
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
        READWRITE(vinSwiftnode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinSwiftnode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Swiftnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CSwiftnodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CSwiftnodePaymentWinner> mapSwiftnodePayeeVotes;
    std::map<int, CSwiftnodeBlockPayees> mapSwiftnodeBlocks;
    std::map<uint256, int> mapSwiftnodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CSwiftnodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapSwiftnodeBlocks, cs_mapSwiftnodePayeeVotes);
        mapSwiftnodeBlocks.clear();
        mapSwiftnodePayeeVotes.clear();
    }

    bool AddWinningSwiftnode(CSwiftnodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CSwiftnode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CSwiftnode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outSwiftnode, int nBlockHeight)
    {
        LOCK(cs_mapSwiftnodePayeeVotes);

        if (mapSwiftnodesLastVote.count(outSwiftnode.hash + outSwiftnode.n)) {
            if (mapSwiftnodesLastVote[outSwiftnode.hash + outSwiftnode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this swiftnode voted
        mapSwiftnodesLastVote[outSwiftnode.hash + outSwiftnode.n] = nBlockHeight;
        return true;
    }

    int GetMinSwiftnodePaymentsProto();
    void ProcessMessageSwiftnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapSwiftnodePayeeVotes);
        READWRITE(mapSwiftnodeBlocks);
    }
};


#endif
