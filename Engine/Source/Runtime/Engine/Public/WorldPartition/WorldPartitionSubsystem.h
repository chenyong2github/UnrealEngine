// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartitionStreamingSource.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldPartitionSubsystem.generated.h"

class UWorldPartition;

enum class EWorldPartitionRuntimeCellState : uint8;

/**
 * UWorldPartitionSubsystem
 */

UCLASS()
class ENGINE_API UWorldPartitionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UWorldPartitionSubsystem();

	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface.

	//~ Begin UWorldSubsystem Interface.
	virtual void UpdateStreamingState() override;
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	//~End FTickableGameObject

	UFUNCTION(BlueprintCallable, Category = Streaming)
	bool IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const;

	void DumpStreamingSources(FOutputDevice& OutputDevice) const;

	TArray<IWorldPartitionStreamingSourceProvider*> GetStreamingSourceProviders() const { return StreamingSourceProviders; }
	void RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);
	bool UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);

#if WITH_EDITOR
	void ForEachWorldPartition(TFunctionRef<bool(UWorldPartition*)> Func);

	static bool IsRunningConvertWorldPartitionCommandlet();
#endif

private:

	void OnWorldPartitionInitialized(UWorldPartition* InWorldPartition);
	void OnWorldPartitionUninitialized(UWorldPartition* InWorldPartition);

	UWorldPartition* GetWorldPartition();
	const UWorldPartition* GetWorldPartition() const;
	void Draw(class UCanvas* Canvas, class APlayerController* PC);
	friend class UWorldPartition;

	TArray<TObjectPtr<UWorldPartition>> RegisteredWorldPartitions;

	TArray<IWorldPartitionStreamingSourceProvider*> StreamingSourceProviders;

	FDelegateHandle	DrawHandle;

	// GC backup values
	int32 LevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge;
	int32 LevelStreamingForceGCAfterLevelStreamedOut;

#if WITH_EDITOR
	bool bIsRunningConvertWorldPartitionCommandlet;
#endif
};
