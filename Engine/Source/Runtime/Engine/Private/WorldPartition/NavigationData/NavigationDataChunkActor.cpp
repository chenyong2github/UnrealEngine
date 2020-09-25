// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/NavigationData/NavigationDataChunkActor.h"
#include "AI/NavigationSystemBase.h"

ANavigationDataChunkActor::ANavigationDataChunkActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!IsTemplate())
	{
		USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(USceneComponent::GetDefaultSceneRootVariableName());
		Root->SetMobility(EComponentMobility::Static);
		SetRootComponent(Root);
	}

	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
}

void ANavigationDataChunkActor::Serialize(FArchive& Ar)
{
	UE_LOG(LogNavigation, VeryVerbose, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));

	Super::Serialize(Ar);
}

void ANavigationDataChunkActor::PostLoad()
{
	UE_LOG(LogNavigation, VeryVerbose, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));

	Super::PostLoad();
}

void ANavigationDataChunkActor::CollectNavData(const FBox& Bounds)
{
	UE_LOG(LogNavigation, VeryVerbose, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));

	UWorld* World = GetWorld();
	if (World)
	{
		if (UNavigationSystemBase* NavSys = World->GetNavigationSystem())
		{
			NavSys->FillNavigationDataChunkActor(Bounds, *this);
		}
	}
}

void ANavigationDataChunkActor::BeginPlay()
{
	UE_LOG(LogNavigation, VeryVerbose, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));

	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World)
	{
		if (UNavigationSystemBase* NavSys = World->GetNavigationSystem())
		{
			NavSys->AddNavigationDataChunk(*this);
		}
	}
}

void ANavigationDataChunkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogNavigation, VeryVerbose, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));

	UWorld* World = GetWorld();
	if (World)
	{
		if (UNavigationSystemBase* NavSys = World->GetNavigationSystem())
		{
			NavSys->RemoveNavigationDataChunk(*this);
		}
	}

	Super::EndPlay(EndPlayReason);
}


#if WITH_EDITOR
EActorGridPlacement ANavigationDataChunkActor::GetDefaultGridPlacement() const
{
	return EActorGridPlacement::Location;
}
#endif // WITH_EDITOR
