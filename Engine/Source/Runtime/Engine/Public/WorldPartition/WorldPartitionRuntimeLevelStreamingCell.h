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
	virtual bool CanUnload() const override;
	virtual void Activate() const override;
	virtual void Deactivate() const override;
	virtual bool IsAddedToWorld() const override;
	virtual bool CanAddToWorld() const override;
	virtual ULevel* GetLevel() const override;
	virtual EWorldPartitionRuntimeCellState GetCurrentState() const override;
	virtual FLinearColor GetDebugColor(EWorldPartitionRuntimeCellVisualizeMode VisualizeMode) const override;
	virtual void SetIsAlwaysLoaded(bool bInIsAlwaysLoaded) override;
	virtual EStreamingStatus GetStreamingStatus() const override;
	virtual bool IsLoading() const override;
	//~End UWorldPartitionRuntimeCell Interface

	virtual void SetStreamingPriority(int32 InStreamingPriority) const override;
	class UWorldPartitionLevelStreamingDynamic* GetLevelStreaming() const;
	

#if WITH_EDITOR
	//~Begin UWorldPartitionRuntimeCell Interface
	virtual void AddActorToCell(const FWorldPartitionActorDescView& ActorDescView, const FActorContainerID& InContainerID, const FTransform& InContainerTransform, const UActorDescContainer* InContainer) override;
	virtual int32 GetActorCount() const override;
	// Cook methods
	virtual bool PrepareCellForCook(UPackage* InPackage) override;
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage) override;
	virtual FString GetPackageNameToCreate() const override;
	//~End UWorldPartitionRuntimeCell Interface

	const TArray<FWorldPartitionRuntimeCellObjectMapping>& GetPackages() const { return Packages; }
	const TSet<FGuid>& GetActorFolders() const { return ActorFolders; }
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
	class UWorldPartitionLevelStreamingDynamic* CreateLevelStreaming(const FString& InPackageName = FString()) const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FWorldPartitionRuntimeCellObjectMapping> Packages;

	UPROPERTY()
	TSet<FGuid> ActorFolders;
#endif

	UPROPERTY()
	mutable TObjectPtr<class UWorldPartitionLevelStreamingDynamic> LevelStreaming;
};