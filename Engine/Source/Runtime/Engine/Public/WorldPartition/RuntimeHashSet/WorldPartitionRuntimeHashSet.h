// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/RuntimeHashSet/StaticSpatialIndex.h"
#include "WorldPartitionRuntimeHashSet.generated.h"

class UHLODLayer;
class URuntimePartition;
class URuntimePartitionPersistent;
struct FPropertyChangedChainEvent;

using FStaticSpatialIndexType = TStaticSpatialIndexRTree<UWorldPartitionRuntimeCell*>;

/** Holds settings for an HLOD layer (and its parents) for a particular partition class. */
USTRUCT()
struct FRuntimePartitionHLODSetup
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = RuntimeSettings, EditFixedSize, Instanced, meta = (NoResetToDefault, EditFixedOrder, TitleProperty="Name"))
	TArray<TObjectPtr<URuntimePartition>> Partitions;
};

/** Holds settings for a runtime partition instance. */
USTRUCT()
struct FRuntimePartitionDesc
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	/** Partition class */
	UPROPERTY(EditAnywhere, Category = RuntimeSettings)
	TSubclassOf<URuntimePartition> Class;

	/** Name for this partition, used to map actors to it through the Actor.RuntimeGrid property  */
	UPROPERTY(EditAnywhere, Category = RuntimeSettings, meta = (EditCondition = "Class != nullptr", HideEditConditionToggle))
	FName Name;

	/** Array of partition objects, first index is the main one and then one per HLOD layer */
	UPROPERTY(VisibleAnywhere, Category = RuntimeSettings, EditFixedSize, Instanced, meta = (EditCondition = "Class != nullptr", HideEditConditionToggle, NoResetToDefault, EditFixedOrder, TitleProperty="Name"))
	TArray<TObjectPtr<URuntimePartition>> Partitions;

	/** HLOD setups used by this partition */
	UPROPERTY(EditAnywhere, Category = RuntimeSettings, meta = (EditCondition = "Class != nullptr", HideEditConditionToggle, ForceInlineRow, DisplayThumbnail=false))
	TMap<TObjectPtr<const UHLODLayer>, FRuntimePartitionHLODSetup> HLODSetups;
#endif
};

UCLASS()
class URuntimeHashSetExternalStreamingObject : public URuntimeHashExternalStreamingObjectBase
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<UWorldPartitionRuntimeCell>> NonSpatiallyLoadedRuntimeCells;

	UPROPERTY()
	TArray<TObjectPtr<UWorldPartitionRuntimeCell>> SpatiallyLoadedRuntimeCells;

	// Transient
	TUniquePtr<FStaticSpatialIndexType> SpatialIndex;
};

UCLASS(HideDropdown)
class ENGINE_API UWorldPartitionRuntimeHashSet : public UWorldPartitionRuntimeHash
{
	GENERATED_UCLASS_BODY()

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

public:
#if WITH_EDITOR
	// Streaming generation interface
	virtual void SetDefaultValues() override;
	virtual bool SupportsHLODs() const override;
	virtual bool GenerateStreaming(class UWorldPartitionStreamingPolicy* StreamingPolicy, const IStreamingGenerationContext* StreamingGenerationContext, TArray<FString>* OutPackagesToGenerate) override;
	virtual void FlushStreaming() override;
	virtual bool IsValidGrid(FName GridName) const;
	virtual TArray<UWorldPartitionRuntimeCell*> GetAlwaysLoadedCells() const override;
	virtual void DumpStateLog(FHierarchicalLogArchive& Ar) const override;

	// Helpers
	static TArray<FName> ParseGridName(FName GridName);
#endif

	// External streaming object interface
#if WITH_EDITOR
	virtual URuntimeHashExternalStreamingObjectBase* StoreToExternalStreamingObject(UObject* StreamingObjectOuter, FName StreamingObjectName) override;
#endif
	virtual bool InjectExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* ExternalStreamingObject) override;
	virtual bool RemoveExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* ExternalStreamingObject) override;

	// Streaming interface
	virtual void ForEachStreamingCells(TFunctionRef<bool(const UWorldPartitionRuntimeCell*)> Func) const;
	virtual void ForEachStreamingCellsQuery(const FWorldPartitionStreamingQuerySource& QuerySource, TFunctionRef<bool(const UWorldPartitionRuntimeCell*)> Func, FWorldPartitionQueryCache* QueryCache) const override;
	virtual void ForEachStreamingCellsSources(const TArray<FWorldPartitionStreamingSource>& Sources, TFunctionRef<bool(const UWorldPartitionRuntimeCell*, EStreamingSourceTargetState)> Func) const override;

private:
#if WITH_EDITOR
	/** Update the partition layers to reflect the curent HLOD setups. */
	void SetupHLODPartitionLayers(int32 RuntimePartitionIndex);
#endif

public:
#if WITH_EDITORONLY_DATA
	/** Persistent partition */
	UPROPERTY()
	FRuntimePartitionDesc PersistentPartitionDesc;

	/** Array of runtime partition descriptors */
	UPROPERTY(EditAnywhere, Category = RuntimeSettings, meta = (TitleProperty="Name"))
	TArray<FRuntimePartitionDesc> RuntimePartitions;
#endif

	UPROPERTY()
	TArray<TObjectPtr<UWorldPartitionRuntimeCell>> NonSpatiallyLoadedRuntimeCells;

	TUniquePtr<FStaticSpatialIndexType> SpatialIndex;

	TSet<URuntimeHashSetExternalStreamingObject*> InjectedExternalStreamingObjects;
};