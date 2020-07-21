// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraSystem.h"

#include "NiagaraComponentSettings.generated.h"

UCLASS(config=Game, defaultconfig)
class NIAGARA_API UNiagaraComponentSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	FORCEINLINE static bool ShouldSupressActivation(const UNiagaraSystem* System)
	{
		check(System != nullptr);
		if ( bAllowSupressActivation )
		{
			if (const UNiagaraComponentSettings* ComponentSettings = GetDefault<UNiagaraComponentSettings>())
			{
				if (ComponentSettings->SupressActivationList.Contains(System->GetFName()))
				{
					return true;
				}
			}
		}
		return false;
	}

	FORCEINLINE static bool ShouldForceAutoPooling(const UNiagaraSystem* System)
	{
		check(System != nullptr);
		if (bAllowForceAutoPooling)
		{
			if (const UNiagaraComponentSettings* ComponentSettings = GetDefault<UNiagaraComponentSettings>())
			{
				if (ComponentSettings->ForceAutoPooolingList.Contains(System->GetFName()))
				{
					return true;
				}
			}
		}
		return false;
	}

	UPROPERTY(config)
	TSet<FName> SupressActivationList;

	UPROPERTY(config)
	TSet<FName> ForceAutoPooolingList;

	//UPROPERTY(config)
	//TSet<FName> ForceLatencyList;

	static int32 bAllowSupressActivation;
	static int32 bAllowForceAutoPooling;
};
