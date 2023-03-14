// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectAnnotation.h"
#include "GameplayTagContainer.h"
#include "CollisionShape.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "SmartObjectSlotEntranceAnnotation.generated.h"

class ANavigationData;
struct FNavLocation;

/** Struct defining a collider in world space. */
struct SMARTOBJECTSMODULE_API FSmartObjectAnnotationCollider
{
	FVector Location;
	FQuat Rotation;
	FCollisionShape CollisionShape;
};

/**
 * Annotation to define a entrance locations for a Smart Object Slot.
 * This can be used to add multiple entry points to a slot, or to validate the entries against navigation data. 
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotEntranceAnnotation : public FSmartObjectSlotAnnotation
{
	GENERATED_BODY()

	FSmartObjectSlotEntranceAnnotation()
		: bIsEntry(true)
		, bIsExit(true)
		, bTraceGroundLocation(false)
		, bCheckTransitionTrajectory(false)
	{
	}

#if WITH_EDITOR
	virtual void DrawVisualization(FSmartObjectVisualizationContext& VisContext) const override;
	virtual void DrawVisualizationHUD(FSmartObjectVisualizationContext& VisContext) const override;
	virtual void AdjustWorldTransform(const FTransform& SlotTransform, const FVector& DeltaTranslation, const FRotator& DeltaRotation) override;
#endif
	
	virtual TOptional<FTransform> GetWorldTransform(const FTransform& SlotTransform) const override;
	virtual FVector GetWorldLocation(const FTransform& SlotTransform) const;
	virtual FRotator GetWorldRotation(const FTransform& SlotTransform) const;

	/** @returns array of colliders that should free of collisions with world for this entrance to be used. */
	virtual void GetTrajectoryColliders(const FTransform& SlotTransform, TArray<FSmartObjectAnnotationCollider>& OutColliders) const;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void CollectDataForGameplayDebugger(FGameplayDebuggerCategory& Category, const FTransform& SlotTransform, const AActor* SmartObjectOwnerActor, const FVector ViewLocation, const FVector ViewDirection, const AActor* DebugActor) const override;
#endif // WITH_GAMEPLAY_DEBUGGER	

	/** Local space offset of the entry. */
	UPROPERTY(EditAnywhere, Category="Default")
	FVector3f Offset = FVector3f(0.f);

	/** Local space rotation of the entry. */
	UPROPERTY(EditAnywhere, Category="Default")
	FRotator3f Rotation = FRotator3f(0.f);

	/** Tag that can be used to identify the entry. */
	UPROPERTY(EditAnywhere, Category="Default")
	FGameplayTag Tag;

	/** Set to true if the entry can be used to enter the slot. */
	UPROPERTY(EditAnywhere, Category="Default")
	uint8 bIsEntry : 1;

	/** Set to true if the entry can be used to exit the slot. */
	UPROPERTY(EditAnywhere, Category="Default")
	uint8 bIsExit : 1;

	/** If set to true, ground location will be adjusted using a line trace. */
    UPROPERTY(EditAnywhere, Category="Default")
    uint8 bTraceGroundLocation : 1;

	/** If set to true, collisions will be checked between the transition from navigation location and slot location. */
	UPROPERTY(EditAnywhere, Category="Default")
	uint8 bCheckTransitionTrajectory : 1;
	
	/** Height offset at start of the transition collision check. */
	UPROPERTY(EditAnywhere, Category="Default", meta = (EditCondition = "bCheckTransitionTrajectory"))
	float TrajectoryStartHeightOffset = 80.0f;

	/** Height offset at slot location of the transition collision check. */
	UPROPERTY(EditAnywhere, Category="Default", meta = (EditCondition = "bCheckTransitionTrajectory"))
	float TrajectorySlotHeightOffset = 40.0f;

	/** Radius of the transition trajectory check. */
	UPROPERTY(EditAnywhere, Category="Default", meta = (EditCondition = "bCheckTransitionTrajectory"))
	float TransitionCheckRadius = 10.0f;
};


namespace UE::SmartObject::Annotations
{
	/**
	 * @returns navigation data for a specific actor. Similar to FNavigationSystem::GetNavDataForActor() but allows to pass custom world.
	 */
	extern SMARTOBJECTSMODULE_API const ANavigationData* GetNavDataForActor(const UWorld& World, const AActor* UserActor);

	/**
	 * Projects given location on nearest navigable surface.
	 * @param NavData navigation data to use for projection.
	 * @param Location location to project
	 * @param SearchBounds the bounds where the result must be contained
	 * @param NavigationFilter navigation filter to use to projection test
	 * @param InstigatorActor actor requesting the projection (optional)
	 * @param OutNavLocation resulting nav location
	 * @return true if a navigable location is found within SearchBounds.
	 */
	extern SMARTOBJECTSMODULE_API bool ProjectNavigationLocation(const ANavigationData& NavData, const FVector Location, const FBox& SearchBounds, const FSharedConstNavQueryFilter& NavigationFilter, const AActor* InstigatorActor, FNavLocation& OutNavLocation);

	/**
	 * Traces location down to find ground within SearchBounds.
	 * @param World world to use for the trace
	 * @param Location location to trace down
	 * @param SearchBounds the bounds where the result must be contained
	 * @param TraceParameters Smart Object trace parameters for the trace, defines if the trace is done by channel, profile or object type
	 * @param CollisionQueryParams Collision Query Params for the trace
	 * @param OutGroundLocation resulting ground location
	 * @return true if valid ground location found within SearchBounds.
	 */
	extern SMARTOBJECTSMODULE_API bool TraceGroundLocation(const UWorld& World, const FVector Location, const FBox& SearchBounds, const FSmartObjectTraceParams& TraceParameters, const FCollisionQueryParams& CollisionQueryParams, FVector& OutGroundLocation);

	/**
	 * Checks for overlap of given colliders.
	 * @param World world used for the overlap test
	 * @param Colliders array of colliders to test
	 * @param TraceParameters Smart Object trace parameters for the overlap test, defines if the trace is done by channel, profile or object type
	 * @param CollisionQueryParams Collision Query Params for the overlap test
	 * @return true if any of the colliders overlap.
	 */
	extern SMARTOBJECTSMODULE_API bool TestCollidersOverlap(const UWorld& World, TConstArrayView<FSmartObjectAnnotationCollider> Colliders, const FSmartObjectTraceParams& TraceParameters, const FCollisionQueryParams& CollisionQueryParams);

} // UE::SmartObject::Annotations