// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "AudioLinkSettingsAbstract.generated.h"

/**
  * This interface should be used to provide a non-uclass version of the data described in
  * your implementation of UAudioLinkSettingsAbstract
  */
class AUDIOLINKCORE_API IAudioLinkSettingsProxy
{
public:
	virtual ~IAudioLinkSettingsProxy() = default;
};

/**
  * This opaque class should be used for specifying settings for how audio should be
  * send to an external audio link.
  */
UCLASS(config = Engine, abstract, editinlinenew, BlueprintType)
class AUDIOLINKCORE_API UAudioLinkSettingsAbstract : public UObject
{
	GENERATED_BODY()

public:
	using FSharedSettingsProxyPtr = TSharedPtr<IAudioLinkSettingsProxy, ESPMode::ThreadSafe>;

	virtual FName GetFactoryName() const PURE_VIRTUAL(UAudioLinkSettingsAbstract::GetFactoryName, return {};);
	
	virtual const FSharedSettingsProxyPtr& GetProxy() const
	{
		if (!ProxyInstance.IsValid())
		{
			ProxyInstance = MakeProxy();
		}
		return ProxyInstance;
	}

	template<typename T> 
	auto GetCastProxy() const
	{
		return StaticCastSharedPtr<T>(GetProxy());
	}

protected:
	virtual FSharedSettingsProxyPtr MakeProxy() const PURE_VIRTUAL(UAudioLinkSettingsAbstract::MakeProxy, return {};);
	mutable FSharedSettingsProxyPtr ProxyInstance;
};

