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
#if WITH_EDITOR
#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
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
	virtual bool ShouldBeAlwaysLoaded() const { return bShouldBeAlwaysLoaded; }
	virtual bool ShouldRequireFullVisibilityToRender() const override { return true; }
	void SetShouldBeAlwaysLoaded(bool bInShouldBeAlwaysLoaded) { bShouldBeAlwaysLoaded = bInShouldBeAlwaysLoaded; }
	UWorld* GetOuterWorld() const;

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
	TWeakObjectPtr<UWorldPartition> OuterWorldPartition;
};