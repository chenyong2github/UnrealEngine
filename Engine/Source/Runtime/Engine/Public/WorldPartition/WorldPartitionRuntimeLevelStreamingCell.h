// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"
#include "ProfilingDebugging/ProfilingHelpers.h"
#include "WorldPartitionRuntimeLevelStreamingCell.generated.h"

UCLASS()
class UWorldPartitionRuntimeLevelStreamingCell : public UWorldPartitionRuntimeSpatialHashCell
{
	GENERATED_UCLASS_BODY()

	//~Begin UWorldPartitionRuntimeCell Interface
	virtual void Load() const override;
	virtual void Unload() const override;
	virtual void Activate() const override;
	virtual void Deactivate() const override;
	virtual bool IsAddedToWorld() const override;
	virtual bool CanAddToWorld() const override;
	virtual ULevel* GetLevel() const override;
	virtual EWorldPartitionRuntimeCellState GetCurrentState() const override;
	virtual FLinearColor GetDebugColor() const override;
	virtual void SetIsAlwaysLoaded(bool bInIsAlwaysLoaded) override;
	virtual EStreamingStatus GetStreamingStatus() const override;
	virtual bool IsLoading() const override;
	//~End UWorldPartitionRuntimeCell Interface

	int32 GetStreamingPriority() const { return Level; }
	class UWorldPartitionLevelStreamingDynamic* GetLevelStreaming() const;
	

#if WITH_EDITOR
	//~Begin UWorldPartitionRuntimeCell Interface
	virtual void AddActorToCell(const FWorldPartitionActorDescView& ActorDescView, uint64 InContainerID, const FTransform& InContainerTransform, const UActorDescContainer* InContainer) override;
	virtual int32 GetActorCount() const override;
	// Cook methods
	virtual bool PrepareCellForCook(UPackage* InPackage) override;
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage) override;
	virtual FString GetPackageNameToCreate() const override;
	//~End UWorldPartitionRuntimeCell Interface

	const TArray<FWorldPartitionRuntimeCellObjectMapping>& GetPackages() const { return Packages; }
#endif

private:
	UFUNCTION()
	void OnLevelShown();
	
	UFUNCTION()
	void OnLevelHidden();

	class UWorldPartitionLevelStreamingDynamic* GetOrCreateLevelStreaming() const;

#if WITH_EDITOR
	void LoadActorsForCook();
	void MoveAlwaysLoadedContentToPersistentLevel();
	class ULevelStreaming* CreateLevelStreaming(const FString& InPackageName = FString()) const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FWorldPartitionRuntimeCellObjectMapping> Packages;
#endif

	UPROPERTY()
	mutable TObjectPtr<class UWorldPartitionLevelStreamingDynamic> LevelStreaming;
};