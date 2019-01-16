// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTNODE_H
#define SWIFTNODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#define SWIFTNODE_MIN_CONFIRMATIONS 20
#define SWIFTNODE_MIN_MNP_SECONDS (10 * 60)
#define SWIFTNODE_MIN_MNB_SECONDS (5 * 60)
#define SWIFTNODE_PING_SECONDS (5 * 60)
#define SWIFTNODE_EXPIRATION_SECONDS (120 * 60)
#define SWIFTNODE_REMOVAL_SECONDS (180 * 60)
#define SWIFTNODE_CHECK_SECONDS 5

#define SWIFTNODE_COLLATERAL 50000

using namespace std;

class CSwiftnode;
class CSwiftnodeBroadcast;
class CSwiftnodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Swiftnode Ping Class : Contains a different serialize method for sending pings from swiftnodes throughout the network
//

class CSwiftnodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CSwiftnodePing();
    CSwiftnodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    bool Sign(CKey& keySwiftnode, CPubKey& pubKeySwiftnode);
    bool VerifySignature(CPubKey& pubKeySwiftnode, int &nDos);
    void Relay();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CSwiftnodePing& first, CSwiftnodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CSwiftnodePing& operator=(CSwiftnodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CSwiftnodePing& a, const CSwiftnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CSwiftnodePing& a, const CSwiftnodePing& b)
    {
        return !(a == b);
    }
};

//
// The Swiftnode Class. It contains the input of the SWIFT collateral, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CSwiftnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;

public:
    enum state {
        SWIFTNODE_PRE_ENABLED,
        SWIFTNODE_ENABLED,
        SWIFTNODE_EXPIRED,
        SWIFTNODE_OUTPOINT_SPENT,
        SWIFTNODE_REMOVE,
        SWIFTNODE_WATCHDOG_EXPIRED,
        SWIFTNODE_POSE_BAN,
        SWIFTNODE_VIN_SPENT,
        SWIFTNODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeySwiftnode;
    CPubKey pubKeyCollateralAddress1;
    CPubKey pubKeySwiftnode1;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CSwiftnodePing lastPing;

    int64_t nLastDsee;  // temporary, do not save. Remove after migration to v12
    int64_t nLastDseep; // temporary, do not save. Remove after migration to v12

    CSwiftnode();
    CSwiftnode(const CSwiftnode& other);
    CSwiftnode(const CSwiftnodeBroadcast& mnb);

    void swap(CSwiftnode& first, CSwiftnode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeySwiftnode, second.pubKeySwiftnode);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CSwiftnode& operator=(CSwiftnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CSwiftnode& a, const CSwiftnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CSwiftnode& a, const CSwiftnode& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod = 1, int64_t nBlockHeight = 0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeySwiftnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(cacheInputAge);
        READWRITE(cacheInputAgeBlock);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nLastDsq);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CSwiftnodeBroadcast& mnb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash + slice * 64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CSwiftnodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CSwiftnodePing();
    }

    bool IsEnabled()
    {
        return activeState == SWIFTNODE_ENABLED;
    }

    int GetSwiftnodeInputAge()
    {
        if (chainActive.Tip() == NULL) return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
    }

    std::string GetStatus();

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Swiftnode Broadcast Class : Contains a different serialize method for sending swiftnodes through the network
//

class CSwiftnodeBroadcast : public CSwiftnode
{
public:
    CSwiftnodeBroadcast();
    CSwiftnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CSwiftnodeBroadcast(const CSwiftnode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    bool VerifySignature();
    void Relay();
    std::string GetStrMessage();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeySwiftnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubKeyCollateralAddress;
        return ss.GetHash();
    }

    /// Create Swiftnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keySwiftnodeNew, CPubKey pubKeySwiftnodeNew, std::string& strErrorRet, CSwiftnodeBroadcast& mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CSwiftnodeBroadcast& mnbRet, bool fOffline = false);
    static bool CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext);
};

#endif
