// Copyright Epic Games, Inc. All Rights Reserved.
#include "ColorCorrectRegionsSubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "SceneViewExtension.h"
#include "ColorCorrectRegionsSceneViewExtension.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
	bool IsRegionValid(AColorCorrectRegion* InRegion, UWorld* CurrentWorld)
	{
		// There some cases in which actor can belong to a different world or the world without subsystem.
		// Example: when editing a blueprint deriving from AVPCRegion.
		// We also check if the actor is being dragged from the content browser.
#if WITH_EDITOR
		return InRegion && !InRegion->bIsEditorPreviewActor && InRegion->GetWorld() == CurrentWorld;
#else
		return InRegion && InRegion->GetWorld() == CurrentWorld;
#endif
	}
}
void UColorCorrectRegionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
#if WITH_EDITOR
	if (GetWorld()->WorldType == EWorldType::Editor)
	{
		GEngine->OnLevelActorAdded().AddUObject(this, &UColorCorrectRegionsSubsystem::OnActorSpawned);
		GEngine->OnLevelActorDeleted().AddUObject(this, &UColorCorrectRegionsSubsystem::OnActorDeleted);
		GEngine->OnLevelActorListChanged().AddUObject(this, &UColorCorrectRegionsSubsystem::OnLevelActorListChanged);
		GEditor->RegisterForUndo(this);
	}
#endif
	// Initializing Scene view extension responsible for rendering regions.
	PostProcessSceneViewExtension = FSceneViewExtensions::NewExtension<FColorCorrectRegionsSceneViewExtension>(this);
}

void UColorCorrectRegionsSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (GetWorld()->WorldType == EWorldType::Editor)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
		GEngine->OnLevelActorListChanged().RemoveAll(this);
		GEditor->UnregisterForUndo(this);
	}
#endif
	Regions.Reset();
	PostProcessSceneViewExtension.Reset();
	PostProcessSceneViewExtension = nullptr;
}

void UColorCorrectRegionsSubsystem::OnActorSpawned(AActor* InActor)
{
	AColorCorrectRegion* AsRegion = Cast<AColorCorrectRegion>(InActor);

	if (IsRegionValid(AsRegion, GetWorld()))
	{
		FScopeLock RegionScopeLock(&RegionAccessCriticalSection);
		Regions.Add(AsRegion);
		SortRegionsByPriority();
	}
}

void UColorCorrectRegionsSubsystem::OnActorDeleted(AActor* InActor)
{
	AColorCorrectRegion* AsRegion = Cast<AColorCorrectRegion>(InActor);
	if (IsRegionValid(AsRegion, GetWorld()))
	{
		FScopeLock RegionScopeLock(&RegionAccessCriticalSection);
		Regions.Remove(AsRegion);
	}
}

#if WITH_EDITOR
void UColorCorrectRegionsSubsystem::PostUndo(bool bSuccess)
{
	FScopeLock RegionScopeLock(&RegionAccessCriticalSection);

	Regions.Reset();
	for (TActorIterator<AColorCorrectRegion> It(GetWorld()); It; ++It)
	{
		AColorCorrectRegion* AsRegion = *It;
		if (IsRegionValid(AsRegion, GetWorld()))
		{
			Regions.Add(AsRegion);
		}
	}
	SortRegionsByPriority();
}
#endif

void UColorCorrectRegionsSubsystem::SortRegionsByPriority()
{
	FScopeLock RegionScopeLock(&RegionAccessCriticalSection);

	Regions.Sort([](const AColorCorrectRegion& A, const AColorCorrectRegion& B) {
		// Regions with the same priority could potentially cause flickering on overlap
		return A.Priority < B.Priority;
	});
}


