// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IPlatformCrypto.h"
#include "PlatformCryptoIncludes.h"
#include "Features/IModularFeatures.h"
#include "Misc/IEngineCrypto.h"

class FPlatformCryptoModularFeature : public IEngineCrypto
{
public:

	FPlatformCryptoModularFeature()
	{
		IModularFeatures::Get().RegisterModularFeature(IEngineCrypto::GetFeatureName(), this);
	}

	virtual ~FPlatformCryptoModularFeature()
	{
		Context.Reset();
		IModularFeatures::Get().UnregisterModularFeature(IEngineCrypto::GetFeatureName(), this);
	}
	
	/** IEngineCrypto implementation */
	virtual FRSAKeyHandle CreateRSAKey(const TArrayView<const uint8> InPublicExponent, const TArrayView<const uint8> InPrivateExponent, const TArrayView<const uint8> InModulus) override
	{
		return GetContext()->CreateKey_RSA(InPublicExponent, InPrivateExponent, InModulus);
	}

	virtual void DestroyRSAKey(FRSAKeyHandle InKey) override
	{
		GetContext()->DestroyKey_RSA(InKey);
	}

	virtual int32 GetKeySize(FRSAKeyHandle InKey) override
	{
		return GetContext()->GetKeySize_RSA(InKey);
	}

	virtual int32 GetMaxDataSize(FRSAKeyHandle InKey) override
	{
		return GetContext()->GetMaxDataSize_RSA(InKey);
	}

	virtual int32 EncryptPublic(const TArrayView<const uint8> InSource, TArray<uint8>& OutDestination, FRSAKeyHandle InKey) override
	{
		return GetContext()->EncryptPublic_RSA(InSource, OutDestination, InKey);
	}

	virtual int32 EncryptPrivate(const TArrayView<const uint8> InSource, TArray<uint8>& OutDestination, FRSAKeyHandle InKey) override
	{
		return GetContext()->EncryptPrivate_RSA(InSource, OutDestination, InKey);
	}

	virtual int32 DecryptPublic(const TArrayView<const uint8> InSource, TArray<uint8>& OutDestination, FRSAKeyHandle InKey) override
	{
		return GetContext()->DecryptPublic_RSA(InSource, OutDestination, InKey);
	}

	virtual int32 DecryptPrivate(const TArrayView<const uint8> InSource, TArray<uint8>& OutDestination, FRSAKeyHandle InKey) override
	{
		return GetContext()->DecryptPrivate_RSA(InSource, OutDestination, InKey);
	}

private:

	TUniquePtr<FEncryptionContext>& GetContext()
	{
		if (!Context.IsValid())
		{
			Context = IPlatformCrypto::Get().CreateContext();
		}

		return Context;
	}

	/** Content used by IEngineCrypto implementation */
	TUniquePtr<FEncryptionContext> Context;
};

FPlatformCryptoModularFeature GPlatformCryptoModularFeature;

IMPLEMENT_MODULE(FDefaultModuleImpl, PlatformCrypto)

TUniquePtr<FEncryptionContext> IPlatformCrypto::CreateContext()
{
	return MakeUnique<FEncryptionContext>();
}