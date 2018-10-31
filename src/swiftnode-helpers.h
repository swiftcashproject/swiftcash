// Copyright (c) 2014-2015 Dash developers
// Copyright (c) 2015-2018 PIVX developers
// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTNODEHELPERS_H
#define SWIFTNODEHELPERS_H

#include "main.h"
#include "sync.h"
#include "base58.h"

/** Helper object for signing and checking signatures
 */
class CSwiftnodeSigner
{
public:
    CScript collateralPubKey;

    /// Is the inputs associated with this public key? (and there is SWIFT collateral - checking if valid swiftnode)
    bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
    /// Set the private/public key values, returns true if successful
    bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Set the private/public key values, returns true if successful
    bool SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
    /// Sign the message, returns true if successful
    bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    /// Verify the message, returns true if succcessful
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);

    bool SetCollateralAddress(std::string strAddress);

    void InitCollateralAddress()
    {
        SetCollateralAddress(Params().SwiftnodePoolDummyAddress());
    }

};

void ThreadSwiftnodePool();

extern CSwiftnodeSigner swiftnodeSigner;


#endif
