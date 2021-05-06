// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/NavigationData/NavigationDataChunkActor.h"
#include "AI/NavigationSystemBase.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

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

#if WITH_EDITOR
void ANavigationDataChunkActor::PostLoad()
{
	Super::PostLoad();

	if (GEditor)
	{
		const bool bIsInPIE = (GEditor->PlayWorld != NULL) && (!GEditor->bIsSimulatingInEditor);
		if (!bIsInPIE)
		{
			Log(ANSI_TO_TCHAR(__FUNCTION__));
			UE_LOG(LogNavigation, Verbose, TEXT("   pos: %s ext: %s"), *DataChunkActorBounds.GetCenter().ToCompactString(), *DataChunkActorBounds.GetExtent().ToCompactString());
			AddNavigationDataChunkToWorld();
		}
	}
}

void ANavigationDataChunkActor::BeginDestroy()
{
	if (GEditor)
	{
		const bool bIsInPIE = (GEditor->PlayWorld != NULL) && (!GEditor->bIsSimulatingInEditor);
		if (!bIsInPIE)
		{
			Log(ANSI_TO_TCHAR(__FUNCTION__));
			RemoveNavigationDataChunkFromWorld();
		}
	}

	Super::BeginDestroy();
}
#endif // WITH_EDITOR

void ANavigationDataChunkActor::CollectNavData(const FBox& QueryBounds, FBox& OutTilesBounds)
{
	Log(ANSI_TO_TCHAR(__FUNCTION__));

	UWorld* World = GetWorld();
	if (World)
	{
		if (UNavigationSystemBase* NavSys = World->GetNavigationSystem())
		{
			NavSys->FillNavigationDataChunkActor(QueryBounds, *this, OutTilesBounds);
		}
	}
}

#if WITH_EDITOR
void ANavigationDataChunkActor::SetDataChunkActorBounds(const FBox& InBounds)
{
	DataChunkActorBounds = InBounds;
}
#endif //WITH_EDITOR

void ANavigationDataChunkActor::BeginPlay()
{
	Log(ANSI_TO_TCHAR(__FUNCTION__));
	Super::BeginPlay();
	AddNavigationDataChunkToWorld();
}

void ANavigationDataChunkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Log(ANSI_TO_TCHAR(__FUNCTION__));
	RemoveNavigationDataChunkFromWorld();
	Super::EndPlay(EndPlayReason);
}

void ANavigationDataChunkActor::AddNavigationDataChunkToWorld()
{
	UWorld* World = GetWorld();
	if (World)
	{
		if (UNavigationSystemBase* NavSys = World->GetNavigationSystem())
		{
			NavSys->AddNavigationDataChunk(*this);
		}
	}
}

void ANavigationDataChunkActor::RemoveNavigationDataChunkFromWorld()
{
	UWorld* World = GetWorld();
	if (World)
	{
		if (UNavigationSystemBase* NavSys = World->GetNavigationSystem())
		{
			NavSys->RemoveNavigationDataChunk(*this);
		}
	}
}

void ANavigationDataChunkActor::Log(const TCHAR* FunctionName) const
{
	UE_LOG(LogNavigation, Verbose, TEXT("[%s] %s"), *GetName(), FunctionName);
}

void ANavigationDataChunkActor::GetActorBounds(bool bOnlyCollidingComponents, FVector& OutOrigin, FVector& OutBoxExtent, bool bIncludeFromChildActors) const
{
	DataChunkActorBounds.GetCenterAndExtents(OutOrigin, OutBoxExtent);
}

#if WITH_EDITOR
EActorGridPlacement ANavigationDataChunkActor::GetDefaultGridPlacement() const
{
	return EActorGridPlacement::Bounds;
}

FBox ANavigationDataChunkActor::GetStreamingBounds() const
{
	return DataChunkActorBounds;
}
#endif // WITH_EDITOR
