// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosInterfaceWrapperCore.h"
#include "CollisionQueryFilterCallbackCore.h"

class UPhysicalMaterial;
struct FBodyInstance;

// Needed by low level SQ calls. Right now there's no specific locking for LLI
// #PHYS2 update as locking becomes necessary
struct FScopedSceneReadLock
{
	FScopedSceneReadLock(FPhysScene& Scene)
	{}
};

inline FQueryFilterData MakeQueryFilterData(const FCollisionFilterData& FilterData, EQueryFlags QueryFlags, const FCollisionQueryParams& Params)
{
	return FQueryFilterData();
}

inline UPhysicalMaterial* GetUserData(const FPhysTypeDummy& Material)
{
	return nullptr;
}

inline FBodyInstance* GetUserData(const FPhysActorDummy& Actor)
{
	return nullptr;
}


inline void LowLevelRaycast(FPhysScene& Scene, const FVector& Start, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback) {}
inline void LowLevelSweep(FPhysScene& Scene, const FPhysicsGeometry& Geom, const FTransform& StartTM, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback) {}
inline void LowLevelOverlap(FPhysScene& Scene, const FPhysicsGeometry& Geom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback) {}