// Copyright Epic Games, Inc. All Rights Reserved.

#include "JwtAlgorithms.h"


FJwtAlgorithm_RS256::FJwtAlgorithm_RS256()
{
	EncryptionContext = FJwtUtils::GetEncryptionContext();
}


FJwtAlgorithm_RS256::~FJwtAlgorithm_RS256()
{
	if (PublicKey)
	{
		DestroyKey(PublicKey);
	}
}


bool FJwtAlgorithm_RS256::VerifySignature(
	const TArrayView<const uint8> EncodedMessage,
	const TArrayView<const uint8> DecodedSignature) const
{
	if (!EncryptionContext.IsValid())
	{
		return false;
	}

	TArray<uint8> HashedMessage;

	// Hash the encoded message
	if (!EncryptionContext->CalcSHA256(EncodedMessage, HashedMessage))
	{
		return false;
	}

	return EncryptionContext->DigestVerify_RS256(
		HashedMessage, DecodedSignature, PublicKey);
}


bool FJwtAlgorithm_RS256::SetPublicKey(const TArrayView<const uint8> InKey)
{
	if (!EncryptionContext.IsValid())
	{
		return false;
	}

	if (PublicKey)
	{
		DestroyKey(PublicKey);
	}

	PublicKey = EncryptionContext->GetPublicKey_RSA(InKey);

	if (!PublicKey)
	{
		UE_LOG(LogJwt, Error,
			TEXT("[FJwtAlgorithm_RS256::SetPublicKey] RSA public key is invalid."));

		return false;
	}

	return true;
}


bool FJwtAlgorithm_RS256::SetPublicKey(const FStringView InKey)
{
	TArray<uint8> PublicKeyBytes;

	FJwtUtils::StringViewToBytes(InKey, PublicKeyBytes);

	return SetPublicKey(PublicKeyBytes);
}


void FJwtAlgorithm_RS256::DestroyKey(void* Key)
{
	if (!EncryptionContext.IsValid())
	{
		return;
	}

	EncryptionContext->DestroyKey_RSA(Key);
}

