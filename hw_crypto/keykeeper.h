// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "kdf.h"
#include "coinid.h"
#include "sign.h"

typedef struct
{
	BeamCrypto_Kdf m_MasterKey;

	int m_AllowWeakInputs;

	// TODO: state, Slot management, etc.

} BeamCrypto_KeyKeeper;

void BeamCrypto_SecureEraseMem(void*, uint32_t);

uint32_t BeamCrypto_KeyKeeper_getNumSlots();
void BeamCrypto_KeyKeeper_ReadSlot(uint32_t, BeamCrypto_UintBig*);
void BeamCrypto_KeyKeeper_RegenerateSlot(uint32_t);

typedef struct
{
	BeamCrypto_UintBig m_Secret;
	BeamCrypto_CompactPoint m_CoFactorG;
	BeamCrypto_CompactPoint m_CoFactorJ;

} BeamCrypto_KdfPub;

void BeamCrypto_KeyKeeper_GetPKdf(const BeamCrypto_KeyKeeper*, BeamCrypto_KdfPub*, const uint32_t* pChild); // if pChild is NULL then the master kdfpub (owner key) is returned

typedef uint64_t BeamCrypto_Height;
typedef uint64_t BeamCrypto_WalletIdentity;

typedef struct
{
	BeamCrypto_Amount m_Fee;
	BeamCrypto_Height m_hMin;
	BeamCrypto_Height m_hMax;

	BeamCrypto_CompactPoint m_Commitment;
	BeamCrypto_Signature m_Signature;

} BeamCrypto_TxKernel;

void BeamCrypto_TxKernel_getID(const BeamCrypto_TxKernel*, BeamCrypto_UintBig* pMsg);
int BeamCrypto_TxKernel_IsValid(const BeamCrypto_TxKernel*);

typedef struct
{
	BeamCrypto_UintBig m_Sender;
	BeamCrypto_UintBig m_pMessage[2];

} BeamCrypto_ShieldedTxoUser;

typedef struct
{
	// ticket source params
	BeamCrypto_UintBig m_kSerG;
	uint32_t m_nViewerIdx;
	uint8_t m_IsCreatedByViewer;

	// sender params
	BeamCrypto_ShieldedTxoUser m_User;
	BeamCrypto_Amount m_Amount;
	BeamCrypto_AssetID m_AssetID;

} BeamCrypto_ShieldedTxoID;

typedef struct
{
	BeamCrypto_ShieldedTxoID m_TxoID;
	BeamCrypto_Amount m_Fee;

} BeamCrypto_ShieldedInput;

typedef struct
{
	const BeamCrypto_CoinID* m_pIns;
	const BeamCrypto_CoinID* m_pOuts;
	const BeamCrypto_ShieldedInput* m_pInsShielded;
	unsigned int m_Ins;
	unsigned int m_Outs;
	unsigned int m_InsShielded;

	BeamCrypto_TxKernel m_Krn;
	BeamCrypto_UintBig m_kOffset;

} BeamCrypto_TxCommon;

#define BeamCrypto_KeyKeeper_Status_Ok 0
#define BeamCrypto_KeyKeeper_Status_Unspecified 1
#define BeamCrypto_KeyKeeper_Status_UserAbort 2
#define BeamCrypto_KeyKeeper_Status_NotImpl 3

#define BeamCrypto_KeyKeeper_Status_ProtoError 10

// Split tx, no value transfer. Only fee is spent (hence the user agreement is required)
int BeamCrypto_KeyKeeper_SignTx_Split(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*);

typedef struct
{
	BeamCrypto_UintBig m_Peer;
	BeamCrypto_WalletIdentity m_MyIDKey;
	BeamCrypto_Signature m_PaymentProofSignature;

} BeamCrypto_TxMutualInfo;

int BeamCrypto_KeyKeeper_SignTx_Receive(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*, BeamCrypto_TxMutualInfo*);

typedef struct
{
	uint32_t m_iSlot;
	BeamCrypto_UintBig m_UserAgreement; // set to Zero on 1st invocation

} BeamCrypto_TxSenderParams;

int BeamCrypto_KeyKeeper_SignTx_Send(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*, BeamCrypto_TxMutualInfo*, BeamCrypto_TxSenderParams*);

typedef struct
{
	// ticket
	BeamCrypto_CompactPoint m_SerialPub;
	BeamCrypto_CompactPoint m_NoncePub;
	BeamCrypto_UintBig m_pK[2];

	BeamCrypto_UintBig m_SharedSecret;
	BeamCrypto_Signature m_Signature;

} BeamCrypto_ShieldedVoucher;

#pragma pack (push, 1)
typedef struct
{
	// packed into 674 bytes, serialized the same way
	BeamCrypto_UintBig m_Ax;
	BeamCrypto_UintBig m_Sx;
	BeamCrypto_UintBig m_T1x;
	BeamCrypto_UintBig m_T2x;
	BeamCrypto_UintBig m_Taux;
	BeamCrypto_UintBig m_Mu;
	BeamCrypto_UintBig m_tDot;
	BeamCrypto_UintBig m_pLRx[6][2];
	BeamCrypto_UintBig m_pCondensed[2];
	uint8_t m_pYs[2];

} BeamCrypto_RangeProof_Packed;
#pragma pack (pop)

typedef struct
{
	BeamCrypto_ShieldedVoucher m_Voucher;
	BeamCrypto_UintBig m_Receiver; // recipient
	BeamCrypto_WalletIdentity m_MyIDKey; // set to nnz if sending to yourself
	uint8_t m_HideAssetAlways; // important to specify, this affects expected blinding factor recovery
	BeamCrypto_RangeProof_Packed m_RangeProof;

	// ShieldedTxo::User
	BeamCrypto_UintBig m_Sender; // right now - can be set to arbitrary data
	BeamCrypto_UintBig m_pMessage[2];

	// sent value and asset are derived from the tx balance (ins - outs)

} BeamCrypto_TxSendShieldedParams;

int BeamCrypto_KeyKeeper_SignTx_SendShielded(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*, const BeamCrypto_TxSendShieldedParams*);

//////////////////
// Protocol
#define BeamCrypto_CurrentProtoVer 1

#define BeamCrypto_ProtoRequest_Version(macro)
#define BeamCrypto_ProtoResponse_Version(macro) \
	macro(uint32_t, Value)

#define BeamCrypto_ProtoRequest_GetPKdf(macro) \
	macro(uint8_t, Root) \
	macro(uint32_t, iChild)

#define BeamCrypto_ProtoResponse_GetPKdf(macro) \
	macro(BeamCrypto_KdfPub, Value)

#define BeamCrypto_ProtoRequest_CreateOutput(macro) \
	macro(BeamCrypto_CoinID, Cid) \
	macro(BeamCrypto_UintBig, pKExtra[2]) \
	macro(BeamCrypto_CompactPoint, pT[2]) \

#define BeamCrypto_ProtoResponse_CreateOutput(macro) \
	macro(BeamCrypto_CompactPoint, pT[2]) \
	macro(BeamCrypto_UintBig, TauX) \

#define BeamCrypto_ProtoRequest_CreateShieldedInput(macro) \
	macro(BeamCrypto_ShieldedInput, Inp) \
	macro(BeamCrypto_Height, hMin) \
	macro(BeamCrypto_Height, hMax) \
	macro(uint64_t, WindowEnd) \
	macro(uint32_t, Sigma_M) \
	macro(uint32_t, Sigma_n) \
	macro(BeamCrypto_UintBig, AssetSk) /* negated blinding for asset generator (H` = H - assetSk*G) */ \
	macro(BeamCrypto_UintBig, OutpSk) /* The overall blinding factor of the shielded Txo (not secret) */ \
	macro(BeamCrypto_CompactPoint, pABCD[4]) \
	/* followed by BeamCrypto_CompactPoint* pG[] */

#define BeamCrypto_ProtoResponse_CreateShieldedInput(macro) \
	macro(BeamCrypto_CompactPoint, G0) \
	macro(BeamCrypto_CompactPoint, NoncePub) \
	macro(BeamCrypto_UintBig, pSig[2]) \
	macro(BeamCrypto_UintBig, zR)

#define BeamCrypto_ProtoRequest_CreateShieldedVouchers(macro) \
	macro(uint32_t, Count) \
	macro(BeamCrypto_WalletIdentity, nMyIDKey) \
	macro(BeamCrypto_UintBig, Nonce0) \

#define BeamCrypto_ProtoResponse_CreateShieldedVouchers(macro) \
	macro(uint32_t, Count) \
	/* followed by BeamCrypto_ShieldedVoucher[] */

#define BeamCrypto_ProtoMethods(macro) \
	macro(0x01, Version) \
	macro(0x02, GetPKdf) \
	macro(0x10, CreateOutput) \
	macro(0x21, CreateShieldedInput) \
	macro(0x22, CreateShieldedVouchers) \

int BeamCrypto_KeyKeeper_Invoke(const BeamCrypto_KeyKeeper*, uint8_t* pIn, uint32_t nIn, uint8_t* pOut, uint32_t nOut);
