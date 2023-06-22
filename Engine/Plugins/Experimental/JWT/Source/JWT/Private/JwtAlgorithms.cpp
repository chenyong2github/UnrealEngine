// Copyright Epic Games, Inc. All Rights Reserved.

#include "JwtAlgorithms.h"

#include "JwtUtils.h"


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
	TUniquePtr<FEncryptionContext> EncryptionContext
		= FJwtUtils::GetEncryptionContext();

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
	TUniquePtr<FEncryptionContext> EncryptionContext
		= FJwtUtils::GetEncryptionContext();

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


void FJwtAlgorithm_RS256::DestroyKey(void* Key)
{
	TUniquePtr<FEncryptionContext> EncryptionContext
		= FJwtUtils::GetEncryptionContext();

	if (!EncryptionContext.IsValid())
	{
		return;
	}

	EncryptionContext->DestroyKey_RSA(Key);
}

