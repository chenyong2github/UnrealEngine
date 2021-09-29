// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassAIMovementTypes.h"
#include "ZoneGraphAnnotationTypes.h"
#include "ZoneGraphAnnotationComponent.h"

#include "MassZoneGraphLaneObstacleAnnotations.generated.h"

class UZoneGraphSubsystem;

/**
* Uniquely identified obstacles on lanes.
*/
struct MASSAIMOVEMENT_API FMassLaneObstacle
{
	FMassLaneObstacle() = default;
	FMassLaneObstacle(const FMassLaneObstacleID NewID, const FZoneGraphLaneSection& Section)
		: LaneSection(Section)
		, ID(NewID)
	{}

	/** Returns the unique ID for that obstacle. */
	FMassLaneObstacleID GetID() const { return ID; }

	bool operator==(const FMassLaneObstacle& Other) const
	{
		return LaneSection == Other.LaneSection && ID == Other.ID;
	}
	
	/** Part of the lane that is blocked. */
	FZoneGraphLaneSection LaneSection;

protected:
	/** Lane obstacle ID. */
	FMassLaneObstacleID ID;
};

/**
 * Event indicating an obstacle change on a lane.
 */
USTRUCT()
struct MASSAIMOVEMENT_API FZoneGraphLaneObstacleChangeEvent : public FZoneGraphAnnotationEventBase
{
	GENERATED_BODY()

	FZoneGraphLaneObstacleChangeEvent() = default;
	FZoneGraphLaneObstacleChangeEvent(const FMassLaneObstacle Obstacle, const EMassLaneObstacleEventAction Action)
		: LaneObstacle(Obstacle)
		, EventAction(Action)
	{}

	/** Part of the lane that is blocked. */
	FMassLaneObstacle LaneObstacle;

	/** Action for that lane obstacle. */
	EMassLaneObstacleEventAction EventAction;
};

/**
 * Container to get obstacles from LaneHandle
 */
class FMassLaneObstacleContainer
{
public:
	/** Add a FMassLaneObstacle */
	void Add(const FMassLaneObstacle& Obstacle)
	{
		IDToLaneObstacleMap.Add(Obstacle.GetID(), Obstacle);
		
		TArray<FMassLaneObstacle>& Obstacles = LaneObstaclesMap.FindOrAdd(Obstacle.LaneSection.LaneHandle);
		Obstacles.Add(Obstacle);
	}

	/** Remove a FMassLaneObstacle using it's ID, returns true if it was the last one on the lane. */
	bool Remove(const FMassLaneObstacle& Obstacle)
	{
		IDToLaneObstacleMap.Remove(Obstacle.GetID());
		
		TArray<FMassLaneObstacle>& Obstacles = LaneObstaclesMap.FindChecked(Obstacle.LaneSection.LaneHandle);
		Obstacles.RemoveSwap(Obstacle);

		return Obstacles.IsEmpty();
	}

	/** Find a FMassLaneObstacle from it's ID. */
	const FMassLaneObstacle* Find(const FMassLaneObstacleID& LaneObstacle)
	{
		return IDToLaneObstacleMap.Find(LaneObstacle);
	}

	/** Returns the array of lane obstacle from a FZoneGraphLaneHandle (this is expected to be the most common access pattern). */
	TArray<FMassLaneObstacle>* Find(const FZoneGraphLaneHandle LaneHandle)
	{
		return LaneObstaclesMap.Find(LaneHandle);
	}

	/** Reset data */
	void Reset()
	{
		IDToLaneObstacleMap.Reset();
		LaneObstaclesMap.Reset();
	}
	
	TMap<FMassLaneObstacleID, FMassLaneObstacle> IDToLaneObstacleMap; // @todo: investigate if keeping the LaneHandle with the ID in the fragment is better than having this second map.
	TMap<FZoneGraphLaneHandle, TArray<FMassLaneObstacle>> LaneObstaclesMap;
};

/** Container for movement lane data associated to a specific registered ZoneGraph data. */
struct FMassRegisteredMovementLaneData
{
	void Reset()
	{
		DataHandle.Reset();
		LaneObstacles.Reset();
	}

	/** Handle of the storage the data was initialized from. */
	FZoneGraphDataHandle DataHandle;

	/** 'Lane obstacle' data. */
	FMassLaneObstacleContainer LaneObstacles;
};

/**
 * Zone graph lane obstacle annotations.
 */
UCLASS(ClassGroup = AI, BlueprintType, meta = (BlueprintSpawnableComponent))
class MASSAIMOVEMENT_API UZoneGraphLaneObstacleAnnotations : public UZoneGraphAnnotationComponent
{
	GENERATED_BODY()

public:
	virtual void PostZoneGraphDataAdded(const AZoneGraphData& ZoneGraphData) override;
	virtual void PreZoneGraphDataRemoved(const AZoneGraphData& ZoneGraphData) override;
	
protected:
	virtual void PostSubsystemsInitialized() override;
	virtual FZoneGraphTagMask GetAnnotationTags() const override;
	virtual void HandleEvents(TConstArrayView<const UScriptStruct*> AllEventStructs, const FInstancedStructStream& Events) override;
	virtual void TickAnnotation(const float DeltaTime, FZoneGraphAnnotationTagContainer& BehaviorTagContainer) override;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	virtual void DebugDraw(FZoneGraphAnnotationSceneProxy* DebugProxy) override;
#endif
	
	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem;

	/** Annotation Tag to mark lanes that have obstacles. */
	UPROPERTY(EditAnywhere, Category = Tag)
	FZoneGraphTag LaneObstacleTag;

	/** Array of queued obstacle change events. */
	TArray<FZoneGraphLaneObstacleChangeEvent> LaneObstacleChangeEvents;

	/** Lane data for all registered ZoneGraph data, keeping track of 'lane obstacles' on lanes. */
	TArray<FMassRegisteredMovementLaneData> RegisteredLaneData;
};
