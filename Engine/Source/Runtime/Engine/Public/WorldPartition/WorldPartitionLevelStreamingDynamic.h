// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * WorldPartitionLevelStreamingDynamic
 *
 * Dynamically controlled world partition level streaming implementation.
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#if WITH_EDITOR
#include "WorldPartition/WorldPartitionPackageCache.h"
#endif
#include "WorldPartitionLevelStreamingDynamic.generated.h"

UCLASS()
class ENGINE_API UWorldPartitionLevelStreamingDynamic : public ULevelStreamingDynamic
{
	GENERATED_UCLASS_BODY()

	void Load();
	void Unload();
	void Activate();
	void Deactivate();
	UWorld* GetOuterWorld() const;
	void SetShouldBeAlwaysLoaded(bool bInShouldBeAlwaysLoaded) { bShouldBeAlwaysLoaded = bInShouldBeAlwaysLoaded; }
	const UWorldPartitionRuntimeCell* GetWorldPartitionRuntimeCell() const { return StreamingCell.Get(); }

	virtual bool ShouldBeAlwaysLoaded() const override { return bShouldBeAlwaysLoaded; }
	virtual bool ShouldRequireFullVisibilityToRender() const override { return true; }

#if WITH_EDITOR
	void Initialize(const UWorldPartitionRuntimeLevelStreamingCell& InCell);

	// Override ULevelStreaming
	virtual bool RequestLevel(UWorld* PersistentWorld, bool bAllowLevelLoadRequests, EReqLevelBlock BlockPolicy) override;
	virtual void BeginDestroy() override;

private:
	void CreateRuntimeLevel();
	bool IssueLoadRequests();
	void FinalizeRuntimeLevel();
	void OnCleanupLevel();

	FName OriginalLevelPackageName;
	TArray<FWorldPartitionRuntimeCellObjectMapping> ChildPackages;
	TArray<FWorldPartitionRuntimeCellObjectMapping> ChildPackagesToLoad;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<ULevel> RuntimeLevel;
#endif

	FDelegateHandle OnCleanupLevelDelegateHandle;
	bool bLoadRequestInProgress;
	FWorldPartitionPackageCache PackageCache;
#endif

	UPROPERTY()
	bool bShouldBeAlwaysLoaded;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UActorContainer* ActorContainer;
#endif

	UPROPERTY()
	TWeakObjectPtr<const UWorldPartitionRuntimeLevelStreamingCell> StreamingCell;

	UPROPERTY()
	TWeakObjectPtr<UWorldPartition> OuterWorldPartition;
};