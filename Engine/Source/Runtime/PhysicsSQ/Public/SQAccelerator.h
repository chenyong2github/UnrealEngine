// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/BoxSphereBounds.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXInterfaceWrapperCore.h"	//todo remove physx specific include once refactor is done
#elif PHYSICS_INTERFACE_LLIMMEDIATE
//#include "Physics/Experimental/LLImmediateInterfaceWrapper.h"
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
	template <typename, int>
	class TPBDRigidsEvolutionGBF;
}

class FSQAccelerator;
struct FCollisionFilterData;
struct FCollisionQueryParams;
struct FCollisionQueryParams;

class FSQAcceleratorEntry

{
public:
	bool Intersect(const FBoxSphereBounds& Other) const
	{
		//return FBoxSphereBounds::BoxesIntersect(Other, Bounds);
		return true;
	}

	void* GetPayload() const
	{
		return Payload;
	}
private:
	FSQAcceleratorEntry(void* InPayload)
		: Payload(InPayload) {}

	void* Payload;
	friend FSQAccelerator;
};

struct FSQNode
{
	TArray<FSQAcceleratorEntry*> Entries;
};

class ICollisionQueryFilterCallbackBase;

class PHYSICSSQ_API ISQAccelerator
{
public:
	virtual ~ISQAccelerator() {};
	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const = 0;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& Params, ICollisionQueryFilterCallbackBase& QueryCallback) const = 0;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const = 0;
};

class PHYSICSSQ_API FSQAccelerator : public ISQAccelerator
{
public:
	FSQAcceleratorEntry* AddEntry(void* Payload);
	void RemoveEntry(FSQAcceleratorEntry* Entry);
	void GetNodes(TArray<const FSQNode*>& NodesFound) const;

	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;

	virtual ~FSQAccelerator() override;
private:
	TArray<FSQNode*> Nodes;
};

class PHYSICSSQ_API FSQAcceleratorUnion : public ISQAccelerator
{
public:

	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;

	void AddSQAccelerator(ISQAccelerator* InAccelerator);
	void RemoveSQAccelerator(ISQAccelerator* AcceleratorToRemove);

private:
	TArray<ISQAccelerator*> Accelerators;
};

#if INCLUDE_CHAOS
class PHYSICSSQ_API FChaosSQAccelerator : public ISQAccelerator
{
public:

	FChaosSQAccelerator(const Chaos::TPBDRigidsEvolutionGBF<float, 3>& InEvolution);
	virtual ~FChaosSQAccelerator() {};
	virtual void ChaosSweep(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const;
	virtual void ChaosRaycast(const FVector& StartPoint, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const;
	virtual void ChaosOverlap(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& WorldTM,FPhysicsHitCallback<FOverlapHit>& HitBuffer, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const;

	virtual void Raycast(const FVector& Start, const FVector& Dir,const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override
	{
		check(false);
	}

	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override
	{
		check(false);
	}

	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override
	{
		check(false);
	}

	void SetScene(FPhysScene* InScene);

private:
	const Chaos::TPBDRigidsEvolutionGBF<float, 3>& Evolution;

};
#endif

#if WITH_PHYSX && !WITH_CHAOS
class PHYSICSSQ_API FPhysXSQAccelerator : public ISQAccelerator
{
public:

	FPhysXSQAccelerator();
	FPhysXSQAccelerator(physx::PxScene* InScene);
	virtual ~FPhysXSQAccelerator() {};

	virtual void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;
	virtual void Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& QueryFilter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const override;

	void SetScene(physx::PxScene* InScene);

private:
	physx::PxScene* Scene;

};
#endif
