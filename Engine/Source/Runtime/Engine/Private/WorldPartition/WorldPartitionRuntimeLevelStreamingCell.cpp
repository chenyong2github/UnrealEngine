// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/World.h"

#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#endif

UWorldPartitionRuntimeLevelStreamingCell::UWorldPartitionRuntimeLevelStreamingCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LevelStreaming(nullptr)
{
}

UWorldPartitionLevelStreamingDynamic* UWorldPartitionRuntimeLevelStreamingCell::GetLevelStreaming() const
{
	return LevelStreaming;
}

EStreamingStatus UWorldPartitionRuntimeLevelStreamingCell::GetLevelStreamingStatus() const
{
	check(LevelStreaming)
	return LevelStreaming->GetLevelStreamingStatus();
}

FLinearColor UWorldPartitionRuntimeLevelStreamingCell::GetDebugColor() const
{
	FLinearColor Color = LevelStreaming ? ULevelStreaming::GetLevelStreamingStatusColor(GetLevelStreamingStatus()) : FLinearColor::Black;
	Color.A = 0.25f / (Level + 1);
	return Color;
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

void UWorldPartitionRuntimeLevelStreamingCell::AddActorToCell(FName Package, FName Path)
{
	Packages.Emplace(Package, Path);
}

ULevelStreaming* UWorldPartitionRuntimeLevelStreamingCell::CreateLevelStreaming() const
{
	if (GetActorCount() > 0)
	{
		const UWorldPartition* WorldPartition = GetOuterUWorldPartition();
		UWorld* OuterWorld = WorldPartition->GetTypedOuter<UWorld>();
		UWorld* OwningWorld = WorldPartition->GetWorld();
		
		const FName LevelStreamingName = FName(*FString::Printf(TEXT("WorldPartitionLevelStreaming_%s"), *GetName()));
		const UClass* LevelStreamingClass = (IsRunningCommandlet() && IsAlwaysLoaded() && (OuterWorld == OwningWorld)) ? ULevelStreamingAlwaysLoaded::StaticClass() : UWorldPartitionLevelStreamingDynamic::StaticClass();
		
		// When called by Commandlet (CreateCellForCook), LevelStreaming's outer is set to Cell/WorldPartition's outer to prevent warnings when saving Cell Levels (Warning: Obj in another map). 
		// At runtime, LevelStreaming's outer will be properly set to the main world (see UWorldPartitionRuntimeLevelStreamingCell::Activate).
		UWorld* LevelStreamingOuterWorld = IsRunningCommandlet() ? OuterWorld : OwningWorld;
		ULevelStreaming* NewLevelStreaming = NewObject<ULevelStreaming>(LevelStreamingOuterWorld, LevelStreamingClass, LevelStreamingName, RF_NoFlags, NULL);
		TSoftObjectPtr<UWorld> WorldAsset(FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(GetFName(), OuterWorld), *OuterWorld->GetName())));
		NewLevelStreaming->SetWorldAsset(WorldAsset);
		// Transfer WorldPartition's transform to Level
		NewLevelStreaming->LevelTransform = WorldPartition->GetInstanceTransform();

		if (UWorldPartitionLevelStreamingDynamic* WorldPartitionLevelStreamingDynamic = Cast<UWorldPartitionLevelStreamingDynamic>(NewLevelStreaming))
		{
			WorldPartitionLevelStreamingDynamic->Initialize(*this);
		}

		if (OwningWorld->IsPlayInEditor() && OwningWorld->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor) && OwningWorld->GetPackage()->PIEInstanceID != INDEX_NONE)
		{
			// When renaming for PIE, make sure to keep World's name so that linker can properly remap with Package's instancing context
			NewLevelStreaming->RenameForPIE(OwningWorld->GetPackage()->PIEInstanceID, /*bKeepWorldAssetName*/true);
		}

		return NewLevelStreaming;
	}

	return nullptr;
}

bool UWorldPartitionRuntimeLevelStreamingCell::CreateCellForCook()
{
	if (ULevelStreaming* NewLevelStreaming = CreateLevelStreaming())
	{
		LevelStreaming = Cast<UWorldPartitionLevelStreamingDynamic>(NewLevelStreaming);
		const UWorldPartition* WorldPartition = GetOuterUWorldPartition();
		UWorld* OuterWorld = WorldPartition->GetTypedOuter<UWorld>();
		if (!FWorldPartitionLevelHelper::CreateAndSaveLevelForRuntimeCell(OuterWorld, NewLevelStreaming->GetWorldAsset().ToString(), Packages))
		{
			return false;
		}

		// Always loaded streaming levels are added to world's streaming level to make sure that they get streamed in as soon as world is loaded.
		if (NewLevelStreaming->IsA<ULevelStreamingAlwaysLoaded>())
		{
			OuterWorld->AddStreamingLevel(NewLevelStreaming);
		}
	}
	return true;
}

int32 UWorldPartitionRuntimeLevelStreamingCell::GetActorCount() const
{
	return Packages.Num();
}

#endif

void UWorldPartitionRuntimeLevelStreamingCell::Activate() const
{
#if WITH_EDITOR
	if (!GetOuterUWorldPartition()->IsPreCooked())
	{
		if (GetActorCount() == 0)
		{
			return;
		}

		if (!LevelStreaming)
		{
			LevelStreaming = Cast<UWorldPartitionLevelStreamingDynamic>(CreateLevelStreaming());
		}
		check(LevelStreaming);
	}
#else
	// In Runtime, always loaded cell level is handled by World directly
	check(LevelStreaming || IsAlwaysLoaded());
#endif

#if WITH_EDITOR
	if (GetOuterUWorldPartition()->IsPreCooked())
#endif
	{
		// in Runtime (or PreCooked), prepare LevelStreaming for activation
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
	}

	if (LevelStreaming)
	{
		if (const UWorldPartitionRuntimeHLODCellData* HLODCellData = GetCellData<UWorldPartitionRuntimeHLODCellData>())
		{
			LevelStreaming->OnLevelShown.AddUniqueDynamic(this, &UWorldPartitionRuntimeLevelStreamingCell::OnLevelShown);
			LevelStreaming->OnLevelHidden.AddUniqueDynamic(this, &UWorldPartitionRuntimeLevelStreamingCell::OnLevelHidden);
		}

		LevelStreaming->Activate();
	}
}

void UWorldPartitionRuntimeLevelStreamingCell::Deactivate() const
{
#if WITH_EDITOR
	if (!GetOuterUWorldPartition()->IsPreCooked())
	{
		if (GetActorCount() == 0)
		{
			return;
		}
		check(LevelStreaming);
	}
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
