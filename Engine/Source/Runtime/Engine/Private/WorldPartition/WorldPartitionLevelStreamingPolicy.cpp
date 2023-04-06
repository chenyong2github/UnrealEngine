// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionLevelStreamingPolicy implementation
 */

#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "Engine/Level.h"
#include "Engine/Canvas.h"
#include "Engine/LevelStreamingGCHelper.h"
#if WITH_EDITOR
#include "Misc/PackageName.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldPartitionLevelStreamingPolicy)

int32 UWorldPartitionLevelStreamingPolicy::GetCellLoadingCount() const
{
	int32 CellLoadingCount = 0;

	ForEachActiveRuntimeCell([&CellLoadingCount](const UWorldPartitionRuntimeCell* Cell)
	{
		if (Cell->IsLoading())
		{
			++CellLoadingCount;
		}
	});
	return CellLoadingCount;
}

void UWorldPartitionLevelStreamingPolicy::ForEachActiveRuntimeCell(TFunctionRef<void(const UWorldPartitionRuntimeCell*)> Func) const
{
	UWorld* World = WorldPartition->GetWorld();
	for (ULevelStreaming* LevelStreaming : World->GetStreamingLevels())
	{
		if (UWorldPartitionLevelStreamingDynamic* WorldPartitionLevelStreaming = Cast<UWorldPartitionLevelStreamingDynamic>(LevelStreaming))
		{
			if (const UWorldPartitionRuntimeCell* Cell = WorldPartitionLevelStreaming->GetWorldPartitionRuntimeCell())
			{
				Func(Cell);
			}
		}
	}
}

bool UWorldPartitionLevelStreamingPolicy::IsStreamingCompleted(const TArray<FWorldPartitionStreamingSource>* InStreamingSources) const
{
	const UWorld* World = GetWorld();
	check(World);
	check(World->IsGameWorld());

	if (!Super::IsStreamingCompleted(InStreamingSources))
	{
		return false;
	}

	if (!InStreamingSources)
	{
		// Also verify that there's no remaining activity (mainly for unloading) 
		// waiting to be processed on level streaming of world partition runtime cells
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			ULevel* Level = StreamingLevel ? StreamingLevel->GetLoadedLevel() : nullptr;
			if (Level && Level->IsWorldPartitionRuntimeCell() && StreamingLevel->IsStreamingStatePending())
			{
				return false;
			}
		}
	}
	return true;
}

#if WITH_EDITOR

FString UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(const FName& InCellName, const UWorld* InWorld)
{
	if (InWorld->IsGameWorld())
	{
		// Set as memory package to avoid wasting time in FPackageName::DoesPackageExist
		return FString::Printf(TEXT("/Memory/%s"), *InCellName.ToString());
	}
	else
	{
		return FString::Printf(TEXT("/%s"), *InCellName.ToString());
	}
}

TSubclassOf<UWorldPartitionRuntimeCell> UWorldPartitionLevelStreamingPolicy::GetRuntimeCellClass() const
{
	return UWorldPartitionRuntimeLevelStreamingCell::StaticClass();
}

void UWorldPartitionLevelStreamingPolicy::PrepareActorToCellRemapping()
{
	FString SourceWorldPath, DummyUnusedPath;
	WorldPartition->GetTypedOuter<UWorld>()->GetSoftObjectPathMapping(SourceWorldPath, DummyUnusedPath);
	SourceWorldAssetPath = FTopLevelAssetPath(SourceWorldPath);
	
	// Build Actor-to-Cell remapping
	WorldPartition->RuntimeHash->ForEachStreamingCells([this, &SourceWorldPath](const UWorldPartitionRuntimeCell* Cell)
	{
		const UWorldPartitionRuntimeLevelStreamingCell* StreamingCell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(Cell);
		check(StreamingCell);
		for (const FWorldPartitionRuntimeCellObjectMapping& CellObjectMap : StreamingCell->GetPackages())
		{
			// The use cases for remapping are the following:
			//
			// - Spatially loaded or Datalayer Actors from the main World Partition map that get moved into a Streaming Cell. In thise case an actor path like:
			//		- '/Game/SomePath/WorldName.WorldName:PersistentLevel.ActorA' would be mapped to a cell name ex: 'WorldName_MainGrid_L0_X5_Y-4'
			// - Always loaded Actors from the main World:
			//		- In PIE they get remapped to the top level Cell 'WorldName_MainGrid_L{MAX}_X0_Y0'
			//		- In Cook they don't need remapping as the top level Cell is the PersistentLevel (Cell->NeedsActorToCellRemapping() returns false)
			if (Cell->NeedsActorToCellRemapping())
			{
				const FSoftObjectPath CellActorPath = FWorldPartitionLevelHelper::RemapActorPath(CellObjectMap.ContainerID, SourceWorldPath, FSoftObjectPath(CellObjectMap.Path.ToString()));
				
				const FString ActorPath = CellActorPath.ToString();
				const FName CellName = StreamingCell->GetFName();
				const int32 LastDotPos = ActorPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				check(LastDotPos != INDEX_NONE);
				SubObjectsToCellRemapping.Add(FName(*ActorPath.Mid(LastDotPos + 1)), CellName);
			}
		}
		return true;
	});
}

void UWorldPartitionLevelStreamingPolicy::RemapSoftObjectPath(FSoftObjectPath& ObjectPath) const
{
	const FSoftObjectPath SrcPath(ObjectPath);
	ConvertEditorPathToRuntimePath(SrcPath, ObjectPath);
}
#endif

bool UWorldPartitionLevelStreamingPolicy::ConvertEditorPathToRuntimePath(const FSoftObjectPath& InPath, FSoftObjectPath& OutPath) const
{
	// Make sure to work on non-PIE path (can happen for modified actors in PIE)
	const UWorld* OuterWorld = WorldPartition->GetTypedOuter<UWorld>();
	const UPackage* OuterWorldPackage = OuterWorld->GetPackage();
	FTopLevelAssetPath WorldAssetPath(OuterWorld);

#if WITH_EDITOR
	const int32 PIEInstanceID = OuterWorldPackage->GetPIEInstanceID();
	check(PIEInstanceID == INDEX_NONE || OuterWorld->IsPlayInEditor());
	int32 PathPIEInstanceID = INDEX_NONE;
	WorldAssetPath = UWorld::RemovePIEPrefix(WorldAssetPath.ToString(), &PathPIEInstanceID);
	check(PathPIEInstanceID == INDEX_NONE || OuterWorldPackage->HasAnyPackageFlags(PKG_PlayInEditor));
	check(PathPIEInstanceID == PIEInstanceID);

	FString SrcPath = UWorld::RemovePIEPrefix(InPath.ToString(), &PathPIEInstanceID);
	check(PathPIEInstanceID == INDEX_NONE || PathPIEInstanceID == PIEInstanceID);
	const FSoftObjectPath SrcObjectPath(SrcPath);
#else
	const FSoftObjectPath SrcObjectPath(InPath);
#endif

	// Allow remapping of instanced source path or non-instanced source path
	if (SrcObjectPath.GetAssetPath() != SourceWorldAssetPath && 
		SrcObjectPath.GetAssetPath() != WorldAssetPath)
	{
		return false;
	}

	// In the editor, the _LevelInstance_ID is appended to the persistent level, while at runtime it is appended to each cell package, so we need to remap it there if present.
	FString LevelInstanceSuffix;
	const FString WorldAssetPackageName = WorldAssetPath.GetPackageName().ToString();
	const FString SourceWorldAssetPackageName = SourceWorldAssetPath.GetPackageName().ToString();
	if (WorldAssetPackageName.StartsWith(SourceWorldAssetPackageName))
	{
		LevelInstanceSuffix = WorldAssetPackageName.Mid(SourceWorldAssetPackageName.Len());
	}

	FString SubAssetName;
	FString SubAssetContext;
	if (SrcObjectPath.GetSubPathString().Split(TEXT("."), &SubAssetContext, &SubAssetName))
	{
		if (SubAssetContext == TEXT("PersistentLevel"))
		{
			FString SubObjectName;
			FString SubObjectContext(SubAssetName);
			SubAssetName.Split(TEXT("."), &SubObjectContext, &SubObjectName);

			// Try to find the corresponding streaming cell, if it doesn't exists the actor must be in the persistent level.
			const FName* CellName = SubObjectsToCellRemapping.Find(*SubObjectContext);
			if (!CellName)
			{
				OutPath = FSoftObjectPath(WorldAssetPath, InPath.GetSubPathString());
			}
#if WITH_EDITOR
			else if (OuterWorld->IsGameWorld())
			{
				const FString PackagePath = UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(*CellName, OuterWorld);
				OutPath = FString::Printf(TEXT("%s.%s:%s"), *PackagePath, *OuterWorld->GetName(), *InPath.GetSubPathString());
			}
#endif
			else
			{
				OutPath = FString::Printf(TEXT("%s/_Generated_/%s%s.%s:%s"), *SourceWorldAssetPackageName, *(CellName->ToString()), *LevelInstanceSuffix, *WorldAssetPath.GetAssetName().ToString(), *InPath.GetSubPathString());
			}

#if WITH_EDITOR
			OutPath.FixupForPIE(PIEInstanceID);
#endif
			return true;
		}
	}

	return false;
}

UObject* UWorldPartitionLevelStreamingPolicy::GetSubObject(const TCHAR* SubObjectPath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionLevelStreamingPolicy::GetSubObject);

	// Support for subobjects such as Actor.Component
	FString SubObjectName;
	FString SubObjectContext;
	if (!FString(SubObjectPath).Split(TEXT("."), &SubObjectContext, &SubObjectName))
	{
		SubObjectContext = SubObjectPath;
	}

	const FString SrcPath = UWorld::RemovePIEPrefix(*SubObjectContext);
	if (FName* CellName = SubObjectsToCellRemapping.Find(FName(*SrcPath)))
	{
		if (UWorldPartitionRuntimeLevelStreamingCell* Cell = (UWorldPartitionRuntimeLevelStreamingCell*)StaticFindObject(UWorldPartitionRuntimeLevelStreamingCell::StaticClass(), GetOuterUWorldPartition()->RuntimeHash, *(CellName->ToString())))
		{
			if (UWorldPartitionLevelStreamingDynamic* LevelStreaming = Cell->GetLevelStreaming())
			{
				if (LevelStreaming->GetLoadedLevel())
				{
					return StaticFindObject(UObject::StaticClass(), LevelStreaming->GetLoadedLevel(), SubObjectPath);
				}
			}
		}
	}

	return nullptr;
}

void UWorldPartitionLevelStreamingPolicy::DrawRuntimeCellsDetails(UCanvas* Canvas, FVector2D& Offset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionLevelStreamingPolicy::DrawRuntimeCellsDetails);

	UWorld* World = WorldPartition->GetWorld();
	struct FCellsPerStreamingStatus
	{
		TArray<const UWorldPartitionRuntimeCell*> Cells;
	};
	FCellsPerStreamingStatus CellsPerStreamingStatus[(int32)LEVEL_StreamingStatusCount];
	ForEachActiveRuntimeCell([&CellsPerStreamingStatus](const UWorldPartitionRuntimeCell* Cell)
	{
		if (Cell->IsDebugShown())
		{
			CellsPerStreamingStatus[(int32)Cell->GetStreamingStatus()].Cells.Add(Cell);
		}
	});

	FVector2D Pos = Offset;
	const float BaseY = Offset.Y;

	float CurrentColumnWidth = 0.f;
	float MaxPosY = Pos.Y;

	auto DrawCellDetails = [&](const FString& Text, const UFont* Font, const FColor& Color)
	{
		FWorldPartitionDebugHelper::DrawText(Canvas, Text, Font, Color, Pos, &CurrentColumnWidth);
		MaxPosY = FMath::Max(MaxPosY, Pos.Y);
		if ((Pos.Y + 30) > Canvas->ClipY)
		{
			Pos.Y = BaseY;
			Pos.X += CurrentColumnWidth + 5;
			CurrentColumnWidth = 0.f;
		}
	};

	for (int32 i = 0; i < (int32)LEVEL_StreamingStatusCount; ++i)
	{
		const EStreamingStatus StreamingStatus = (EStreamingStatus)i;
		const TArray<const UWorldPartitionRuntimeCell*>& Cells = CellsPerStreamingStatus[i].Cells;
		if (Cells.Num() > 0)
		{
			const FString StatusDisplayName = *FString::Printf(TEXT("%s (%d)"), ULevelStreaming::GetLevelStreamingStatusDisplayName(StreamingStatus), Cells.Num());
			DrawCellDetails(StatusDisplayName, GEngine->GetSmallFont(), FColor::Yellow);

			const FColor Color = ULevelStreaming::GetLevelStreamingStatusColor(StreamingStatus);
			for (const UWorldPartitionRuntimeCell* Cell : Cells)
			{
				if ((StreamingStatus == EStreamingStatus::LEVEL_Loaded) ||
					(StreamingStatus == EStreamingStatus::LEVEL_MakingVisible) ||
					(StreamingStatus == EStreamingStatus::LEVEL_Visible) ||
					(StreamingStatus == EStreamingStatus::LEVEL_MakingInvisible))
				{
					float LoadTime = Cell->GetLevel() ? Cell->GetLevel()->GetPackage()->GetLoadTime() : 0.f;
					DrawCellDetails(FString::Printf(TEXT("%s (%s)"), *Cell->GetDebugName(), *FPlatformTime::PrettyTime(LoadTime)), GEngine->GetTinyFont(), Color);
				}
				else
				{
					DrawCellDetails(Cell->GetDebugName(), GEngine->GetTinyFont(), Color);
				}
			}
		}
	}

	Offset.Y = MaxPosY;
}

/**
 * Debug Draw Streaming Status Legend
 */
void UWorldPartitionLevelStreamingPolicy::DrawStreamingStatusLegend(UCanvas* Canvas, FVector2D& Offset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionLevelStreamingPolicy::DrawStreamingStatusLegend);

	check(Canvas);

	// Cumulate counter stats
	int32 StatusCount[(int32)LEVEL_StreamingStatusCount] = { 0 };
	ForEachActiveRuntimeCell([&StatusCount](const UWorldPartitionRuntimeCell* Cell)
	{
		StatusCount[(int32)Cell->GetStreamingStatus()]++;
	});

	// @todo_ow: This is not exactly the good value, as we could have pending unload level from Level Instances, etc.
	//           We could modify GetNumLevelsPendingPurge to return the number of pending purge levels from the grid, 
	//           bu that will do for now.
	StatusCount[LEVEL_UnloadedButStillAround] = FLevelStreamingGCHelper::GetNumLevelsPendingPurge();

	// Draw legend
	FVector2D Pos = Offset;
	float MaxTextWidth = 0.f;
	FWorldPartitionDebugHelper::DrawText(Canvas, TEXT("Streaming Status Legend"), GEngine->GetSmallFont(), FColor::Yellow, Pos, &MaxTextWidth);
	
	for (int32 i = 0; i < (int32)LEVEL_StreamingStatusCount; ++i)
	{
		EStreamingStatus Status = (EStreamingStatus)i;
		const FColor& StatusColor = ULevelStreaming::GetLevelStreamingStatusColor(Status);
		FString DebugString = *FString::Printf(TEXT("%d) %s"), i, ULevelStreaming::GetLevelStreamingStatusDisplayName(Status));
		if (Status != LEVEL_Unloaded)
		{
			DebugString += *FString::Printf(TEXT(" (%d)"), StatusCount[(int32)Status]);
		}
		FWorldPartitionDebugHelper::DrawLegendItem(Canvas, *DebugString, GEngine->GetSmallFont(), StatusColor, FColor::White, Pos, &MaxTextWidth);
	}

	Offset.X += MaxTextWidth + 10;
}
