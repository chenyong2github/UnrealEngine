// Copyright Epic Games, Inc. All Rights Reserved.

#include "JwtUtils.h"
#include "JwtGlobals.h"


TUniquePtr<FEncryptionContext> FJwtUtils::GetEncryptionContext()
{
	TUniquePtr<FEncryptionContext> EncryptionContext
		= IPlatformCrypto::Get().CreateContext();

	if (!EncryptionContext.IsValid())
	{
		UE_LOG(LogJwt, Error,
			TEXT("[JwtUtils::GetEncryptionContext] "
				"EncryptionContext pointer is invalid."));
	}

	return EncryptionContext;
}
