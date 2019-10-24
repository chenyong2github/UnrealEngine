// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayCueNotify_HitImpact.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayCueManager.h"
#include "AbilitySystemGlobals.h"

UGameplayCueNotify_HitImpact::UGameplayCueNotify_HitImpact(const FObjectInitializer& PCIP)
: Super(PCIP)
{

}

bool UGameplayCueNotify_HitImpact::HandlesEvent(EGameplayCueEvent::Type EventType) const
{
	return (EventType == EGameplayCueEvent::Executed);
}

void UGameplayCueNotify_HitImpact::HandleGameplayCue(AActor* Self, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
	check(EventType == EGameplayCueEvent::Executed);
	
	const UObject* WorldContextObject = Self;
	if (!WorldContextObject)
	{
		WorldContextObject = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	}

	if (ParticleSystem && WorldContextObject)
	{
		const FHitResult* HitResult = Parameters.EffectContext.GetHitResult();
		if (HitResult)
		{
			UGameplayStatics::SpawnEmitterAtLocation(WorldContextObject, ParticleSystem, HitResult->ImpactPoint, HitResult->ImpactNormal.Rotation(), true);
		}
		else
		{
			const FVector Location = Self ? Self->GetActorLocation() : Parameters.Location;
			const FRotator Rotation = Self ? Self->GetActorRotation() : Parameters.Normal.Rotation();
			UGameplayStatics::SpawnEmitterAtLocation(WorldContextObject, ParticleSystem, Location, Rotation, true);
		}
	}
}
