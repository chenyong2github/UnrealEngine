// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/BoxSphereBounds.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXInterfaceWrapperCore.h"	//todo remove physx specific include once refactor is done
#elif WITH_CHAOS
#include "ChaosInterfaceWrapperCore.h"
#endif

#include "ChaosSQTypes.h"

#if WITH_PHYSX
namespace physx
{
	class PxScene;
}
#endif

namespace Chaos
{
	class FImplicitObject;

	template <typename T, int d>
	class TAccelerationStructureHandle;

	template <typename TPayload, typename T, int d>
	class ISpatialAcceleration;
}

class FSQAccelerator;
struct FCollisionFilterData;
struct FCollisionQueryParams;
struct FCollisionQueryParams;
class ICollisionQueryFilterCallbackBase;

class PHYSICSSQ_API FChaosSQAccelerator
{
public:

	FChaosSQAccelerator(const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>,float, 3>& InSpatialAcceleration);
	virtual ~FChaosSQAccelerator() {};

	void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams = FQueryDebugParams()) const;
	void Sweep(const Chaos::FImplicitObject& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams = FQueryDebugParams()) const;
	void Overlap(const Chaos::FImplicitObject& QueryGeom, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams = FQueryDebugParams()) const;

private:
	const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>& SpatialAcceleration;

};

// An interface to the scene query accelerator that allows us to run queries against either PhysX or Chaos
// when compiling WITH_PHYSX.
// This was used in the 2019 GDC demos and is now broken. To make it work again, we would need to implement
// the FChaosSQAcceleratorAdapter below to use its internal SQ accelerator and convert the inputs and outputs
// from/to PhysX types.
class PHYSICSSQ_API ISQAccelerator
{
public:
	virtual ~ISQAccelerator() {};
	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const = 0;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const = 0;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const = 0;
};

class PHYSICSSQ_API FSQAcceleratorUnion : public ISQAccelerator
{
public:

	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;

	void AddSQAccelerator(ISQAccelerator* InAccelerator);
	void RemoveSQAccelerator(ISQAccelerator* AcceleratorToRemove);

private:
	TArray<ISQAccelerator*> Accelerators;
};

#if WITH_PHYSX
// A Chaos Query Accelerator with a PhysX API
// TODO: Not implemented - required to make GDC 2019 demos work again.
class PHYSICSSQ_API FChaosSQAcceleratorAdapter : public ISQAccelerator
{
public:

	FChaosSQAcceleratorAdapter(const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>& InSpatialAcceleration);
	virtual ~FChaosSQAcceleratorAdapter() {};

	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;

private:
	FChaosSQAccelerator ChaosSQAccelerator;
};
#endif


#if WITH_PHYSX && !WITH_CHAOS
class PHYSICSSQ_API FPhysXSQAccelerator : public ISQAccelerator
{
public:

	FPhysXSQAccelerator();
	FPhysXSQAccelerator(physx::PxScene* InScene);
	virtual ~FPhysXSQAccelerator() {};

	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;

	void SetScene(physx::PxScene* InScene);

private:
	physx::PxScene* Scene;

};
#endif
