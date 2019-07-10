// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once
#include "CollisionQueryFilterCallbackCore.h"
#include "CollisionQueryParams.h"
#include "Physics/PhysicsInterfaceTypes.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXInterfaceWrapper.h"
#endif

#define ENABLE_PREFILTER_LOGGING 0

struct FBodyInstance;

/** TArray typedef of components to ignore. */
typedef FCollisionQueryParams::IgnoreComponentsArrayType FilterIgnoreComponentsArrayType;

/** TArray typedef of actors to ignore. */
typedef FCollisionQueryParams::IgnoreActorsArrayType FilterIgnoreActorsArrayType;

class FCollisionQueryFilterCallback : public ICollisionQueryFilterCallbackBase
{
public:
	/** Result of PreFilter callback. */
	ECollisionQueryHitType PreFilterReturnValue;

	/** List of ComponentIds for this query to ignore */
	const FilterIgnoreComponentsArrayType& IgnoreComponents;

	/** List of ActorIds for this query to ignore */
	const FilterIgnoreActorsArrayType& IgnoreActors;

	/** Whether we are doing an overlap query. This is needed to ensure physx results are never blocking (even if they are in terms of unreal)*/
	bool bIsOverlapQuery;

	/** Whether to ignore touches (convert an eTOUCH result to eNONE). */
	bool bIgnoreTouches;

	/** Whether to ignore blocks (convert an eBLOCK result to eNONE). */
	bool bIgnoreBlocks;

	FCollisionQueryFilterCallback(const FCollisionQueryParams& InQueryParams, bool bInIsSweep)
		: IgnoreComponents(InQueryParams.GetIgnoredComponents())
		, IgnoreActors(InQueryParams.GetIgnoredActors())
#if DETECT_SQ_HITCHES
		, bRecordHitches(false)
#endif
		, bIsSweep(bInIsSweep)
	{
		PreFilterReturnValue = ECollisionQueryHitType::None;
		bIsOverlapQuery = false;
		bIgnoreTouches = InQueryParams.bIgnoreTouches;
		bIgnoreBlocks = InQueryParams.bIgnoreBlocks;
		bDiscardInitialOverlaps = !InQueryParams.bFindInitialOverlaps;
	}
	~FCollisionQueryFilterCallback() {}


	static ECollisionQueryHitType CalcQueryHitType(const FCollisionFilterData& QueryFilter, const FCollisionFilterData& ShapeFilter, bool bPreFilter = false);

	ECollisionQueryHitType PreFilterImp(const FCollisionFilterData& FilterData, const FCollisionFilterData& ShapeFilterData, uint32 ComponentID, const FBodyInstance* BodyInstance);

	ECollisionQueryHitType PostFilterImp(const FCollisionFilterData& FilterData, bool bIsOverlap);

	ECollisionQueryHitType PostFilterImp(const FCollisionFilterData& FilterData, const FPhysicsQueryHit& Hit);
	ECollisionQueryHitType PreFilterImp(const FCollisionFilterData& FilterData, const FPhysicsShape& Shape, const FPhysicsActor& Actor);

#if INCLUDE_CHAOS
	virtual ECollisionQueryHitType PreFilterChaos(const FCollisionFilterData& FilterData, const Chaos::TImplicitObject<float, 3>& Shape, int32 ActorIdx, int32 ShapeIdx) { check(false);  return ECollisionQueryHitType::Block; }
#endif

	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const FPhysicsQueryHit& Hit) override
	{
		return PostFilterImp(FilterData, Hit);
	}
	
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const FPhysicsShape& Shape, const FPhysicsActor& Actor) override
	{
		return PreFilterImp(FilterData, Shape, Actor);
	}

#if WITH_PHYSX
	virtual PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override;
	virtual PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override;
#endif

#if DETECT_SQ_HITCHES
	// Util struct to record what preFilter was called with
	struct FPreFilterRecord
	{
		FString OwnerComponentReadableName;
		ECollisionQueryHitType Result;
	};

	TArray<FPreFilterRecord> PreFilterHitchInfo;
	bool bRecordHitches;
#endif
	bool bDiscardInitialOverlaps;
	bool bIsSweep;
};

