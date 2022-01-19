// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectTypes.h"
#include "ZoneGraphAnnotationComponent.h"
#include "ZoneGraphTypes.h"
#include "SmartObjectZoneAnnotations.generated.h"

class AZoneGraphData;
class USmartObjectSubsystem;

/** Minimal amount of data to search and compare lane locations within the same graph. */
USTRUCT()
struct FSmartObjectLaneLocation
{
	GENERATED_BODY()

	UPROPERTY()
	int32 LaneIndex = INDEX_NONE;

	UPROPERTY()
	float DistanceAlongLane = 0.0f;
};

/** Simple struct to wrap the container to be able to use in a TMap */
USTRUCT()
struct FSmartObjectList
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TArray<FSmartObjectHandle> SmartObjects;
};

/** Per ZoneGraphData smart object look up data. */
USTRUCT()
struct FSmartObjectAnnotationData
{
	GENERATED_BODY()

	/** @return True if this entry is valid (associated to a valid zone graph data), false otherwise. */
	bool IsValid() const { return DataHandle.IsValid(); }

	/** Reset all internal data. */
	void Reset()
	{
		DataHandle = {};
		AffectedLanes.Reset();
		ObjectToEntryPointLookup.Reset();
		LaneToSmartObjectsLookup.Reset();
	}

	/** Handle of the ZoneGraphData that this smart object annotation data is associated to */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	FZoneGraphDataHandle DataHandle;

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TArray<int32> AffectedLanes;

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TMap<FSmartObjectHandle, FSmartObjectLaneLocation> ObjectToEntryPointLookup;

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TMap<int32, FSmartObjectList> LaneToSmartObjectsLookup;

	bool bInitialTaggingCompleted = false;
};

/**
 * ZoneGraph annotations for smart objects
 */
UCLASS(ClassGroup = AI, BlueprintType, meta = (BlueprintSpawnableComponent))
class MASSSMARTOBJECTS_API USmartObjectZoneAnnotations : public UZoneGraphAnnotationComponent
{
	GENERATED_BODY()

public:
	const FSmartObjectAnnotationData* GetAnnotationData(FZoneGraphDataHandle DataHandle) const;

protected:
	virtual void PostSubsystemsInitialized() override;
	virtual FZoneGraphTagMask GetAnnotationTags() const override;
	virtual void TickAnnotation(const float DeltaTime, FZoneGraphAnnotationTagContainer& BehaviorTagContainer) override;

	virtual void PostZoneGraphDataAdded(const AZoneGraphData& ZoneGraphData) override;
	virtual void PreZoneGraphDataRemoved(const AZoneGraphData& ZoneGraphData) override;

	/** Filter specifying which lanes the behavior is applied to. */
	UPROPERTY(EditAnywhere, Category = SmartObject)
	FZoneGraphTagFilter AffectedLaneTags;

	/** Entry points graph for each ZoneGraphData. */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TArray<FSmartObjectAnnotationData> SmartObjectAnnotationDataArray;

	/** Tag to mark the lanes that offers smart objects. */
	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	FZoneGraphTag BehaviorTag;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void OnUnregister() override;

	void RebuildForSingleGraph(FSmartObjectAnnotationData& Data, const FZoneGraphStorage& Storage);
	void RebuildForAllGraphs();

	FDelegateHandle OnAnnotationSettingsChangedHandle;
	FDelegateHandle OnGraphDataChangedHandle;
	FDelegateHandle OnMainCollectionChangedHandle;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	/** Cached SmartObjectSubsystem */
	UPROPERTY(Transient)
	USmartObjectSubsystem* SmartObjectSubsystem = nullptr;
#endif // WITH_EDITORONLY_DATA
};
