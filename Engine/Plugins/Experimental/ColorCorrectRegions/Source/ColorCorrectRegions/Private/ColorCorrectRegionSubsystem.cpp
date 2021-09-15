// Copyright Epic Games, Inc. All Rights Reserved.
#include "ColorCorrectRegionsSubsystem.h"
#include "ColorCorrectRegionDatabase.h"
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
	// In some cases (like nDisplay nodes) EndPlay is not guaranteed to be called when level is removed.
	GetWorld()->OnLevelsChanged().AddUObject(this, &UColorCorrectRegionsSubsystem::OnLevelsChanged);
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
	GetWorld()->OnLevelsChanged().RemoveAll(this);
	for (AColorCorrectRegion* Region : Regions)
	{
		Region->Cleanup();
	}
	Regions.Reset();

	// Prevent this SVE from being gathered, in case it is kept alive by a strong reference somewhere else.
	{
		PostProcessSceneViewExtension->IsActiveThisFrameFunctions.Empty();

		FSceneViewExtensionIsActiveFunctor IsActiveFunctor;

		IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
		{
			return TOptional<bool>(false);
		};

		PostProcessSceneViewExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
	}

	PostProcessSceneViewExtension.Reset();
	PostProcessSceneViewExtension = nullptr;
}

void UColorCorrectRegionsSubsystem::OnActorSpawned(AActor* InActor)
{
	AColorCorrectRegion* AsRegion = Cast<AColorCorrectRegion>(InActor);
	if (IsRegionValid(AsRegion, GetWorld()))
	{
		FScopeLock RegionScopeLock(&RegionAccessCriticalSection);

		// We wouldn't have to do a check here except in case of nDisplay we need to populate this list during OnLevelsChanged 
		// because nDisplay can release Actors while those are marked as BeginningPlay. Therefore we want to avoid 
		// adding regions twice.
		if (!Regions.Contains(AsRegion))
		{
			Regions.Add(AsRegion);
			SortRegionsByPriority();
		}
	}
}

void UColorCorrectRegionsSubsystem::OnActorDeleted(AActor* InActor)
{
	AColorCorrectRegion* AsRegion = Cast<AColorCorrectRegion>(InActor);
	if (AsRegion 
#if WITH_EDITORONLY_DATA
		&& !AsRegion->bIsEditorPreviewActor)
#else
		)
#endif
	{
		FScopeLock RegionScopeLock(&RegionAccessCriticalSection);
		Regions.Remove(AsRegion);

		// Clear all asociated data with this CCR.
		FColorCorrectRegionDatabase::RemoveCCRData(AsRegion);
	}
}

void UColorCorrectRegionsSubsystem::SortRegionsByPriority()
{
	FScopeLock RegionScopeLock(&RegionAccessCriticalSection);

	Regions.Sort([](const AColorCorrectRegion& A, const AColorCorrectRegion& B) {
		// Regions with the same priority could potentially cause flickering on overlap
		return A.Priority < B.Priority;
	});
}

void UColorCorrectRegionsSubsystem::RefreshRegions()
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


