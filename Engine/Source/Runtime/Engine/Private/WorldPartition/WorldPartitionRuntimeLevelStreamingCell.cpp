// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/World.h"

#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Algo/ForEach.h"
#include "AssetCompilingManager.h"
#endif

UWorldPartitionRuntimeLevelStreamingCell::UWorldPartitionRuntimeLevelStreamingCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LevelStreaming(nullptr)
{}

EWorldPartitionRuntimeCellState UWorldPartitionRuntimeLevelStreamingCell::GetCurrentState() const
{
	if (LevelStreaming)
	{
		ULevelStreaming::ECurrentState CurrentStreamingState = LevelStreaming->GetCurrentState();
		if (CurrentStreamingState == ULevelStreaming::ECurrentState::LoadedVisible)
		{
			return EWorldPartitionRuntimeCellState::Activated;
		}
		else if (CurrentStreamingState >= ULevelStreaming::ECurrentState::LoadedNotVisible)
		{
			return EWorldPartitionRuntimeCellState::Loaded;
		}
	}
	
	//@todo_ow: Now that actors are moved to the persistent level, remove the AlwaysLoaded cell (it's always empty)
	return IsAlwaysLoaded() ? EWorldPartitionRuntimeCellState::Activated : EWorldPartitionRuntimeCellState::Unloaded;
}

UWorldPartitionLevelStreamingDynamic* UWorldPartitionRuntimeLevelStreamingCell::GetLevelStreaming() const
{
	return LevelStreaming;
}

EStreamingStatus UWorldPartitionRuntimeLevelStreamingCell::GetStreamingStatus() const
{
	if (LevelStreaming)
	{
		return LevelStreaming->GetLevelStreamingStatus();
	}
	return Super::GetStreamingStatus();
}

bool UWorldPartitionRuntimeLevelStreamingCell::IsLoading() const
{
	if (LevelStreaming)
	{
		ULevelStreaming::ECurrentState CurrentState = LevelStreaming->GetCurrentState();
		return (CurrentState == ULevelStreaming::ECurrentState::Removed || CurrentState == ULevelStreaming::ECurrentState::Unloaded || CurrentState == ULevelStreaming::ECurrentState::Loading);
	}
	return Super::IsLoading();
}

FLinearColor UWorldPartitionRuntimeLevelStreamingCell::GetDebugColor(EWorldPartitionRuntimeCellVisualizeMode VisualizeMode) const
{
	switch (VisualizeMode)
	{
		case EWorldPartitionRuntimeCellVisualizeMode::StreamingPriority:
		{
			return GetDebugStreamingPriorityColor();
		}
		case EWorldPartitionRuntimeCellVisualizeMode::StreamingStatus:
		{
			// Return streaming status color
			FLinearColor Color = LevelStreaming ? ULevelStreaming::GetLevelStreamingStatusColor(GetStreamingStatus()) : FLinearColor::Black;
			Color.A = 0.25f / (Level + 1);
			return Color;
		}
		default:
		{
			return Super::GetDebugColor(VisualizeMode);
		}
	}
}

void UWorldPartitionRuntimeLevelStreamingCell::SetIsAlwaysLoaded(bool bInIsAlwaysLoaded)
{
	Super::SetIsAlwaysLoaded(bInIsAlwaysLoaded);
	if (LevelStreaming)
	{
		LevelStreaming->SetShouldBeAlwaysLoaded(true);
	}
}

#if WITH_EDITOR
void UWorldPartitionRuntimeLevelStreamingCell::AddActorToCell(const FWorldPartitionActorDescView& ActorDescView, const FActorContainerID& InContainerID, const FTransform& InContainerTransform, const UActorDescContainer* InContainer)
{
	check(!ActorDescView.GetActorIsEditorOnly());
	Packages.Emplace(ActorDescView.GetActorPackage(), ActorDescView.GetActorPath(), InContainerID, InContainerTransform, InContainer->GetContainerPackage());
}

UWorldPartitionLevelStreamingDynamic* UWorldPartitionRuntimeLevelStreamingCell::CreateLevelStreaming(const FString& InPackageName) const
{
	if (GetActorCount() > 0)
	{
		const UWorldPartition* WorldPartition = GetOuterUWorldPartition();
		UWorld* OuterWorld = WorldPartition->GetTypedOuter<UWorld>();
		UWorld* OwningWorld = WorldPartition->GetWorld();
		
		const FName LevelStreamingName = FName(*FString::Printf(TEXT("WorldPartitionLevelStreaming_%s"), *GetName()));
		
		// When called by Commandlet (PopulateGeneratedPackageForCook), LevelStreaming's outer is set to Cell/WorldPartition's outer to prevent warnings when saving Cell Levels (Warning: Obj in another map). 
		// At runtime, LevelStreaming's outer will be properly set to the main world (see UWorldPartitionRuntimeLevelStreamingCell::Activate).
		UWorld* LevelStreamingOuterWorld = IsRunningCommandlet() ? OuterWorld : OwningWorld;
		UWorldPartitionLevelStreamingDynamic* NewLevelStreaming = NewObject<UWorldPartitionLevelStreamingDynamic>(LevelStreamingOuterWorld, UWorldPartitionLevelStreamingDynamic::StaticClass(), LevelStreamingName, RF_NoFlags, NULL);
		FString PackageName = !InPackageName.IsEmpty() ? InPackageName : UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(GetFName(), OuterWorld);
		TSoftObjectPtr<UWorld> WorldAsset(FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *PackageName, *OuterWorld->GetName())));
		NewLevelStreaming->SetWorldAsset(WorldAsset);
		// Transfer WorldPartition's transform to Level
		NewLevelStreaming->LevelTransform = WorldPartition->GetInstanceTransform();
		NewLevelStreaming->bClientOnlyVisible = GetClientOnlyVisible();
		NewLevelStreaming->Initialize(*this);

		if (OwningWorld->IsPlayInEditor() && OwningWorld->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor) && OwningWorld->GetPackage()->GetPIEInstanceID() != INDEX_NONE)
		{
			// When renaming for PIE, make sure to keep World's name so that linker can properly remap with Package's instancing context
			NewLevelStreaming->RenameForPIE(OwningWorld->GetPackage()->GetPIEInstanceID(), /*bKeepWorldAssetName*/true);
		}

		return NewLevelStreaming;
	}

	return nullptr;
}

bool UWorldPartitionRuntimeLevelStreamingCell::LoadCellObjectsForCook(TArray<UObject*>& OutLoadedObjects)
{
	check(!bCookHasLoadedCellObjects);
	if (GetActorCount() > 0)
	{
		const bool bLoadAsync = false;
		UWorld* OuterWorld = GetOuterUWorldPartition()->GetTypedOuter<UWorld>();
		verify(FWorldPartitionLevelHelper::LoadActors(OuterWorld, nullptr, Packages, CookPackageReferencer, [](bool) {}, bLoadAsync, FLinkerInstancingContext()));

		TArray<UObject*> ObjectsInPackage;
		for (const FWorldPartitionRuntimeCellObjectMapping& Mapping : Packages)
		{
			if (AActor* Actor = FindObject<AActor>(nullptr, *Mapping.LoadedPath.ToString()))
			{
				OutLoadedObjects.Add(Actor);
				CookLoadedObjects.Add(Actor);

				// Include objects found in the source actor package
				const bool bIncludeNestedSubobjects = false;
				ObjectsInPackage.Reset();
				GetObjectsWithOuter(Actor->GetExternalPackage(), ObjectsInPackage, bIncludeNestedSubobjects);
				for (UObject* Object : ObjectsInPackage)
				{
					if (Object->GetFName() != NAME_PackageMetaData && Object != Actor)
					{
						OutLoadedObjects.Add(Object);
						CookLoadedObjects.Add(Object);
					}
				}
			}
		}
	}
	bCookHasLoadedCellObjects = true;
	return true;
}

bool UWorldPartitionRuntimeLevelStreamingCell::PopulateGeneratorPackageForCook(TArray<UObject*>& OutLoadedObjects)
{
	check(IsAlwaysLoaded());

	if (!LoadCellObjectsForCook(OutLoadedObjects))
	{
		return false;
	}

	check(bCookHasLoadedCellObjects);
	if (GetActorCount() > 0)
	{
		UWorld* OuterWorld = GetOuterUWorldPartition()->GetTypedOuter<UWorld>();
		FWorldPartitionLevelHelper::MoveExternalActorsToLevel(Packages, OuterWorld->PersistentLevel);
	}

	return true;
}

// Do all necessary work to prepare cell object for cook.
bool UWorldPartitionRuntimeLevelStreamingCell::PrepareCellForCook(UPackage* InPackage)
{
	// LevelStreaming could already be created
	if (!LevelStreaming && GetActorCount() > 0)
	{
		if (!InPackage)
		{
			return false;
		}

		LevelStreaming = CreateLevelStreaming(InPackage->GetName());
	}
	return true;
}

bool UWorldPartitionRuntimeLevelStreamingCell::PopulateGeneratedPackageForCook(UPackage* InPackage, TArray<UObject*>& OutLoadedObjects)
{
	check(!IsAlwaysLoaded());
	if (!InPackage)
	{
		return false;
	}

	if (!LoadCellObjectsForCook(OutLoadedObjects))
	{
		return false;
	}

	check(bCookHasLoadedCellObjects);

	if (GetActorCount() > 0)
	{
		// When cook splitter doesn't use deferred populate, cell needs to be prepared here.
		if (!PrepareCellForCook(InPackage))
		{
			return false;
		}

		// Create a level and move loaded actors in it
		UWorldPartition* WorldPartition = GetOuterUWorldPartition();
		UWorld* OuterWorld = WorldPartition->GetTypedOuter<UWorld>();
		ULevel* NewLevel = FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(this, OuterWorld, LevelStreaming->GetWorldAsset().ToString(), InPackage);
		check(NewLevel->GetPackage() == InPackage);
		FWorldPartitionLevelHelper::MoveExternalActorsToLevel(Packages, NewLevel);

		// Remap Level's SoftObjectPaths
		FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(NewLevel, WorldPartition);
	}

	return true;
}

bool UWorldPartitionRuntimeLevelStreamingCell::FinalizePackageForCook()
{
	check(bCookHasLoadedCellObjects);

	// Taken from original code introduced in CL 15771404 :
	//     We can't have async compilation still going on while we move actors as this is going to ResetLoaders which will move bulkdata around that
	//     might still be used by async compilation. 
	// This code was modified to use FAssetCompilingManager instead of FStaticMeshCompilingManager.
	//
	// TODO: Remove once the core team fixes a remaining bug in the COTFS that requires this code to wait for async compilation.
	FAssetCompilingManager::Get().FinishAllCompilation();

	UPackage* LevelPackage = nullptr;
	for (const TWeakObjectPtr<UObject>& LoadedObject : CookLoadedObjects)
	{
		check(LoadedObject.Get());
		AActor* Actor = Cast<AActor>(LoadedObject);
		if (UPackage* ActorExternalPackage = Actor ? Actor->GetExternalPackage() : nullptr)
		{
			UPackage* ActorLevelPackage = Actor->GetLevel()->GetPackage();
			check(!LevelPackage || (LevelPackage == ActorLevelPackage));
			LevelPackage = ActorLevelPackage;

			// Remove package external on actor
			Actor->SetPackageExternal(false, false);

			// Make sure to also include objects found in the source actor package into the level package
			TArray<UObject*> Objects;
			const bool bIncludeNestedSubobjects = false;
			GetObjectsWithOuter(ActorExternalPackage, Objects, bIncludeNestedSubobjects);
			for (UObject* Object : Objects)
			{
				if (Object->GetFName() != NAME_PackageMetaData)
				{
					Object->Rename(nullptr, LevelPackage, REN_ForceNoResetLoaders);
				}
			}
		}
	}

	if (IsAlwaysLoaded())
	{
		// Empty cell's package list (this ensures that no one can rely on cell's content).
		Packages.Empty();
	}

	CookLoadedObjects.Empty();
	CookPackageReferencer.RemoveReferences();
	bCookHasLoadedCellObjects = false;

	return true;
}

int32 UWorldPartitionRuntimeLevelStreamingCell::GetActorCount() const
{
	return Packages.Num();
}

FString UWorldPartitionRuntimeLevelStreamingCell::GetPackageNameToCreate() const
{
	const UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	UWorld* OuterWorld = WorldPartition->GetTypedOuter<UWorld>();
	return UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(GetFName(), OuterWorld);
}

void UWorldPartitionRuntimeLevelStreamingCell::DumpStateLog(FHierarchicalLogArchive& Ar)
{
	Super::DumpStateLog(Ar);

	for (const FWorldPartitionRuntimeCellObjectMapping& Mapping : Packages)
	{
		Ar.Printf(TEXT("Actor Path: %s"), *Mapping.Path.ToString());
		Ar.Printf(TEXT("Actor Package: %s"), *Mapping.Package.ToString());
	}
}
#endif

UWorldPartitionLevelStreamingDynamic* UWorldPartitionRuntimeLevelStreamingCell::GetOrCreateLevelStreaming() const
{
#if WITH_EDITOR
	if (GetActorCount() == 0)
	{
		return nullptr;
	}

	if (!LevelStreaming)
	{
		LevelStreaming = CreateLevelStreaming();
	}
	check(LevelStreaming);
#else
	// In Runtime, always loaded cell level is handled by World directly
	check(LevelStreaming || IsAlwaysLoaded());
#endif

#if !WITH_EDITOR
	// In Runtime, prepare LevelStreaming for activation
	if (LevelStreaming)
	{
		// Setup pre-created LevelStreaming's outer to the WorldPartition owning world
		const UWorldPartition* WorldPartition = GetOuterUWorldPartition();
		UWorld* OwningWorld = WorldPartition->GetWorld();
		if (LevelStreaming->GetWorld() != OwningWorld)
		{
			LevelStreaming->Rename(nullptr, OwningWorld);
		}

		// Transfer WorldPartition's transform to LevelStreaming
		LevelStreaming->LevelTransform = WorldPartition->GetInstanceTransform();

		// When Partition outer level is an instance, make sure to also generate unique cell level instance name
		ULevel* PartitionLevel = WorldPartition->GetTypedOuter<ULevel>();
		if (PartitionLevel->IsInstancedLevel())
		{
			FString PackageShortName = FPackageName::GetShortName(PartitionLevel->GetPackage());
			FString InstancedLevelPackageName = FString::Printf(TEXT("%s_InstanceOf_%s"), *LevelStreaming->PackageNameToLoad.ToString(), *PackageShortName);
			LevelStreaming->SetWorldAssetByPackageName(FName(InstancedLevelPackageName));
		}
	}
#endif

	if (LevelStreaming)
	{
		LevelStreaming->OnLevelShown.AddUniqueDynamic(this, &UWorldPartitionRuntimeLevelStreamingCell::OnLevelShown);
		LevelStreaming->OnLevelHidden.AddUniqueDynamic(this, &UWorldPartitionRuntimeLevelStreamingCell::OnLevelHidden);
	}

	return LevelStreaming;
}

void UWorldPartitionRuntimeLevelStreamingCell::Load() const
{
	if (UWorldPartitionLevelStreamingDynamic* LocalLevelStreaming = GetOrCreateLevelStreaming())
	{
		LocalLevelStreaming->Load();
	}
}

void UWorldPartitionRuntimeLevelStreamingCell::Activate() const
{
	if (UWorldPartitionLevelStreamingDynamic* LocalLevelStreaming = GetOrCreateLevelStreaming())
	{
		LocalLevelStreaming->Activate();
	}
}

bool UWorldPartitionRuntimeLevelStreamingCell::IsAddedToWorld() const
{
	return LevelStreaming && LevelStreaming->GetLoadedLevel() && LevelStreaming->GetLoadedLevel()->bIsVisible;
}

bool UWorldPartitionRuntimeLevelStreamingCell::CanAddToWorld() const
{
	return LevelStreaming &&
		   LevelStreaming->GetLoadedLevel() &&
		   (LevelStreaming->GetCurrentState() == ULevelStreaming::ECurrentState::MakingVisible);
}

void UWorldPartitionRuntimeLevelStreamingCell::SetStreamingPriority(int32 InStreamingPriority) const
{
	if (LevelStreaming)
	{
		LevelStreaming->SetPriority(InStreamingPriority);
	}
}

ULevel* UWorldPartitionRuntimeLevelStreamingCell::GetLevel() const 
{
	return LevelStreaming ? LevelStreaming->GetLoadedLevel() : nullptr;
}

bool UWorldPartitionRuntimeLevelStreamingCell::CanUnload() const
{
	if (LevelStreaming)
	{
		if (UHLODSubsystem* HLODSubsystem = LevelStreaming->GetWorld()->GetSubsystem<UHLODSubsystem>())
		{
			return HLODSubsystem->RequestUnloading(this);
		}
	}

	return true;
}

void UWorldPartitionRuntimeLevelStreamingCell::Unload() const
{
#if WITH_EDITOR
	if (GetActorCount() == 0)
	{
		return;
	}
	check(LevelStreaming);
#else
	// In Runtime, always loaded cell level is handled by World directly
	check(LevelStreaming || IsAlwaysLoaded());
#endif

	if (LevelStreaming)
	{
		LevelStreaming->Unload();
	}
}

void UWorldPartitionRuntimeLevelStreamingCell::Deactivate() const
{
#if WITH_EDITOR
	if (GetActorCount() == 0)
	{
		return;
	}
	check(LevelStreaming);
#else
	// In Runtime, always loaded cell level is handled by World directly
	check(LevelStreaming || IsAlwaysLoaded());
#endif

	if (LevelStreaming)
	{
		LevelStreaming->Deactivate();
	}
}

void UWorldPartitionRuntimeLevelStreamingCell::OnLevelShown()
{
	check(LevelStreaming);
	LevelStreaming->GetWorld()->GetSubsystem<UHLODSubsystem>()->OnCellShown(this);
}

void UWorldPartitionRuntimeLevelStreamingCell::OnLevelHidden()
{
	check(LevelStreaming);
	LevelStreaming->GetWorld()->GetSubsystem<UHLODSubsystem>()->OnCellHidden(this);
}