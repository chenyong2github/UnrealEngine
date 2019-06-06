// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EncryptionContextOpenSSL.h"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>

DEFINE_LOG_CATEGORY_STATIC(LogPlatformCryptoOpenSSL, Warning, All);

static const int32 AES256_KeySizeInBytes = 32;
static const int32 AES256_BlockSizeInBytes = 16;
static const int32 AES256_IVSizeInBytes = 12;
static const int32 AES256_AuthTagSizeInBytes = 16;

class FScopedEVPContext
{
public:
	FScopedEVPContext() :
		Context(EVP_CIPHER_CTX_new())
	{
	}

	~FScopedEVPContext()
	{
		EVP_CIPHER_CTX_free(Context);
	}

	EVP_CIPHER_CTX* Get() const { return Context; }

private:
	EVP_CIPHER_CTX* Context;
};

TArray<uint8> FEncryptionContextOpenSSL::Encrypt_AES_256_ECB(const TArrayView<const uint8> Plaintext, const TArrayView<const uint8> Key, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 Encrypt"), STAT_OpenSSL_AES_Encrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	if (Key.Num() != AES256_KeySizeInBytes)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_ECB: Key size %d is not the expected size %d."), Key.Num(), AES256_KeySizeInBytes);
		return TArray<uint8>();
	}

	FScopedEVPContext Context;
	if (Context.Get() == nullptr)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_ECB: failed to create EVP context."));
		return TArray<uint8>();
	}

	const int InitResult = EVP_EncryptInit_ex(Context.Get(), EVP_aes_256_ecb(), nullptr, Key.GetData(), nullptr);
	if (InitResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_ECB: EVP_EncryptInit_ex failed."));
		return TArray<uint8>();
	}

	TArray<uint8> Ciphertext;
	Ciphertext.SetNumUninitialized(Plaintext.Num() + AES256_BlockSizeInBytes); // Allow for up to a block of padding.

	int OutLength = 0;
	const int UpdateResult = EVP_EncryptUpdate(Context.Get(), Ciphertext.GetData(), &OutLength, Plaintext.GetData(), Plaintext.Num());
	if (UpdateResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_ECB: EVP_EncryptUpdate failed."));
		return TArray<uint8>();
	}

	int FinalizeLength = 0;
	const int FinalizeResult = EVP_EncryptFinal_ex(Context.Get(), Ciphertext.GetData() + OutLength, &FinalizeLength);
	if (FinalizeResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_ECB: EVP_EncryptFinal_ex failed."));
		return TArray<uint8>();
	}

	Ciphertext.SetNum(OutLength + FinalizeLength);

	OutResult = EPlatformCryptoResult::Success;
	return Ciphertext;
}

TArray<uint8> FEncryptionContextOpenSSL::Decrypt_AES_256_ECB(const TArrayView<const uint8> Ciphertext, const TArrayView<const uint8> Key, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 Decrypt"), STAT_OpenSSL_AES_Decrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	if (Key.Num() != AES256_KeySizeInBytes)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_ECB: Key size %d is not the expected size %d."), Key.Num(), AES256_KeySizeInBytes);
		return TArray<uint8>();
	}

	FScopedEVPContext Context;
	if (Context.Get() == nullptr)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_ECB: failed to create EVP context."));
		return TArray<uint8>();
	}

	const int InitResult = EVP_DecryptInit_ex(Context.Get(), EVP_aes_256_ecb(), nullptr, Key.GetData(), nullptr);
	if (InitResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_ECB: EVP_DecryptInit_ex failed."));
		return TArray<uint8>();
	}

	TArray<uint8> Plaintext;
	Plaintext.SetNumUninitialized(Ciphertext.Num());

	int OutLength = 0;
	const int UpdateResult = EVP_DecryptUpdate(Context.Get(), Plaintext.GetData(), &OutLength, Ciphertext.GetData(), Ciphertext.Num());
	if (UpdateResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_ECB: EVP_DecryptUpdate failed."));
		return TArray<uint8>();
	}

	int FinalizeLength = 0;
	const int FinalizeResult = EVP_DecryptFinal_ex(Context.Get(), Plaintext.GetData() + OutLength, &FinalizeLength);
	if (FinalizeResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_ECB: EVP_DecryptFinal_ex failed."));
		return TArray<uint8>();
	}

	Plaintext.SetNum(OutLength + FinalizeLength);

	OutResult = EPlatformCryptoResult::Success;
	return Plaintext;
}

TArray<uint8> FEncryptionContextOpenSSL::Encrypt_AES_256_GCM(const TArrayView<const uint8> Plaintext, const TArrayView<const uint8> Key, const TArrayView<const uint8> IV, TArray<uint8>& OutAuthTag, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256GCM Encrypt"), STAT_OpenSSL_AES_GCM_Encrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	if (Key.Num() != AES256_KeySizeInBytes)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_GCM: Key size %d is not the expected size %d."), Key.Num(), AES256_KeySizeInBytes);
		return TArray<uint8>();
	}

	FScopedEVPContext Context;
	if (Context.Get() == nullptr)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_GCM: failed to create EVP context."));
		return TArray<uint8>();
	}

	const int InitResult = EVP_EncryptInit_ex(Context.Get(), EVP_aes_256_gcm(), nullptr, Key.GetData(), IV.GetData());
	if (InitResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_GCM: EVP_EncryptInit_ex failed."));
		return TArray<uint8>();
	}

	TArray<uint8> Ciphertext;
	Ciphertext.SetNumUninitialized(Plaintext.Num() + AES256_BlockSizeInBytes); // Allow for up to a block of padding.

	int OutLength = 0;
	const int UpdateResult = EVP_EncryptUpdate(Context.Get(), Ciphertext.GetData(), &OutLength, Plaintext.GetData(), Plaintext.Num());
	if (UpdateResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_GCM: EVP_EncryptUpdate failed."));
		return TArray<uint8>();
	}

	int FinalizeLength = 0;
	const int FinalizeResult = EVP_EncryptFinal_ex(Context.Get(), Ciphertext.GetData() + OutLength, &FinalizeLength);
	if (FinalizeResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_GCM: EVP_EncryptFinal_ex failed."));
		return TArray<uint8>();
	}

	OutAuthTag.Reset();
	OutAuthTag.AddUninitialized(AES256_AuthTagSizeInBytes);

	const int GetTagResult = EVP_CIPHER_CTX_ctrl(Context.Get(), EVP_CTRL_GCM_GET_TAG, OutAuthTag.Num(), OutAuthTag.GetData());
	if (GetTagResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Encrypt_AES_256_GCM: EVP_CIPHER_CTX_ctrl failed."));
		return TArray<uint8>();
	}

	Ciphertext.SetNum(OutLength + FinalizeLength);

	OutResult = EPlatformCryptoResult::Success;
	return Ciphertext;
}

TArray<uint8> FEncryptionContextOpenSSL::Decrypt_AES_256_GCM(const TArrayView<const uint8> Ciphertext, const TArrayView<const uint8> Key, const TArrayView<const uint8> IV, const TArrayView<const uint8> AuthTag, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256GCM Decrypt"), STAT_OpenSSL_AES_GCM_Decrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	if (Key.Num() != AES256_KeySizeInBytes)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: Key size %d is not the expected size %d."), Key.Num(), AES256_KeySizeInBytes);
		return TArray<uint8>();
	}

	if (IV.Num() != AES256_IVSizeInBytes)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: IV size %d is not the expected size %d."), IV.Num(), AES256_IVSizeInBytes);
		return TArray<uint8>();
	}

	if (AuthTag.Num() != AES256_AuthTagSizeInBytes)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: Auth tag size %d is not the expected size %d."), IV.Num(), AES256_AuthTagSizeInBytes);
		return TArray<uint8>();
	}

	FScopedEVPContext Context;
	if (Context.Get() == nullptr)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: failed to create EVP context."));
		return TArray<uint8>();
	}

	const int InitResult = EVP_DecryptInit_ex(Context.Get(), EVP_aes_256_gcm(), nullptr, Key.GetData(), IV.GetData());
	if (InitResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: EVP_DecryptInit_ex failed."));
		return TArray<uint8>();
	}

	TArray<uint8> Plaintext;
	Plaintext.SetNumUninitialized(Ciphertext.Num());

	int OutLength = 0;
	const int UpdateResult = EVP_DecryptUpdate(Context.Get(), Plaintext.GetData(), &OutLength, Ciphertext.GetData(), Ciphertext.Num());
	if (UpdateResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: EVP_DecryptUpdate failed."));
		return TArray<uint8>();
	}

	const int SetTagResult = EVP_CIPHER_CTX_ctrl(Context.Get(), EVP_CTRL_GCM_SET_TAG, AuthTag.Num(), const_cast<uint8*>(AuthTag.GetData()));
	if (SetTagResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: EVP_CIPHER_CTX_ctrl failed."));
		return TArray<uint8>();
	}

	int FinalizeLength = 0;
	const int FinalizeResult = EVP_DecryptFinal_ex(Context.Get(), Plaintext.GetData() + OutLength, &FinalizeLength);
	if (FinalizeResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::Decrypt_AES_256_GCM: EVP_DecryptFinal_ex failed."));
		return TArray<uint8>();
	}

	Plaintext.SetNum(OutLength + FinalizeLength);

	OutResult = EPlatformCryptoResult::Success;
	return Plaintext;
}

TArray<uint8> FEncryptionContextOpenSSL::GetRandomBytes(uint32 NumBytes, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL GetRandomBytes"), STAT_OpenSSL_GetRandomBytes, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TArray<uint8> RandomBytes;
	RandomBytes.AddUninitialized(NumBytes);

	const int RandResult = RAND_bytes(RandomBytes.GetData(), RandomBytes.Num());
	if (RandResult != 1)
	{
		UE_LOG(LogPlatformCryptoOpenSSL, Warning, TEXT("FEncryptionContextOpenSSL::GetRandomBytes: RAND_bytes failed."));
		return TArray<uint8>();
	}
	
	OutResult = EPlatformCryptoResult::Success;
	return RandomBytes;
}

class FScopedEVPMDContext
{
public:
	FScopedEVPMDContext() :
		Context(EVP_MD_CTX_create())
	{
	}

	FScopedEVPMDContext(FScopedEVPMDContext&) = delete;
	FScopedEVPMDContext& operator=(FScopedEVPMDContext&) = delete;

	~FScopedEVPMDContext()
	{
		EVP_MD_CTX_destroy(Context);
	}

	EVP_MD_CTX* Get() const { return Context; }

private:
	EVP_MD_CTX* Context;
};

bool FEncryptionContextOpenSSL::DigestVerify_PS256(const TArrayView<const char> Message, const TArrayView<const uint8> Signature, const TArrayView<const uint8> PKCS1Key)
{
	FScopedEVPMDContext Context;

	if (Context.Get() == nullptr)
	{
		return false;
	}

	const unsigned char* PKCS1KeyData = PKCS1Key.GetData();
	RSA* RsaKey = d2i_RSAPublicKey(nullptr, &PKCS1KeyData, PKCS1Key.Num());
	EVP_PKEY* PKey = EVP_PKEY_new();
	EVP_PKEY_assign_RSA(PKey, RsaKey);

	EVP_PKEY_CTX* KeyContext = nullptr;
	EVP_DigestVerifyInit(Context.Get(), &KeyContext, EVP_sha256(), nullptr, PKey);
	EVP_PKEY_CTX_set_rsa_padding(KeyContext, RSA_PKCS1_PSS_PADDING);
	EVP_DigestVerifyUpdate(Context.Get(), Message.GetData(), Message.Num());
	return EVP_DigestVerifyFinal(Context.Get(), const_cast<uint8*>(Signature.GetData()), Signature.Num()) == 1;
}

// Some platforms were upgraded to OpenSSL 1.1.1 while the others were left on a previous version. There are some minor differences we have to account for
// in the older version, so declare a handy define that we can use to gate the code
#if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x10100000L
#define USE_LEGACY_OPENSSL 1
#else
#define USE_LEGACY_OPENSSL 0
#endif

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

	if (PublicExponent.Num())
	{
		LoadBinaryIntoBigNum(PublicExponent.GetData(), PublicExponent.Num(), BN_PublicExponent);
	}

	if (PrivateExponent.Num())
	{
		LoadBinaryIntoBigNum(PrivateExponent.GetData(), PrivateExponent.Num(), BN_PrivateExponent);
	}

	LoadBinaryIntoBigNum(Modulus.GetData(), Modulus.Num(), BN_Modulus);
#if USE_LEGACY_OPENSSL
	NewKey->n = BN_Modulus;
	NewKey->e = BN_PublicExponent;
	NewKey->d = BN_PrivateExponent;
#else
	RSA_set0_key(NewKey, BN_Modulus, BN_PublicExponent, BN_PrivateExponent);
#endif

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