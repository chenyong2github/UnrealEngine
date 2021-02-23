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
	virtual void AddActorToCell(const FGuid& InActorGuid, uint32 InContainerID, const FTransform& InContainerTransform, const UActorDescContainer* InContainer) override;
	virtual int32 GetActorCount() const override;
	const TArray<FWorldPartitionRuntimeCellObjectMapping>& GetPackages() const { return Packages; }

	// Cook methods
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageCookName) override;
	virtual FString GetPackageNameToCreate() const override;
#endif

	virtual void SetIsAlwaysLoaded(bool bInIsAlwaysLoaded) override;

private:
	UFUNCTION()
	void OnLevelShown();
	
	UFUNCTION()
	void OnLevelHidden();

	EStreamingStatus GetLevelStreamingStatus() const;

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