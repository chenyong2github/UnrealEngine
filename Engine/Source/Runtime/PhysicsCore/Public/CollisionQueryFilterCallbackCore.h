// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "PhysicsInterfaceTypesCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublicCore.h"
#include "PhysXInterfaceWrapperCore.h"
#endif
#if WITH_CHAOS
#include "ChaosInterfaceWrapperCore.h"
#endif


/**
 *
 * Make sure this matches PxQueryHitType for HitTypeToPxQueryHitType to work
 */
enum class ECollisionQueryHitType : uint8
{
	None = 0,
	Touch = 1,
	Block = 2
};

namespace Chaos
{
	class FImplicitObject;
}

class ICollisionQueryFilterCallbackBase
#if PHYSICS_INTERFACE_PHYSX
	: public PxQueryFilterCallback	//this is a bit of a hack until physx support is gone. Means any new non physx callback will have dummy pre/postFilter functions for physx api
#endif
{
public:
	
	virtual ~ICollisionQueryFilterCallbackBase() {}
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) = 0;
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::FPerShapeData& Shape, const Chaos::FGeometryParticle& Actor) = 0;

#if  PHYSICS_INTERFACE_PHYSX
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const physx::PxQueryHit& Hit) = 0;
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const physx::PxShape& Shape, physx::PxRigidActor& Actor) = 0;
#endif
};

class PHYSICSCORE_API FBlockAllQueryCallback : public ICollisionQueryFilterCallbackBase
{
public:
	virtual ~FBlockAllQueryCallback() {}
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) override { return ECollisionQueryHitType::Block; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::FPerShapeData& Shape, const Chaos::FGeometryParticle& Actor) override { return ECollisionQueryHitType::Block; }

#if PHYSICS_INTERFACE_PHYSX
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const physx::PxQueryHit& Hit) override { return ECollisionQueryHitType::Block; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const physx::PxShape& Shape, physx::PxRigidActor& Actor) override { return ECollisionQueryHitType::Block; }
	virtual PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override { return PxQueryHitType::eBLOCK; }
	virtual PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override { return PxQueryHitType::eBLOCK; }
#endif
};

class PHYSICSCORE_API FOverlapAllQueryCallback : public ICollisionQueryFilterCallbackBase
{
public:
	virtual ~FOverlapAllQueryCallback() {}
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) override { return ECollisionQueryHitType::Touch; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::FPerShapeData& Shape, const Chaos::FGeometryParticle& Actor) override { return ECollisionQueryHitType::Touch; }

#if PHYSICS_INTERFACE_PHYSX
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const physx::PxQueryHit& Hit) override { return ECollisionQueryHitType::Touch; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const physx::PxShape& Shape, physx::PxRigidActor& Actor) override { return ECollisionQueryHitType::Touch; }
	virtual PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override { return PxQueryHitType::eTOUCH; }
	virtual PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override { return PxQueryHitType::eTOUCH; }
#endif
};

#if PHYSICS_INTERFACE_PHYSX
inline PxQueryHitType::Enum U2PCollisionQueryHitType(ECollisionQueryHitType HitType)
{
	return (PxQueryHitType::Enum)HitType;
}

inline ECollisionQueryHitType P2UCollisionQueryHitType(PxQueryHitType::Enum HitType)
{
	return (ECollisionQueryHitType)HitType;
}
#endif