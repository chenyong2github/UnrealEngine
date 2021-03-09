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

UCLASS()
class UWorldPartitionLevelStreamingPolicy : public UWorldPartitionStreamingPolicy
{
	GENERATED_BODY()

public:
	virtual void LoadCells(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells) override;
	virtual void LoadCell(const UWorldPartitionRuntimeCell* Cell) override;
	virtual void UnloadCell(const UWorldPartitionRuntimeCell* Cell) override;
	virtual class ULevel* GetPreferredLoadedLevelToAddToWorld() const override;

#if WITH_EDITOR
	virtual TSubclassOf<class UWorldPartitionRuntimeCell> GetRuntimeCellClass() const override;
	virtual void OnBeginPlay() override;
	virtual void OnEndPlay() override;
	virtual void RemapSoftObjectPath(FSoftObjectPath& ObjectPath) override;
	static FString GetCellPackagePath(const FName& InCellName, const UWorld* InWorld);
#endif

private:
	int32 GetCellLoadingCount() const;

#if WITH_EDITOR
	TMap<FName, FName> ActorToCellRemapping;
#endif
};