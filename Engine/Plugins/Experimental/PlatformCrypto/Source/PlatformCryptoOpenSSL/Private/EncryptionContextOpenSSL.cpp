// Copyright Epic Games, Inc. All Rights Reserved.

#include "EncryptionContextOpenSSL.h"
#include "PlatformCryptoOpenSSLTypes.h"
#include "PlatformCryptoAesEncryptorsOpenSSL.h"
#include "PlatformCryptoAesDecryptorsOpenSSL.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/err.h>
THIRD_PARTY_INCLUDES_END

#include "Misc/AssertionMacros.h"

DEFINE_LOG_CATEGORY(LogPlatformCryptoOpenSSL);

struct FSHA256HasherOpenSSL::FImplDetails
{
	SHA256_CTX Ctx;
	enum class EState { NeedsInit, Ready, ReadyNeedsFinalize } State = EState::NeedsInit;
};

FSHA256HasherOpenSSL::FSHA256HasherOpenSSL() : Inner(MakePimpl<FSHA256HasherOpenSSL::FImplDetails>())
{
	check(Inner);
	Init();
}

EPlatformCryptoResult FSHA256HasherOpenSSL::Init()
{
	if (Inner->State == FImplDetails::EState::ReadyNeedsFinalize)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FSHA256Hasher::Init was called while there was still a digest being computed."));
		return EPlatformCryptoResult::Failure;
	}
	if (Inner->State == FImplDetails::EState::Ready)
	{
		return EPlatformCryptoResult::Success;
	}

	SHA256_Init(&Inner->Ctx);
	Inner->State = FImplDetails::EState::Ready;
	return EPlatformCryptoResult::Success;
}

EPlatformCryptoResult FSHA256HasherOpenSSL::Update(const TArrayView<const uint8> InDataBuffer)
{
	if (Inner->State == FImplDetails::EState::NeedsInit)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FSHA256Hasher::Update was called after the hasher was finalized without first calling Init"));
		return EPlatformCryptoResult::Failure;
	}

	SHA256_Update(&Inner->Ctx, InDataBuffer.GetData(), static_cast<size_t>(InDataBuffer.Num()));
	Inner->State = FImplDetails::EState::ReadyNeedsFinalize;
	return EPlatformCryptoResult::Success;
}

EPlatformCryptoResult FSHA256HasherOpenSSL::Finalize(const TArrayView<uint8> OutDataBuffer)
{
	if (Inner->State == FImplDetails::EState::NeedsInit)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FSHA256Hasher::Finalize was called after the hasher was finalized without first calling Init"));
		return EPlatformCryptoResult::Failure;
	}
	if (OutDataBuffer.Num() < (int32)OutputByteLength)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FSHA256Hasher::Finalize called with a buffer that is too small"));
		return EPlatformCryptoResult::Failure;
	}

	SHA256_Final(reinterpret_cast<unsigned char*>(OutDataBuffer.GetData()), &Inner->Ctx);
	Inner->State = FImplDetails::EState::NeedsInit;
	return EPlatformCryptoResult::Success;
}

TArray<uint8> FEncryptionContextOpenSSL::Encrypt_AES_256_ECB(const TArrayView<const uint8> Plaintext, const TArrayView<const uint8> Key, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 ECB Encrypt"), STAT_OpenSSL_AES256_ECB_Encrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoEncryptor> Encryptor = CreateEncryptor_AES_256_ECB(Key);
	if (!Encryptor.IsValid())
	{
		return TArray<uint8>();
	}

	TArray<uint8> Ciphertext;
	Ciphertext.AddUninitialized(Encryptor->GetUpdateBufferSizeBytes(Plaintext) + Encryptor->GetFinalizeBufferSizeBytes());

	int32 UpdateBytesWritten = 0;
	if (Encryptor->Update(Plaintext, Ciphertext, UpdateBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	int32 FinalizeBytesWritten = 0;
	if (Encryptor->Finalize(TArrayView<uint8>(Ciphertext.GetData() + UpdateBytesWritten, Ciphertext.Num() - UpdateBytesWritten), FinalizeBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	// Truncate message to final length
	Ciphertext.SetNum(UpdateBytesWritten + FinalizeBytesWritten);

	OutResult = EPlatformCryptoResult::Success;
	return Ciphertext;
}

TArray<uint8> FEncryptionContextOpenSSL::Encrypt_AES_256_CBC(const TArrayView<const uint8> Plaintext, const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 CBC Encrypt"), STAT_OpenSSL_AES256_CBC_Encrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoEncryptor> Encryptor = CreateEncryptor_AES_256_CBC(Key, InitializationVector);
	if (!Encryptor.IsValid())
	{
		return TArray<uint8>();
	}

	TArray<uint8> Ciphertext;
	Ciphertext.AddUninitialized(Encryptor->GetUpdateBufferSizeBytes(Plaintext) + Encryptor->GetFinalizeBufferSizeBytes());

	int32 UpdateBytesWritten = 0;
	if (Encryptor->Update(Plaintext, Ciphertext, UpdateBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	int32 FinalizeBytesWritten = 0;
	if (Encryptor->Finalize(TArrayView<uint8>(Ciphertext.GetData() + UpdateBytesWritten, Ciphertext.Num() - UpdateBytesWritten), FinalizeBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	// Truncate message to final length
	Ciphertext.SetNum(UpdateBytesWritten + FinalizeBytesWritten);

	OutResult = EPlatformCryptoResult::Success;
	return Ciphertext;
}

TArray<uint8> FEncryptionContextOpenSSL::Encrypt_AES_256_GCM(const TArrayView<const uint8> Plaintext, const TArrayView<const uint8> Key, const TArrayView<const uint8> Nonce, TArray<uint8>& OutAuthTag, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 GCM Encrypt"), STAT_OpenSSL_AES256_GCM_Encrypt, STATGROUP_PlatformCrypto);

	OutAuthTag.Reset();
	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoEncryptor> Encryptor = CreateEncryptor_AES_256_GCM(Key, Nonce);
	if (!Encryptor.IsValid())
	{
		return TArray<uint8>();
	}

	TArray<uint8> Ciphertext;
	Ciphertext.AddUninitialized(Encryptor->GetUpdateBufferSizeBytes(Plaintext) + Encryptor->GetFinalizeBufferSizeBytes());

	int32 UpdateBytesWritten = 0;
	if (Encryptor->Update(Plaintext, Ciphertext, UpdateBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	int32 FinalizeBytesWritten = 0;
	if (Encryptor->Finalize(TArrayView<uint8>(Ciphertext.GetData() + UpdateBytesWritten, Ciphertext.Num() - UpdateBytesWritten), FinalizeBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	// Truncate message to final length
	Ciphertext.SetNum(UpdateBytesWritten + FinalizeBytesWritten);

	OutAuthTag.SetNumUninitialized(Encryptor->GetCipherAuthTagSizeBytes());
	int32 AuthTagBytesWritten = 0;
	if (Encryptor->GenerateAuthTag(OutAuthTag, AuthTagBytesWritten) != EPlatformCryptoResult::Success)
	{
		OutAuthTag.Reset();
		return TArray<uint8>();
	}

	// Truncate auth tag to final length
	OutAuthTag.SetNum(AuthTagBytesWritten);

	OutResult = EPlatformCryptoResult::Success;
	return Ciphertext;
}

TArray<uint8> FEncryptionContextOpenSSL::Decrypt_AES_256_ECB(const TArrayView<const uint8> Ciphertext, const TArrayView<const uint8> Key, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 ECB Decrypt"), STAT_OpenSSL_AES256_ECB_Decrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoDecryptor> Decryptor = CreateDecryptor_AES_256_ECB(Key);
	if (!Decryptor.IsValid())
	{
		return TArray<uint8>();
	}

	TArray<uint8> Plaintext;
	Plaintext.AddUninitialized(Decryptor->GetUpdateBufferSizeBytes(Ciphertext) + Decryptor->GetFinalizeBufferSizeBytes());

	int32 UpdateBytesWritten = 0;
	if (Decryptor->Update(Ciphertext, Plaintext, UpdateBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	int32 FinalizeBytesWritten = 0;
	if (Decryptor->Finalize(TArrayView<uint8>(Plaintext.GetData() + UpdateBytesWritten, Plaintext.Num() - UpdateBytesWritten), FinalizeBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	// Truncate message to final length
	Plaintext.SetNum(UpdateBytesWritten + FinalizeBytesWritten);

	OutResult = EPlatformCryptoResult::Success;
	return Plaintext;
}

TArray<uint8> FEncryptionContextOpenSSL::Decrypt_AES_256_CBC(const TArrayView<const uint8> Ciphertext, const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 CBC Decrypt"), STAT_OpenSSL_AES256_CBC_Decrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoDecryptor> Decryptor = CreateDecryptor_AES_256_CBC(Key, InitializationVector);
	if (!Decryptor.IsValid())
	{
		return TArray<uint8>();
	}

	TArray<uint8> Plaintext;
	Plaintext.AddUninitialized(Decryptor->GetUpdateBufferSizeBytes(Ciphertext) + Decryptor->GetFinalizeBufferSizeBytes());

	int32 UpdateBytesWritten = 0;
	if (Decryptor->Update(Ciphertext, Plaintext, UpdateBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	int32 FinalizeBytesWritten = 0;
	if (Decryptor->Finalize(TArrayView<uint8>(Plaintext.GetData() + UpdateBytesWritten, Plaintext.Num() - UpdateBytesWritten), FinalizeBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	// Truncate message to final length
	Plaintext.SetNum(UpdateBytesWritten + FinalizeBytesWritten);

	OutResult = EPlatformCryptoResult::Success;
	return Plaintext;
}

TArray<uint8> FEncryptionContextOpenSSL::Decrypt_AES_256_GCM(const TArrayView<const uint8> Ciphertext, const TArrayView<const uint8> Key, const TArrayView<const uint8> Nonce, const TArrayView<const uint8> AuthTag, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 GCM Decrypt"), STAT_OpenSSL_AES256_GCM_Decrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoDecryptor> Decryptor = CreateDecryptor_AES_256_GCM(Key, Nonce, AuthTag);
	if (!Decryptor.IsValid())
	{
		return TArray<uint8>();
	}

	TArray<uint8> Plaintext;
	Plaintext.AddUninitialized(Decryptor->GetUpdateBufferSizeBytes(Ciphertext) + Decryptor->GetFinalizeBufferSizeBytes());

	int32 UpdateBytesWritten = 0;
	if (Decryptor->Update(Ciphertext, Plaintext, UpdateBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	int32 FinalizeBytesWritten = 0;
	if (Decryptor->Finalize(TArrayView<uint8>(Plaintext.GetData() + UpdateBytesWritten, Plaintext.Num() - UpdateBytesWritten), FinalizeBytesWritten) != EPlatformCryptoResult::Success)
	{
		return TArray<uint8>();
	}

	// Truncate message to final length
	Plaintext.SetNum(UpdateBytesWritten + FinalizeBytesWritten);

	OutResult = EPlatformCryptoResult::Success;
	return Plaintext;
}

TUniquePtr<IPlatformCryptoEncryptor> FEncryptionContextOpenSSL::CreateEncryptor_AES_256_ECB(const TArrayView<const uint8> Key)
{
	return FPlatformCryptoEncryptor_AES_256_ECB_OpenSSL::Create(Key);
}

TUniquePtr<IPlatformCryptoEncryptor> FEncryptionContextOpenSSL::CreateEncryptor_AES_256_CBC(const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector)
{
	return FPlatformCryptoEncryptor_AES_256_CBC_OpenSSL::Create(Key, InitializationVector);
}

TUniquePtr<IPlatformCryptoEncryptor> FEncryptionContextOpenSSL::CreateEncryptor_AES_256_GCM(const TArrayView<const uint8> Key, const TArrayView<const uint8> Nonce)
{
	return FPlatformCryptoEncryptor_AES_256_GCM_OpenSSL::Create(Key, Nonce);
}

TUniquePtr<IPlatformCryptoDecryptor> FEncryptionContextOpenSSL::CreateDecryptor_AES_256_ECB(const TArrayView<const uint8> Key)
{
	return FPlatformCryptoDecryptor_AES_256_ECB_OpenSSL::Create(Key);
}

TUniquePtr<IPlatformCryptoDecryptor> FEncryptionContextOpenSSL::CreateDecryptor_AES_256_CBC(const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector)
{
	return FPlatformCryptoDecryptor_AES_256_CBC_OpenSSL::Create(Key, InitializationVector);
}

TUniquePtr<IPlatformCryptoDecryptor> FEncryptionContextOpenSSL::CreateDecryptor_AES_256_GCM(const TArrayView<const uint8> Key, const TArrayView<const uint8> Nonce, const TArrayView<const uint8> AuthTag)
{
	return FPlatformCryptoDecryptor_AES_256_GCM_OpenSSL::Create(Key, Nonce, AuthTag);
}

bool FEncryptionContextOpenSSL::DigestSign_RS256(const TArrayView<const uint8> Message, TArray<uint8>& Signature, FRSAKeyHandle Key)
{
	Signature.SetNum(GetKeySize_RSA(Key));

	unsigned int BytesWritten = 0;
	return RSA_sign(NID_sha256, Message.GetData(), Message.Num(), Signature.GetData(), &BytesWritten, (RSA*)Key) == 1;
}

// Verify a message against the provided signature, using the PS256 algorithm.
// PS256 is a JWT abbrieviation which translates to "RSASSA-PSS using SHA-256 digest, and MGF1 with SHA-256"
bool FEncryptionContextOpenSSL::DigestVerify_PS256(const TArrayView<const uint8> Message, const TArrayView<const uint8> Signature, FRSAKeyHandle Key)
{
	TArray<uint8> MessageDigest;
	if (!CalcSHA256(Message, MessageDigest))
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerify_PS256 failed: error in generate digest"));
		return false;
	}

	if (!ensure(SHA256_DIGEST_LENGTH == MessageDigest.Num()))
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerify_PS256 failed: invalid digest length"));
		return false;
	}

	return DigestVerifyPreHashed_PS256(MessageDigest.GetData(), Signature.GetData(), Signature.Num(), Key);
}

// Verify a message digest (hash) against the provided signature, using the PS256 algorithm.
// PS256 is a JWT abbrieviation which translates to "RSASSA-PSS using SHA-256 digest, and MGF1 with SHA-256"
bool FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256(const uint8* const MessageDigest, const TArrayView<const uint8> Signature, FRSAKeyHandle Key)
{
	return DigestVerifyPreHashed_PS256(MessageDigest, Signature.GetData(), Signature.Num(), Key);
}

bool FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256(const uint8* const MessageDigest, const uint8* const Signature, const uint32 SignatureLen, FRSAKeyHandle Key)
{
	RSA *RsaKey = (RSA*)Key;

	// Create public key (PKEY) and copy the relevant RSA key data into it.
	EVP_PKEY *PubKey = EVP_PKEY_new();
	const bool bPubKeySetOk = 1 == EVP_PKEY_set1_RSA(PubKey, RsaKey);
	if (!bPubKeySetOk)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256 failed: error setting public key"));
		EVP_PKEY_free(PubKey);
		return false;
	}

	// Create a context for this public key operation, to hold any internal/intermediate variables.
	EVP_PKEY_CTX *PubKeyContext = EVP_PKEY_CTX_new(PubKey, nullptr);
	if (!PubKeyContext)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256 failed: error allocating public key context"));
		EVP_PKEY_free(PubKey);
		return false;
	}

	// Set up the context of this operation.

	bool bInitOk = true;
	bInitOk = bInitOk && (1 == EVP_PKEY_verify_init(PubKeyContext)); // set that we want to perform a verification operation
	bInitOk = bInitOk && (1 == EVP_PKEY_CTX_set_signature_md(PubKeyContext, EVP_sha256())); // set digest algorithm used in signature operation

	// PSS-specific context params.
	bInitOk = bInitOk && (1 == EVP_PKEY_CTX_set_rsa_padding(PubKeyContext, RSA_PKCS1_PSS_PADDING)); // set padding mode (PKCS1 PSS)
	bInitOk = bInitOk && (1 == EVP_PKEY_CTX_set_rsa_mgf1_md(PubKeyContext, EVP_sha256())); // set digest algorithm used by mask generation function (MGF1)
	// Salt length options:
	// - RSA_PSS_SALTLEN_DIGEST (-1) : Salt length matches digest length
	// - RSA_PSS_SALTLEN_AUTO (-2) : Auto-detect salt length (for verification only)
	// - RSA_PSS_SALTLEN_MAX (-3) : Set salt length to maximum possible
	// - Positive values : Fixed-size salt length
	bInitOk = bInitOk && (1 == EVP_PKEY_CTX_set_rsa_pss_saltlen(PubKeyContext, RSA_PSS_SALTLEN_AUTO)); // set PSS salt length

	if (!bInitOk)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256 failed in initializing public key context (%u): %s"),
			ERR_get_error(), UTF8_TO_TCHAR(ERR_error_string(ERR_get_error(), nullptr)));

		EVP_PKEY_CTX_free(PubKeyContext);
		EVP_PKEY_free(PubKey);
		return false;
	}

	// Verify against pre-hashed input.
	const int VerifyResult = EVP_PKEY_verify(PubKeyContext, Signature, SignatureLen, MessageDigest, SHA256_DIGEST_LENGTH);
	const bool bVerifyOk = (1 == VerifyResult);
	if (!bVerifyOk)
	{
		if (0 == VerifyResult)
		{
			UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256 signature is invalid (%u): %s"),
				ERR_get_error(), UTF8_TO_TCHAR(ERR_error_string(ERR_get_error(), nullptr)));
		}
		else
		{
			UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::DigestVerifyPreHashed_PS256 error verifying signature (%u): %s"),
				ERR_get_error(), UTF8_TO_TCHAR(ERR_error_string(ERR_get_error(), nullptr)));
		}
	}

	EVP_PKEY_CTX_free(PubKeyContext);
	EVP_PKEY_free(PubKey);

	return bVerifyOk;
}

bool FEncryptionContextOpenSSL::DigestVerify_RS256(const TArrayView<const uint8> Message, const TArrayView<const uint8> Signature, FRSAKeyHandle Key)
{
	return RSA_verify(NID_sha256, Message.GetData(), Message.Num(), Signature.GetData(), Signature.Num(), (RSA*)Key) == 1;
}

// Some platforms were upgraded to OpenSSL 1.1.1 while the others were left on a previous version. There are some minor differences we have to account for
// in the older version, so declare a handy define that we can use to gate the code
#if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x10100000L
#define USE_LEGACY_OPENSSL 1
#else
#define USE_LEGACY_OPENSSL 0
#endif

void BigNumToArray(const int32 InKeySize, const BIGNUM* InNum, TArray<uint8>& OutBytes)
{
	int32 NumBytes = BN_num_bytes(InNum);
	check(NumBytes <= InKeySize);
	OutBytes.SetNumZeroed(NumBytes);

	BN_bn2bin(InNum, OutBytes.GetData());
	Algo::Reverse(OutBytes);
}

bool FEncryptionContextOpenSSL::GenerateKey_RSA(const int32 InNumKeyBits, TArray<uint8>& OutPublicExponent, TArray<uint8>& OutPrivateExponent, TArray<uint8>& OutModulus)
{
	if (InNumKeyBits % 8 != 0)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::GenerateKey_RSA failed because InNumKeyBits is not divisible by 8."));
		return false;
	}

	int32 KeySize = InNumKeyBits;
	int32 KeySizeInBytes = InNumKeyBits / 8;

	RSA* RSAKey = RSA_new();
	BIGNUM* E = BN_new();
	BN_set_word(E, RSA_F4);
	RSA_generate_key_ex(RSAKey, KeySize, E, nullptr);

#if USE_LEGACY_OPENSSL
	const BIGNUM* PublicModulus = RSAKey->n;
	const BIGNUM* PublicExponent = RSAKey->e;
	const BIGNUM* PrivateExponent = RSAKey->d;
#else
	const BIGNUM* PublicModulus = RSA_get0_n(RSAKey);
	const BIGNUM* PublicExponent = RSA_get0_e(RSAKey);
	const BIGNUM* PrivateExponent = RSA_get0_d(RSAKey);
#endif

	BigNumToArray(KeySizeInBytes, PublicModulus, OutModulus);
	BigNumToArray(KeySizeInBytes, PublicExponent, OutPublicExponent);
	BigNumToArray(KeySizeInBytes, PrivateExponent, OutPrivateExponent);

	RSA_free(RSAKey);

	return true;
}

static void LoadBinaryIntoBigNum(const uint8* InData, int64 InDataSize, BIGNUM* InBigNum)
{
#if USE_LEGACY_OPENSSL
	TArray<uint8> Bytes(InData, InDataSize);
	Algo::Reverse(Bytes);
	BN_bin2bn(Bytes.GetData(), Bytes.Num(), InBigNum);
#else
	BN_lebin2bn(InData, InDataSize, InBigNum);
#endif
}

FRSAKeyHandle FEncryptionContextOpenSSL::CreateKey_RSA(const TArrayView<const uint8> PublicExponent, const TArrayView<const uint8> PrivateExponent, const TArrayView<const uint8> Modulus)
{
	RSA* NewKey = RSA_new();

	BIGNUM* BN_PublicExponent = PublicExponent.Num() > 0 ? BN_new() : nullptr;
	BIGNUM* BN_PrivateExponent = PrivateExponent.Num() > 0 ? BN_new() : nullptr;
	BIGNUM* BN_Modulus = BN_new();

	bool bCreateKeyFailed = false;

	if (PublicExponent.Num())
	{
		LoadBinaryIntoBigNum(PublicExponent.GetData(), PublicExponent.Num(), BN_PublicExponent);

		if (!BN_is_odd(BN_PublicExponent))
		{
			bCreateKeyFailed = true;
			UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::CreateKey_RSA: public exponent is not an odd number"));
		}
	}

	if (PrivateExponent.Num())
	{
		LoadBinaryIntoBigNum(PrivateExponent.GetData(), PrivateExponent.Num(), BN_PrivateExponent);

		if (!BN_is_odd(BN_PrivateExponent))
		{
			bCreateKeyFailed = true;
			UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::CreateKey_RSA: private exponent is not an odd number"));
		}
	}

	LoadBinaryIntoBigNum(Modulus.GetData(), Modulus.Num(), BN_Modulus);
	if (!BN_is_odd(BN_Modulus))
	{
		bCreateKeyFailed = true;
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::CreateKey_RSA: modulus is not an odd number"));
	}

#if USE_LEGACY_OPENSSL
	NewKey->n = BN_Modulus;
	NewKey->e = BN_PublicExponent;
	NewKey->d = BN_PrivateExponent;
#else
	const int Result = RSA_set0_key(NewKey, BN_Modulus, BN_PublicExponent, BN_PrivateExponent);
	if (!Result)
	{
		bCreateKeyFailed = true;
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::CreateKey_RSA failed (%u): %s"),
			ERR_get_error(), UTF8_TO_TCHAR(ERR_error_string(ERR_get_error(), nullptr)));
	}
#endif

	if (bCreateKeyFailed)
	{
		RSA_free(NewKey);
		NewKey = nullptr;
	}

	return NewKey;
}

void FEncryptionContextOpenSSL::DestroyKey_RSA(FRSAKeyHandle Key)
{
	RSA_free((RSA*)Key);
}

int32 FEncryptionContextOpenSSL::GetKeySize_RSA(FRSAKeyHandle Key)
{
	return RSA_size((RSA*)Key);
}

int32 FEncryptionContextOpenSSL::GetMaxDataSize_RSA(FRSAKeyHandle Key)
{
	return (GetKeySize_RSA(Key)) - RSA_PKCS1_PADDING_SIZE;
}

int32 FEncryptionContextOpenSSL::EncryptPublic_RSA(TArrayView<const uint8> Source, TArray<uint8>& Dest, FRSAKeyHandle Key)
{
	Dest.SetNum(GetKeySize_RSA(Key));
	int NumEncryptedBytes = RSA_public_encrypt(Source.Num(), Source.GetData(), Dest.GetData(), (RSA*)Key, RSA_PKCS1_PADDING);
	if (NumEncryptedBytes == -1)
	{
		Dest.Empty(0);
	}
	return NumEncryptedBytes;
}

int32 FEncryptionContextOpenSSL::EncryptPrivate_RSA(TArrayView<const uint8> Source, TArray<uint8>& Dest, FRSAKeyHandle Key)
{
	Dest.SetNum(GetKeySize_RSA(Key));
	int NumEncryptedBytes = RSA_private_encrypt(Source.Num(), Source.GetData(), Dest.GetData(), (RSA*)Key, RSA_PKCS1_PADDING);
	if (NumEncryptedBytes == -1)
	{
		Dest.Empty(0);
	}
	return NumEncryptedBytes;
}

int32 FEncryptionContextOpenSSL::DecryptPublic_RSA(TArrayView<const uint8> Source, TArray<uint8>& Dest, FRSAKeyHandle Key)
{
	Dest.SetNum(GetKeySize_RSA(Key) - RSA_PKCS1_PADDING_SIZE);
	int NumDecryptedBytes = RSA_public_decrypt(Source.Num(), Source.GetData(), Dest.GetData(), (RSA*)Key, RSA_PKCS1_PADDING);
	if (NumDecryptedBytes == -1)
	{
		Dest.Empty(0);
	}
	else
	{
		Dest.SetNum(NumDecryptedBytes);
	}
	return NumDecryptedBytes;
}

int32 FEncryptionContextOpenSSL::DecryptPrivate_RSA(TArrayView<const uint8> Source, TArray<uint8>& Dest, FRSAKeyHandle Key)
{
	Dest.SetNum(GetKeySize_RSA(Key) - RSA_PKCS1_PADDING_SIZE);
	int NumDecryptedBytes = RSA_private_decrypt(Source.Num(), Source.GetData(), Dest.GetData(), (RSA*)Key, RSA_PKCS1_PADDING);
	if (NumDecryptedBytes == -1)
	{
		Dest.Empty(0);
	}
	else
	{
		Dest.SetNum(NumDecryptedBytes);
	}
	return NumDecryptedBytes;
}

EPlatformCryptoResult FEncryptionContextOpenSSL::CreateRandomBytes(const TArrayView<uint8> OutData)
{
	return (RAND_bytes(OutData.GetData(), OutData.Num()) == 1) ? EPlatformCryptoResult::Success : EPlatformCryptoResult::Failure;
}

EPlatformCryptoResult FEncryptionContextOpenSSL::CreatePseudoRandomBytes(const TArrayView<uint8> OutData)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	return (RAND_pseudo_bytes(OutData.GetData(), OutData.Num()) == 1) ? EPlatformCryptoResult::Success : EPlatformCryptoResult::Failure;
#else // OPENSSL_VERSION_NUMBER < 0x10100000L
	return CreateRandomBytes(OutData);
#endif // OPENSSL_VERSION_NUMBER < 0x10100000L
}

FSHA256Hasher FEncryptionContextOpenSSL::CreateSHA256Hasher()
{
	return FSHA256Hasher();
}

bool FEncryptionContextOpenSSL::CalcSHA256(const TArrayView<const uint8> Source, TArray<uint8>& OutHash)
{
	FSHA256Hasher Hasher = CreateSHA256Hasher();
	if (Hasher.Init() != EPlatformCryptoResult::Success)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::CalcSHA256: create failed"));
		return false;
	}

	if (Hasher.Update(Source) != EPlatformCryptoResult::Success)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::CalcSHA256: update failed"));
		return false;
	}

	OutHash.Empty();
	OutHash.AddUninitialized(FSHA256Hasher::OutputByteLength);
	if (Hasher.Finalize(OutHash) != EPlatformCryptoResult::Success)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Verbose, TEXT("FEncryptionContextOpenSSL::CalcSHA256: finalize failed"));
		return false;
	}

	return true;
}
