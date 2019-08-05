// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "ChaosSQTypes.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Capsule.h"
#include "PhysicsInterfaceTypesCore.h"

#if WITH_PHYSX
#include "PhysXPublicCore.h"
#endif

namespace ChaosInterface
{
struct FDummyPhysType {};
struct FDummyPhysActor {};

template<typename DummyT>
struct FDummyCallback {};

#if WITH_PHYSX
using FQueryFilterData = PxQueryFilterData;
#else
using FQueryFilterData = FDummyPhysType;
#endif

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsRaycastInputAdapater
{
	FPhysicsRaycastInputAdapater(const FVector& InStart, const FVector& InDir, const EHitFlags InFlags)
		: Start(InStart)
		, Dir(InDir)
		, OutputFlags(InFlags)
	{

	}
	FVector Start;
	FVector Dir;
	EHitFlags OutputFlags;
};

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsSweepInputAdapater
{
	FPhysicsSweepInputAdapater(const FTransform& InStartTM, const FVector& InDir, const EHitFlags InFlags)
		: StartTM(InStartTM)
		, Dir(InDir)
		, OutputFlags(InFlags)
	{

	}
	FTransform StartTM;
	FVector Dir;
	EHitFlags OutputFlags;
};

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsOverlapInputAdapater
{
	FPhysicsOverlapInputAdapater(const FTransform& InPose)
		: GeomPose(InPose)
	{

	}
	FTransform GeomPose;
};

inline ECollisionShapeType GetImplicitType(const Chaos::TImplicitObject<float, 3>& InGeometry)
{
	switch (InGeometry.GetType())
	{
	case Chaos::ImplicitObjectType::Sphere: return ECollisionShapeType::Sphere;
	case Chaos::ImplicitObjectType::Box: return ECollisionShapeType::Box;
	case Chaos::ImplicitObjectType::Capsule: return ECollisionShapeType::Capsule;
	case Chaos::ImplicitObjectType::Convex: return ECollisionShapeType::Convex;
	case Chaos::ImplicitObjectType::TriangleMesh: return ECollisionShapeType::Trimesh;
	case Chaos::ImplicitObjectType::HeightField: return ECollisionShapeType::Heightfield;
	default: break;
	}

	return ECollisionShapeType::None;
}

inline ECollisionShapeType GetType(const Chaos::TImplicitObject<float, 3>& InGeometry)
{
	return GetImplicitType(InGeometry);
}

inline float GetRadius(const Chaos::TCapsule<float>& InCapsule)
{
	return InCapsule.GetRadius();
}

inline float GetHalfHeight(const Chaos::TCapsule<float>& InCapsule)
{
	return InCapsule.GetHeight()/2.;
}

inline bool HadInitialOverlap(const FLocationHit& Hit)
{
	return Hit.Distance <= 0.f;
}

inline const Chaos::TPerShapeData<float, 3>* GetShape(const FActorShape& Hit)
{
	return Hit.Shape;
}

inline Chaos::TGeometryParticle<float,3>* GetActor(const FActorShape& Hit)
{
	return Hit.Actor;
}

inline float GetDistance(const FLocationHit& Hit)
{
	return Hit.Distance;
}

inline FVector GetPosition(const FLocationHit& Hit)
{
	return Hit.WorldPosition;
}

inline FVector GetNormal(const FLocationHit& Hit)
{
	return Hit.WorldNormal;
}

inline FDummyPhysType* GetMaterialFromInternalFaceIndex(const FDummyPhysType& Shape, uint32 InternalFaceIndex)
{
	return nullptr;
}

inline FHitFlags GetFlags(const FLocationHit& Hit)
{
	return Hit.Flags;
}

FORCEINLINE void SetFlags(FLocationHit& Hit, FHitFlags Flags)
{
	Hit.Flags = Flags;
}

inline uint32 GetInternalFaceIndex(const FQueryHit& Hit)
{
	return Hit.FaceIndex;
}

inline void SetInternalFaceIndex(FQueryHit& Hit, uint32 FaceIndex)
{
	Hit.FaceIndex = FaceIndex;
}

inline FCollisionFilterData GetQueryFilterData(const Chaos::TPerShapeData<float, 3>& Shape)
{
	return Shape.QueryData;
}

inline FCollisionFilterData GetSimulationFilterData(const Chaos::TPerShapeData<float, 3>& Shape)
{
	return Shape.QueryData;
}

inline uint32 GetInvalidPhysicsFaceIndex()
{
	return 0xffffffff;
}

inline uint32 GetTriangleMeshExternalFaceIndex(const FDummyPhysType& Shape, uint32 InternalFaceIndex)
{
	return GetInvalidPhysicsFaceIndex();
}

inline FTransform GetGlobalPose(const FDummyPhysActor& RigidActor)
{
	return FTransform::Identity;
}

inline uint32 GetNumShapes(const FDummyPhysActor& RigidActor)
{
	return 0;
}

inline void GetShapes(const FDummyPhysActor& RigidActor, Chaos::TImplicitObject<float, 3>** ShapesBuffer, uint32 NumShapes)
{

}

inline void SetActor(FDummyPhysType& Hit, FDummyPhysActor* Actor)
{

}

inline void SetShape(FDummyPhysType& Hit, Chaos::TImplicitObject<float, 3>* Shape)
{

}

template <typename HitType>
HitType* GetBlock(FSQHitBuffer<HitType>& Callback)
{
	return Callback.GetBlock();
}

template <typename HitType>
bool GetHasBlock(const FSQHitBuffer<HitType>& Callback)
{
	return Callback.HasBlockingHit();
}

} // namespace ChaosInterface

#endif // WITH_CHAOS

#if WITH_CHAOS && (!defined(PHYSICS_INTERFACE_PHYSX) || !PHYSICS_INTERFACE_PHYSX)
using namespace ChaosInterface;
#endif
