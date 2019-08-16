// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if INCLUDE_CHAOS
#include "ChaosInterfaceWrapperCore.h"
#include "CollisionQueryFilterCallbackCore.h"
#include "Chaos/ParticleHandle.h"
#include "PhysxUserData.h"
//todo: move this include into an impl header

class UPhysicalMaterial;
struct FBodyInstance;
struct FCollisionFilterData;
struct FCollisionQueryParams;

namespace ChaosInterface
{

// Needed by low level SQ calls. Right now there's no specific locking for LLI
// #PHYS2 update as locking becomes necessary
struct FScopedSceneReadLock
{
	FScopedSceneReadLock(FPhysScene& Scene)
	{}
};

inline FQueryFilterData MakeQueryFilterData(const FCollisionFilterData& FilterData, EQueryFlags QueryFlags, const FCollisionQueryParams& Params)
{
#if WITH_PHYSX
	return PxQueryFilterData(U2PFilterData(FilterData), U2PQueryFlags(QueryFlags));
#else
	return FQueryFilterData();
#endif
}

inline UPhysicalMaterial* GetUserData(const FPhysTypeDummy& Material)
{
	return nullptr;
}

inline FBodyInstance* GetUserData(const Chaos::TGeometryParticle<float,3>& Actor)
{
	void* UserData = Actor.UserData();
	return UserData ? FPhysxUserData::Get<FBodyInstance>(Actor.UserData()) : nullptr;
}

}
void LowLevelRaycast(FPhysScene& Scene, const FVector& Start, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback);
void LowLevelSweep(FPhysScene& Scene, const FPhysicsGeometry& Geom, const FTransform& StartTM, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback);
void LowLevelOverlap(FPhysScene& Scene, const FPhysicsGeometry& Geom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback);

#endif // INCLUDE_CHAOS