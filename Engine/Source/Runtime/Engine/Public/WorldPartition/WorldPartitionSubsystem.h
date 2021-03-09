// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Misc/Optional.h"
#include "WorldPartitionSubsystem.generated.h"

class UWorldPartition;
class UWorldPartitionEditorCell;
class FWorldPartitionActorDesc;

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
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface.

	//~ Begin UWorldSubsystem Interface.
	virtual void PostInitialize() override;
	virtual void UpdateStreamingState() override;
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	//~End FTickableGameObject

#if WITH_EDITOR
	void ForEachIntersectingActorDesc(const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const;
	void ForEachActorDesc(TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const;
#endif
	void ToggleDrawRuntimeHash2D();

private:
	UWorldPartition* GetMainWorldPartition();
	const UWorldPartition* GetMainWorldPartition() const;
	void RegisterWorldPartition(UWorldPartition* WorldPartition);
	void UnregisterWorldPartition(UWorldPartition* WorldPartition);
	void Draw(class UCanvas* Canvas, class APlayerController* PC);
	friend class UWorldPartition;

	UPROPERTY()
	TArray<TObjectPtr<UWorldPartition>> RegisteredWorldPartitions;

	FDelegateHandle	DrawHandle;

	struct FWorldPartitionCVars
	{
		FWorldPartitionCVars() {}

		void ReadFromCVars();
		void WriteToCVars() const;

		static const TCHAR* ContinuouslyIncrementalText;
		static const TCHAR* ForceGCAfterLevelStreamedOutText;
		static const TCHAR* TimeBetweenPurgingPendingKillObjectsText;

		TOptional<int32> ContinuouslyIncremental;
		TOptional<int32> ForceGCAfterLevelStreamedOut;
		TOptional<float> TimeBetweenPurgingPendingKillObjects;
	};

	FWorldPartitionCVars PreviousCVarValues;
};
