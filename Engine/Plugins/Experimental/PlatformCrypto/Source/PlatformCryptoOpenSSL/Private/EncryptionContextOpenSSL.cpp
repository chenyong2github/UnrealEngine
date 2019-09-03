// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EncryptionContextOpenSSL.h"
#include "PlatformCryptoOpenSSLTypes.h"
#include "PlatformCryptoAesEncryptorsOpenSSL.h"
#include "PlatformCryptoAesDecryptorsOpenSSL.h"

DEFINE_LOG_CATEGORY(LogPlatformCryptoOpenSSL);

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

TArray<uint8> FEncryptionContextOpenSSL::Encrypt_AES_256_GCM(const TArrayView<const uint8> Plaintext, const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector, TArray<uint8>& OutAuthTag, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 GCM Encrypt"), STAT_OpenSSL_AES256_GCM_Encrypt, STATGROUP_PlatformCrypto);

	OutAuthTag.Reset();
	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoEncryptor> Encryptor = CreateEncryptor_AES_256_GCM(Key, InitializationVector);
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

TArray<uint8> FEncryptionContextOpenSSL::Decrypt_AES_256_GCM(const TArrayView<const uint8> Ciphertext, const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector, const TArrayView<const uint8> AuthTag, EPlatformCryptoResult& OutResult)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OpenSSL AES256 GCM Decrypt"), STAT_OpenSSL_AES256_GCM_Decrypt, STATGROUP_PlatformCrypto);

	OutResult = EPlatformCryptoResult::Failure;

	TUniquePtr<IPlatformCryptoDecryptor> Decryptor = CreateDecryptor_AES_256_GCM(Key, InitializationVector, AuthTag);
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

TUniquePtr<IPlatformCryptoEncryptor> FEncryptionContextOpenSSL::CreateEncryptor_AES_256_GCM(const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector)
{
	return FPlatformCryptoEncryptor_AES_256_GCM_OpenSSL::Create(Key, InitializationVector);
}

TUniquePtr<IPlatformCryptoDecryptor> FEncryptionContextOpenSSL::CreateDecryptor_AES_256_ECB(const TArrayView<const uint8> Key)
{
	return FPlatformCryptoDecryptor_AES_256_ECB_OpenSSL::Create(Key);
}

TUniquePtr<IPlatformCryptoDecryptor> FEncryptionContextOpenSSL::CreateDecryptor_AES_256_CBC(const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector)
{
	return FPlatformCryptoDecryptor_AES_256_CBC_OpenSSL::Create(Key, InitializationVector);
}

TUniquePtr<IPlatformCryptoDecryptor> FEncryptionContextOpenSSL::CreateDecryptor_AES_256_GCM(const TArrayView<const uint8> Key, const TArrayView<const uint8> InitializationVector, const TArrayView<const uint8> AuthTag)
{
	return FPlatformCryptoDecryptor_AES_256_GCM_OpenSSL::Create(Key, InitializationVector, AuthTag);
}

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
