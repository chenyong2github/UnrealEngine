// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayCueNotify_Instanced.h"
#include "Engine/World.h"
#include "GameplayCueManager.h"
#include "GameplayCueNotifyTypes.h"


//////////////////////////////////////////////////////////////////////////
// AGameplayCueNotify_Instanced
//////////////////////////////////////////////////////////////////////////
AGameplayCueNotify_Instanced::AGameplayCueNotify_Instanced()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	PrimaryActorTick.bStartWithTickEnabled = false;
	bAutoDestroyOnRemove = true;
	NumPreallocatedInstances = 3;
}

void AGameplayCueNotify_Instanced::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameplayCueManager::IsGameplayCueRecylingEnabled() && (EndPlayReason == EEndPlayReason::Destroyed))
	{
		UWorld* World = GetWorld();
		if (World && !World->IsPlayingReplay() && (World->WorldType == EWorldType::Game))
		{
			UE_LOG(LogGameplayCueNotify, Error, TEXT("GameplayCueNotify [%s] is calling EndPlay(). This should not happen since they are recycled through the GameplayCueManager."), *GetName());
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AGameplayCueNotify_Instanced::K2_DestroyActor()
{
	UE_LOG(LogGameplayCueNotify, Warning, TEXT("GameplayCueNotify [%s] is calling DesotryActor(). This is not necessary as GCs will be cleaned up and recycled automatically."), *GetName());
}

void AGameplayCueNotify_Instanced::Destroyed()
{
	if (UGameplayCueManager::IsGameplayCueRecylingEnabled())
	{
		UWorld* World = GetWorld();
		if (World && !World->IsPlayingReplay() && (World->WorldType == EWorldType::Game))
		{
			UE_LOG(LogGameplayCueNotify, Error, TEXT("GameplayCueNotify [%s] is calling Destroyed(). This should not happen since they are recycled through the GameplayCueManager."), *GetName());
		}
	}

	Super::Destroyed();
}

bool AGameplayCueNotify_Instanced::WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	if (IsHidden())
	{
		SetActorHiddenInGame(false);
	}

	return false;
}

bool AGameplayCueNotify_Instanced::OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	if (!IsHidden())
	{
		SetActorHiddenInGame(true);
	}

	return false;
}
