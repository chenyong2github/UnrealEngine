// Copyright Epic Games, Inc. All Rights Reserved.

#include "Restorability/BlacklistRestorabilityOverrider.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

ISnapshotRestorabilityOverrider::ERestorabilityOverride FBlacklistRestorabilityOverrider::IsActorDesirableForCapture(const AActor* Actor)
{
	const FRestorationBlacklist& Blacklist = GetBlacklistCallback.Execute();

	for (UClass* ActorClass = Actor->GetClass(); ActorClass; ActorClass = ActorClass->GetSuperClass())
	{
		if (Blacklist.ActorClasses.Contains(ActorClass))
		{
			return ERestorabilityOverride::Disallow;
		}
	}
	
	return ERestorabilityOverride::DoNotCare;
}

ISnapshotRestorabilityOverrider::ERestorabilityOverride FBlacklistRestorabilityOverrider::IsComponentDesirableForCapture(const UActorComponent* Component)
{
	const FRestorationBlacklist& Blacklist = GetBlacklistCallback.Execute();

	for (UClass* ComponentClass = Component->GetClass(); ComponentClass; ComponentClass = ComponentClass->GetSuperClass())
	{
		if (Blacklist.ComponentClasses.Contains(ComponentClass))
		{
			return ERestorabilityOverride::Disallow;
		}
	}
	
	return ERestorabilityOverride::DoNotCare;
}