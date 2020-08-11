// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"
#include "ProfilingDebugging/ProfilingHelpers.h"
#include "WorldPartitionRuntimeLevelStreamingCell.generated.h"

UCLASS()
class UWorldPartitionRuntimeLevelStreamingCell : public UWorldPartitionRuntimeSpatialHashCell
{
	GENERATED_UCLASS_BODY()

	void Activate() const;
	void Deactivate() const;
	int32 GetStreamingPriority() const { return Level; }
	class UWorldPartitionLevelStreamingDynamic* GetLevelStreaming() const;
	virtual FLinearColor GetDebugColor() const override;

#if WITH_EDITOR
	virtual void AddActorToCell(FName Package, FName Path) override;
	virtual bool CreateCellForCook() override;
	virtual int32 GetActorCount() const override;
	const TArray<FWorldPartitionRuntimeCellObjectMapping>& GetPackages() const { return Packages; }
#endif

	virtual void SetIsAlwaysLoaded(bool bInIsAlwaysLoaded) override;

private:
	UFUNCTION()
	void OnLevelShown();
	
	UFUNCTION()
	void OnLevelHidden();

	EStreamingStatus GetLevelStreamingStatus() const;

#if WITH_EDITOR
	class ULevelStreaming* CreateLevelStreaming() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FWorldPartitionRuntimeCellObjectMapping> Packages;
#endif

	UPROPERTY()
	mutable class UWorldPartitionLevelStreamingDynamic* LevelStreaming;
};