// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISnapshotRestorabilityOverrider.h"
#include "Settings/RestorationBlacklist.h"

/* Disallows provided classes and properties. Uses callback to obtain blacklist so the logic is reusable outside the module. */
class LEVELSNAPSHOTS_API FBlacklistRestorabilityOverrider : public ISnapshotRestorabilityOverrider
{
public:

	DECLARE_DELEGATE_RetVal(const FRestorationBlacklist&, FGetBlacklist)
	
	FBlacklistRestorabilityOverrider(FGetBlacklist GetBlacklistCallback)
		:
		GetBlacklistCallback(GetBlacklistCallback)
	{
		check(GetBlacklistCallback.IsBound());
	}
	
	//~ Begin ISnapshotRestorabilityOverrider Interface
	virtual ERestorabilityOverride IsActorDesirableForCapture(const AActor* Actor) override;
	virtual ERestorabilityOverride IsComponentDesirableForCapture(const UActorComponent* Component) override;
	//~ End ISnapshotRestorabilityOverrider Interface

private:

	FGetBlacklist GetBlacklistCallback;

};
