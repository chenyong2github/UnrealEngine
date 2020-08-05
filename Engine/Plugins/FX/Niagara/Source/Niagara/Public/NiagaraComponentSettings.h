// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraSystem.h"

#include "NiagaraComponentSettings.generated.h"

USTRUCT(meta = (DisplayName = "Emitter Name Settings Reference"))
struct FNiagaraEmitterNameSettingsRef
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Parameters)
		FName SystemName;

	UPROPERTY(EditAnywhere, Category = Parameters)
		FString EmitterName;

	FORCEINLINE bool operator==(const FNiagaraEmitterNameSettingsRef& Other)const
	{
		return SystemName == Other.SystemName && EmitterName == Other.EmitterName;
	}

	FORCEINLINE bool operator!=(const FNiagaraEmitterNameSettingsRef& Other)const
	{
		return !(*this == Other);
	}
};

FORCEINLINE uint32 GetTypeHash(const FNiagaraEmitterNameSettingsRef& Var)
{
	return HashCombine(GetTypeHash(Var.SystemName), GetTypeHash(Var.EmitterName));
}

UCLASS(config=Game, defaultconfig)
class NIAGARA_API UNiagaraComponentSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	FORCEINLINE static bool ShouldSuppressActivation(const UNiagaraSystem* System)
	{
		check(System != nullptr);
		if ( bAllowSuppressActivation )
		{
			if (const UNiagaraComponentSettings* ComponentSettings = GetDefault<UNiagaraComponentSettings>())
			{
				if (ComponentSettings->SuppressActivationList.Contains(System->GetFName()))
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
	TSet<FName> SuppressActivationList;

	UPROPERTY(config)
	TSet<FName> ForceAutoPooolingList;

	//UPROPERTY(config)
	//TSet<FName> ForceLatencyList;

	static int32 bAllowSuppressActivation;
	static int32 bAllowForceAutoPooling;


	/** 
		Config file to tweak individual emitters being disabled. Syntax is as follows for the config file:
		[/Script/Niagara.NiagaraComponentSettings]
		SuppressEmitterList=((SystemName="BasicSpriteSystem",EmitterName="BasicSprite001"))
	*/
	UPROPERTY(config)
	TSet<FNiagaraEmitterNameSettingsRef> SuppressEmitterList;
};
