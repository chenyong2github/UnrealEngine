// Copyright Epic Games, Inc. All Rights Reserved.

#include "ISoundfieldFormat.h"
#include "ISoundfieldEndpoint.h"

FName ISoundfieldFactory::GetFormatNameForNoEncoding()
{
	static FName NoEncodingFormatName = FName(TEXT("No Encoding"));
	return NoEncodingFormatName;
}

FName ISoundfieldFactory::GetFormatNameForInheritedEncoding()
{
	static FName InheritedFormatName = FName(TEXT("Inherited Encoding"));
	return InheritedFormatName;
}

FName ISoundfieldFactory::GetModularFeatureName()
{
	static FName SoundfieldFactoryName = FName(TEXT("Soundfield Format"));
	return SoundfieldFactoryName;
}

void ISoundfieldFactory::RegisterSoundfieldFormat(ISoundfieldFactory* InFactory)
{
	check(IsInGameThread());
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), InFactory);
}

void ISoundfieldFactory::UnregisterSoundfieldFormat(ISoundfieldFactory* InFactory)
{
	check(IsInGameThread());
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), InFactory);
}

ISoundfieldFactory* ISoundfieldFactory::Get(const FName& InName)
{
	if (InName == GetFormatNameForNoEncoding() || InName == FName())
	{
		return nullptr;
	}

	TArray<ISoundfieldFactory*> Factories = IModularFeatures::Get().GetModularFeatureImplementations<ISoundfieldFactory>(GetModularFeatureName());

	for (ISoundfieldFactory* Factory : Factories)
	{
		if (Factory && InName == Factory->GetSoundfieldFormatName())
		{
			if (Factory->IsEndpointFormat())
			{
				ensureAlwaysMsgf(false, TEXT("This format is only supported for endpoints. Use ISoundfieldEndpointFactory::Get instead."));
			}

			return Factory;
		}
	}

	ensureAlwaysMsgf(false, TEXT("Soundfield Format %s not found!"), *InName.ToString());
	return nullptr;
}

TArray<FName> ISoundfieldFactory::GetAvailableSoundfieldFormats()
{
	TArray<FName> SoundfieldFormatNames;

	SoundfieldFormatNames.Add(GetFormatNameForInheritedEncoding());
	SoundfieldFormatNames.Add(GetFormatNameForNoEncoding());

	TArray<ISoundfieldFactory*> Factories = IModularFeatures::Get().GetModularFeatureImplementations<ISoundfieldFactory>(GetModularFeatureName());
	for (ISoundfieldFactory* Factory : Factories)
	{
		SoundfieldFormatNames.Add(Factory->GetSoundfieldFormatName());
	}

	return SoundfieldFormatNames;
}
