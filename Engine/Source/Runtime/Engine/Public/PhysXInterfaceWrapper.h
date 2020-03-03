// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXInterfaceWrapperCore.h"
#include "PhysXPublic.h"
#include "Physics/PhysicsInterfaceUtils.h"
#include "CustomPhysXPayload.h"
#include "PhysicsInterfaceWrapperShared.h"

namespace PhysXInterface
{
#if PHYSICS_INTERFACE_PHYSX
/**
* Helper to lock/unlock a scene that also makes sure to unlock everything when it goes out of scope.
* Multiple locks on the same scene are NOT SAFE. You can't call LockRead() if already locked.
* Multiple unlocks on the same scene are safe (repeated unlocks do nothing after the first successful unlock).
*/
struct FScopedSceneReadLock
{
	FScopedSceneReadLock(FPhysScene& Scene)
		: SceneLock(Scene.GetPxScene())
	{
		SCENE_LOCK_READ(SceneLock);
	}

	~FScopedSceneReadLock()
	{
		SCENE_UNLOCK_READ(SceneLock);
	}

	PxScene* SceneLock;
};
#endif

inline PxQueryFlags StaticDynamicQueryFlags(const FCollisionQueryParams& Params)
{
	switch (Params.MobilityType)
	{
	case EQueryMobilityType::Any: return  PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
	case EQueryMobilityType::Static: return  PxQueryFlag::eSTATIC;
	case EQueryMobilityType::Dynamic: return  PxQueryFlag::eDYNAMIC;
	default: check(0);
	}

	check(0);
	return PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
}

inline FQueryFilterData MakeQueryFilterData(const FCollisionFilterData& FilterData, EQueryFlags QueryFlags, const FCollisionQueryParams& Params)
{
	return PxQueryFilterData(U2PFilterData(FilterData), U2PQueryFlags(QueryFlags) | StaticDynamicQueryFlags(Params));
}

FORCEINLINE UPhysicalMaterial* GetUserData(const PxMaterial& Material)
{
	return FPhysxUserData::Get<UPhysicalMaterial>(Material.userData);
}

FORCEINLINE FBodyInstance* GetUserData(const PxActor& Actor)
{
	return FPhysxUserData::Get<FBodyInstance>(Actor.userData);
}

template <typename T>
T* GetUserData(const PxShape& Shape)
{
	return FPhysxUserData::Get<T>(Shape.userData);
}
}

#endif