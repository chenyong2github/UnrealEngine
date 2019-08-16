// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "PhysicsInterfaceTypesCore.h"
#if WITH_PHYSX
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

#if INCLUDE_CHAOS
namespace Chaos
{
	template <typename, int>
	class TImplicitObject;
}
#endif

class ICollisionQueryFilterCallbackBase
#if WITH_PHYSX
	: public physx::PxQueryFilterCallback	//this is a bit of a hack until physx support is gone. Means any new non physx callback will have dummy pre/postFilter functions for physx api
#endif
{
public:
	
	virtual ~ICollisionQueryFilterCallbackBase() {}
#if INCLUDE_CHAOS
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) = 0;
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::TPerShapeData<float, 3>& Shape, const Chaos::TGeometryParticle<float, 3>& Actor) = 0;
#endif

#if WITH_PHYSX
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const physx::PxQueryHit& Hit) = 0;
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const physx::PxShape& Shape, physx::PxRigidActor& Actor) = 0;
#endif
};

class PHYSICSCORE_API FBlockAllQueryCallback : public ICollisionQueryFilterCallbackBase
{
public:
	virtual ~FBlockAllQueryCallback() {}
#if INCLUDE_CHAOS
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) override { return ECollisionQueryHitType::Block; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::TPerShapeData<float, 3>& Shape, const Chaos::TGeometryParticle<float, 3>& Actor) override { return ECollisionQueryHitType::Block; }
#endif

#if WITH_PHYSX
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
#if INCLUDE_CHAOS
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) override { return ECollisionQueryHitType::Touch; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::TPerShapeData<float, 3>& Shape, const Chaos::TGeometryParticle<float, 3>& Actor) override { return ECollisionQueryHitType::Touch; }
#endif

#if WITH_PHYSX
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const physx::PxQueryHit& Hit) override { return ECollisionQueryHitType::Touch; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const physx::PxShape& Shape, physx::PxRigidActor& Actor) override { return ECollisionQueryHitType::Touch; }
	virtual PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override { return PxQueryHitType::eTOUCH; }
	virtual PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override { return PxQueryHitType::eTOUCH; }
#endif
};

#if WITH_PHYSX
inline PxQueryHitType::Enum U2PCollisionQueryHitType(ECollisionQueryHitType HitType)
{
	return (PxQueryHitType::Enum)HitType;
}

inline ECollisionQueryHitType P2UCollisionQueryHitType(PxQueryHitType::Enum HitType)
{
	return (ECollisionQueryHitType)HitType;
}
#endif