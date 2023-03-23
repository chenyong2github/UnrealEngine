// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartitionStreamingSource.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/Filter/WorldPartitionActorFilter.h"
#include "WorldPartition/WorldPartitionActorContainerID.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Misc/Guid.h"
#include "WorldPartitionSubsystem.generated.h"

class UWorldPartition;
class UActorDescContainer;
class FWorldPartitionActorDesc;

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

	//~ Begin UObject Interface
#if WITH_EDITOR
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
#endif
	//~ End UObject Interface

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

	/** Returns true if world partition is done streaming levels, adding them to the world or removing them from the world. */
	UFUNCTION(BlueprintCallable, Category = Streaming)
	bool IsAllStreamingCompleted();

	/*
	 * Returns true if world partition is done streaming levels, adding them to the world or removing them from the world. 
	 * When provided, the test is reduced to streaming levels affected by the optional streaming source provider.
	 */
	bool IsStreamingCompleted(const IWorldPartitionStreamingSourceProvider* InStreamingSourceProvider = nullptr) const;

	void DumpStreamingSources(FOutputDevice& OutputDevice) const;

	TSet<IWorldPartitionStreamingSourceProvider*> GetStreamingSourceProviders() const;
	void RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);
	bool IsStreamingSourceProviderRegistered(IWorldPartitionStreamingSourceProvider* StreamingSource) const;
	bool UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);

	DECLARE_DELEGATE_RetVal_OneParam(bool, FWorldPartitionStreamingSourceProviderFilter, const IWorldPartitionStreamingSourceProvider*);
	FWorldPartitionStreamingSourceProviderFilter& OnIsStreamingSourceProviderFiltered() { return IsStreamingSourceProviderFiltered; }

	void ForEachWorldPartition(TFunctionRef<bool(UWorldPartition*)> Func);

#if WITH_EDITOR
	FWorldPartitionActorFilter GetWorldPartitionActorFilter(const FString& InWorldPackage) const;
	TMap<FActorContainerID, TSet<FGuid>> GetFilteredActorsPerContainer(const FActorContainerID& InContainerID, const FString& InWorldPackage, const FWorldPartitionActorFilter& InActorFilter);

	static bool IsRunningConvertWorldPartitionCommandlet();

	UActorDescContainer* RegisterContainer(FName PackageName) { return ActorDescContainerInstanceManager.RegisterContainer(PackageName, GetWorld()); }
	void UnregisterContainer(UActorDescContainer* Container) { ActorDescContainerInstanceManager.UnregisterContainer(Container); }
	FBox GetContainerBounds(FName PackageName) const { return ActorDescContainerInstanceManager.GetContainerBounds(PackageName); }
	void UpdateContainerBounds(FName PackageName) { ActorDescContainerInstanceManager.UpdateContainerBounds(PackageName); }

	TSet<FWorldPartitionActorDesc*> SelectedActorDescs;

	class FActorDescContainerInstanceManager
	{
		friend class UWorldPartitionSubsystem;

		struct FActorDescContainerInstance
		{
			FActorDescContainerInstance()
				: Container(nullptr)
				, RefCount(0)
				, Bounds(ForceInit)
			{}

			void AddReferencedObjects(FReferenceCollector& Collector);
			void UpdateBounds();

			UActorDescContainer* Container;
			uint32 RefCount;
			FBox Bounds;
		};

		void AddReferencedObjects(FReferenceCollector& Collector);

	public:
		UActorDescContainer* RegisterContainer(FName PackageName, UWorld* InWorld);
		void UnregisterContainer(UActorDescContainer* Container);

		FBox GetContainerBounds(FName PackageName) const;
		void UpdateContainerBounds(FName PackageName);

	private:
		TMap<FName, FActorDescContainerInstance> ActorDescContainers;
	};
private:
	FWorldPartitionActorFilter GetWorldPartitionActorFilterInternal(const FString& InWorldPackage, TSet<FString>& InOutVisitedPackages) const;
#endif

protected:
	//~ Begin USubsystem Interface.
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End USubsystem Interface.

private:

	void OnWorldPartitionInitialized(UWorldPartition* InWorldPartition);
	void OnWorldPartitionUninitialized(UWorldPartition* InWorldPartition);

	UWorldPartition* GetWorldPartition();
	const UWorldPartition* GetWorldPartition() const;
	void Draw(class UCanvas* Canvas, class APlayerController* PC);
	friend class UWorldPartition;

	TArray<TObjectPtr<UWorldPartition>> RegisteredWorldPartitions;

	TSet<IWorldPartitionStreamingSourceProvider*> StreamingSourceProviders;

	FWorldPartitionStreamingSourceProviderFilter IsStreamingSourceProviderFiltered;

	FDelegateHandle	DrawHandle;

	// GC backup values
	int32 LevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge;
	int32 LevelStreamingForceGCAfterLevelStreamedOut;

#if WITH_EDITOR
	bool bIsRunningConvertWorldPartitionCommandlet;
	mutable FActorDescContainerInstanceManager ActorDescContainerInstanceManager;
#endif
};
