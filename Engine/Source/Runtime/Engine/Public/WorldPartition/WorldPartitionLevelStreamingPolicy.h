// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * WorldPartitionLevelStreamingPolicy
 *
 * World Partition Streaming Policy that handles load/unload of streaming cell using a corresponding Streaming Level and 
 * a Level that will contain all streaming cell's content.
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartitionLevelStreamingPolicy.generated.h"

enum class EWorldPartitionRuntimeCellState : uint8;

UCLASS()
class UWorldPartitionLevelStreamingPolicy : public UWorldPartitionStreamingPolicy
{
	GENERATED_BODY()

public:
	virtual void SetTargetStateForCells(EWorldPartitionRuntimeCellState TargetState, const TSet<const UWorldPartitionRuntimeCell*>& Cells) override;
	virtual EWorldPartitionRuntimeCellState GetCurrentStateForCell(const UWorldPartitionRuntimeCell* Cell) const override;
	virtual class ULevel* GetPreferredLoadedLevelToAddToWorld() const override;

#if WITH_EDITOR
	virtual TSubclassOf<class UWorldPartitionRuntimeCell> GetRuntimeCellClass() const override;
	virtual void PrepareActorToCellRemapping() override;
	virtual void ClearActorToCellRemapping() override;
	virtual void RemapSoftObjectPath(FSoftObjectPath& ObjectPath) override;
	static FString GetCellPackagePath(const FName& InCellName, const UWorld* InWorld);
#endif

	virtual UObject* GetSubObject(const TCHAR* SubObjectPath) override;

private:
	void SetCellsStateToLoaded(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells);
	void SetCellsStateToActivated(const TSet<const UWorldPartitionRuntimeCell*>& ToActivateCells);
	void SetCellsStateToUnloaded(const TSet<const UWorldPartitionRuntimeCell*>& ToUnloadCells);
	int32 GetCellLoadingCount() const;
	int32 GetMaxCellsToLoad() const;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TMap<FName, FName> ActorToCellRemapping;
#endif

	UPROPERTY()
	TMap<FName, FName> SubObjectsToCellRemapping;
};