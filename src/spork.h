// Copyright (c) 2014-2016 Dash developers
// Copyright (c) 2016-2017 PIVX developers
// Copyright (c) 2018-2020 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#include "protocol.h"
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
#define SPORK_START 10001
#define SPORK_END 10014

#define SPORK_1_SWIFTTX 10001
#define SPORK_2_SWIFTTX_BLOCK_FILTERING 10002
#define SPORK_3_MAX_VALUE 10003
#define SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT 10004
#define SPORK_5_SWIFTNODE_BUDGET_ENFORCEMENT 10005
#define SPORK_8_SWIFTNODE_PAY_UPDATED_NODES 10008
#define SPORK_10_RECONSIDER_BLOCKS 10010
#define SPORK_11_SUPERBLOCKS 10011
#define SPORK_12_NEW_PROTOCOL_ENFORCEMENT 10012
#define SPORK_13_HODLDEPOSITS 10013
#define SPORK_14_LOTTERIES 10014


#define SPORK_1_SWIFTTX_DEFAULT 978307200				//ON
#define SPORK_2_SWIFTTX_BLOCK_FILTERING_DEFAULT 1424217600		//ON
#define SPORK_3_MAX_VALUE_DEFAULT 10000					//10,000 SWIFT
#define SPORK_4_SWIFTNODE_PAYMENT_ENFORCEMENT_DEFAULT 8580808800	//OFF
#define SPORK_5_SWIFTNODE_BUDGET_ENFORCEMENT_DEFAULT 8580808800		//OFF
#define SPORK_8_SWIFTNODE_PAY_UPDATED_NODES_DEFAULT 8580808800		//OFF
#define SPORK_10_RECONSIDER_BLOCKS_DEFAULT 0				//OFF
#define SPORK_11_SUPERBLOCKS_DEFAULT 8580808800				//OFF
#define SPORK_12_NEW_PROTOCOL_ENFORCEMENT_DEFAULT 8580808800		//OFF
#define SPORK_13_HODLDEPOSITS_DEFAULT 10013				//ON
#define SPORK_14_LOTTERIES_DEFAULT 10014				//ON

class CSporkMessage;
class CSporkManager;

extern std::map<uint256, CSporkMessage> mapSporks;
extern std::map<int, CSporkMessage> mapSporksActive;
extern CSporkManager sporkManager;

void LoadSporksFromDB();
void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
int64_t GetSporkValue(int nSporkID);
bool IsSporkActive(int nSporkID);
void ExecuteSpork(int nSporkID, int nValue);
void ReprocessBlocks(int nBlocks);

//
// Spork Class
// Keeps track of all of the network spork settings
//

class CSporkMessage
{
public:
    std::vector<unsigned char> vchSig;
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;

    uint256 GetHash()
    {
        uint256 n = Hash(BEGIN(nSporkID), END(nTimeSigned));
        return n;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
    }
};


class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;
    std::string strMasterPrivKey;

public:
    CSporkManager()
    {
    }

    std::string GetSporkNameByID(int id);
    int GetSporkIDByName(std::string strName);
    bool UpdateSpork(int nSporkID, int64_t nValue);
    bool SetPrivKey(std::string strPrivKey);
    bool CheckSignature(CSporkMessage& spork);
    bool Sign(CSporkMessage& spork);
    void Relay(CSporkMessage& msg);
};

#endif
