// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartitionEditorHash.generated.h"

class UWorldPartitionEditorCell;

UCLASS(Abstract, Config=Engine, Within = WorldPartition)
class ENGINE_API UWorldPartitionEditorHash : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual void Initialize() PURE_VIRTUAL(UWorldPartitionEditorHash::Initialize, return;);
	virtual void SetDefaultValues() PURE_VIRTUAL(UWorldPartitionEditorHash::SetDefaultValues, return;);
	virtual FName GetWorldPartitionEditorName() PURE_VIRTUAL(UWorldPartitionEditorHash::GetWorldPartitionEditorName, return FName(NAME_None););
	virtual FBox GetEditorWorldBounds() const  PURE_VIRTUAL(UWorldPartitionEditorHash::GetEditorWorldBounds, return FBox(ForceInit););
	virtual void Tick(float DeltaSeconds) PURE_VIRTUAL(UWorldPartitionEditorHash::Tick, return;);

	virtual void HashActor(FWorldPartitionHandle& InActorHandle) PURE_VIRTUAL(UWorldPartitionEditorHash::HashActor, ;);
	virtual void UnhashActor(FWorldPartitionHandle& InActorHandle) PURE_VIRTUAL(UWorldPartitionEditorHash::UnhashActor, ;);

	virtual void OnCellLoaded(const UWorldPartitionEditorCell* Cell) PURE_VIRTUAL(UWorldPartitionEditorHash::OnCellLoaded, ;);
	virtual void OnCellUnloaded(const UWorldPartitionEditorCell* Cell) PURE_VIRTUAL(UWorldPartitionEditorHash::OnCellLoaded, ;);

	virtual int32 ForEachIntersectingActor(const FBox& Box, TFunctionRef<void(FWorldPartitionActorDesc*)> InOperation) PURE_VIRTUAL(UWorldPartitionEditorHash::ForEachIntersectingActor, return 0;);
	virtual int32 ForEachIntersectingCell(const FBox& Box, TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation) PURE_VIRTUAL(UWorldPartitionEditorHash::ForEachIntersectingCell, return 0;);
	virtual int32 ForEachCell(TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation) PURE_VIRTUAL(UWorldPartitionEditorHash::ForEachCell, return 0;);
	virtual UWorldPartitionEditorCell* GetAlwaysLoadedCell() PURE_VIRTUAL(UWorldPartitionEditorHash::GetAlwaysLoadedCell, return nullptr;);

	// Helpers
	inline int32 GetIntersectingActors(const FBox& Box, TArray<FWorldPartitionActorDesc*>& OutActors)
	{
		return ForEachIntersectingActor(Box, [&OutActors](FWorldPartitionActorDesc* ActorDesc)
		{
			OutActors.Add(ActorDesc);
		});
	}

	int32 GetIntersectingCells(const FBox& Box, TArray<UWorldPartitionEditorCell*>& OutCells)
	{
		return ForEachIntersectingCell(Box, [&OutCells](UWorldPartitionEditorCell* Cell)
		{
			OutCells.Add(Cell);
		});
	}

	int32 GetAllCells(TArray<UWorldPartitionEditorCell*>& OutCells)
	{
		return ForEachCell([&OutCells](UWorldPartitionEditorCell* Cell)
		{
			OutCells.Add(Cell);
		});
	}
#endif
};
