// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_PHYSX
#include "PhysXPublicCore.h"
#include "PhysicsInterfaceUtilsCore.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "ChaosSQTypes.h"

inline EQueryFlags P2UQueryFlags(PxQueryFlags Flags)
{
	FQueryFlags Result = EQueryFlags::None;
	if (Flags & PxQueryFlag::ePREFILTER)
	{
		Result |= EQueryFlags::PreFilter;
	}

	if (Flags & PxQueryFlag::ePOSTFILTER)
	{
		Result |= EQueryFlags::PostFilter;
	}

	if (Flags & PxQueryFlag::eANY_HIT)
	{
		Result |= EQueryFlags::AnyHit;
	}

	return Result.QueryFlags;
}

inline PxQueryFlags U2PQueryFlags(FQueryFlags Flags)
{
	uint32 Result = 0;
	if (Flags & EQueryFlags::PreFilter)
	{
		Result |= PxQueryFlag::ePREFILTER;
	}

	if (Flags & EQueryFlags::PostFilter)
	{
		Result |= PxQueryFlag::ePOSTFILTER;
	}

	if (Flags & EQueryFlags::AnyHit)
	{
		Result |= PxQueryFlag::eANY_HIT;
	}

	return (PxQueryFlags)Result;
}

FORCEINLINE PxHitFlags U2PHitFlags(const FHitFlags& Flags)
{
	uint32 Result = 0;
	if (Flags & EHitFlags::Position)
	{
		Result |= PxHitFlag::ePOSITION;
	}

	if (Flags & EHitFlags::Normal)
	{
		Result |= PxHitFlag::eNORMAL;
	}

	if (Flags & EHitFlags::Distance)
	{
		Result |= PxHitFlag::eDISTANCE;
	}

	if (Flags & EHitFlags::UV)
	{
		Result |= PxHitFlag::eUV;
	}

	if (Flags & EHitFlags::MTD)
	{
		Result |= PxHitFlag::eMTD;
	}

	if (Flags & EHitFlags::FaceIndex)
	{
		Result |= PxHitFlag::eFACE_INDEX;
	}

	return (PxHitFlags)Result;
}

FORCEINLINE EHitFlags P2UHitFlags(const PxHitFlags& Flags)
{
	FHitFlags Result = EHitFlags::None;
	if (Flags & PxHitFlag::ePOSITION)
	{
		Result |= EHitFlags::Position;
	}

	if (Flags & PxHitFlag::eDISTANCE)
	{
		Result |= EHitFlags::Distance;
	}

	if (Flags & PxHitFlag::eNORMAL)
	{
		Result |= EHitFlags::Normal;
	}

	if (Flags & PxHitFlag::eUV)
	{
		Result |= EHitFlags::UV;
	}

	if (Flags & PxHitFlag::eMTD)
	{
		Result |= EHitFlags::MTD;
	}

	if (Flags & PxHitFlag::eFACE_INDEX)
	{
		Result |= EHitFlags::FaceIndex;
	}

	return Result.HitFlags;
}

namespace PhysXInterface
{

FORCEINLINE bool HadInitialOverlap(const PxLocationHit& Hit)
{
	return Hit.hadInitialOverlap();
}

FORCEINLINE PxShape* GetShape(const PxLocationHit& Hit)
{
	return Hit.shape;
}

FORCEINLINE PxShape* GetShape(const PxOverlapHit& Hit)
{
	return Hit.shape;
}

FORCEINLINE PxRigidActor* GetActor(const PxLocationHit& Hit)
{
	return Hit.actor;
}

FORCEINLINE PxRigidActor* GetActor(const PxOverlapHit& Hit)
{
	return Hit.actor;
}

FORCEINLINE float GetDistance(const PxLocationHit& Hit)
{
	return Hit.distance;
}

template <typename HitType>
HitType* GetBlock(PxHitCallback<HitType>& Callback)
{
	return &Callback.block;
}

template <typename HitType>
bool GetHasBlock(const PxHitCallback<HitType>& Callback)
{
	return Callback.hasBlock;
}

FORCEINLINE FVector GetPosition(const PxLocationHit& Hit)
{
	return P2UVector(Hit.position);
}

FORCEINLINE FVector GetNormal(const PxLocationHit& Hit)
{
	return P2UVector(Hit.normal);
}

FORCEINLINE FHitFlags GetFlags(const PxLocationHit& Hit)
{
	return P2UHitFlags(Hit.flags);
}

FORCEINLINE void SetFlags(PxLocationHit& Hit, FHitFlags Flags)
{
	Hit.flags = U2PHitFlags(Flags);
}

FORCEINLINE uint32 GetInternalFaceIndex(const PxLocationHit& Hit)
{
	return Hit.faceIndex;
}

FORCEINLINE void SetInternalFaceIndex(PxLocationHit& Hit, uint32 FaceIndex)
{
	Hit.faceIndex = FaceIndex;
}

FORCEINLINE FCollisionFilterData GetQueryFilterData(const PxShape& Shape)
{
	return P2UFilterData(Shape.getQueryFilterData());
}

FORCEINLINE FCollisionFilterData GetSimulationFilterData(const PxShape& Shape)
{
	return P2UFilterData(Shape.getSimulationFilterData());
}

FORCEINLINE ECollisionShapeType GetType(const PxGeometry& Geom)
{
	return P2UGeometryType(Geom.getType());
}

FORCEINLINE ECollisionShapeType GetGeometryType(const PxShape& Shape)
{
	return P2UGeometryType(Shape.getGeometryType());
}

FORCEINLINE PxMaterial* GetMaterialFromInternalFaceIndex(const PxShape& Shape, uint32 InternalFaceIndex)
{
	return Shape.getMaterialFromInternalFaceIndex(InternalFaceIndex);
}

FORCEINLINE uint32 GetInvalidPhysicsFaceIndex()
{
	// Sentinel for invalid query results.
	static const PxQueryHit InvalidQueryHit;
	return InvalidQueryHit.faceIndex;
}

FORCEINLINE_DEBUGGABLE bool IsInvalidFaceIndex(PxU32 faceIndex)
{
	checkfSlow(GetInvalidPhysicsFaceIndex() == 0xFFFFffff, TEXT("Engine code needs fixing: PhysX invalid face index sentinel has changed or is not part of default PxQueryHit!"));
	return (faceIndex == 0xFFFFffff);
}

using FQueryFilterData = PxQueryFilterData;

template <typename HitType>
class FSingleHitBuffer : public PxHitBuffer<HitType>
{
public:
	FSingleHitBuffer()
		: PxHitBuffer<HitType>()
	{

	}

};

using FPhysicsSweepBuffer = FSingleHitBuffer<PxSweepHit>;
using FPhysicsRaycastBuffer = FSingleHitBuffer<PxRaycastHit>;

#define HIT_BUFFER_SIZE							512		// Hit buffer size for traces and sweeps. This is the total size allowed for sync + async tests.
static_assert(HIT_BUFFER_SIZE > 0, "Invalid PhysX hit buffer size.");

//todo(ocohen): remove all of this - it's not clear that we care about dynamic dispatch at this layer
template<typename HitType>
class FDynamicHitBuffer : public PxHitCallback<HitType>
{
private:
	/** Hit buffer used to provide hits via processTouches */
	TTypeCompatibleBytes<HitType> HitBuffer[HIT_BUFFER_SIZE];

	/** Hits encountered. Can be larger than HIT_BUFFER_SIZE */
	TArray<TTypeCompatibleBytes<HitType>, TInlineAllocator<HIT_BUFFER_SIZE>> Hits;

public:
	FDynamicHitBuffer()
		: PxHitCallback<HitType>((HitType*)HitBuffer, HIT_BUFFER_SIZE)
	{}

	virtual PxAgain processTouches(const HitType* buffer, PxU32 nbHits) override
	{
		Hits.Append((TTypeCompatibleBytes<HitType>*)buffer, nbHits);
		return true;
	}

	virtual void finalizeQuery() override
	{
		if (this->hasBlock)
		{
			// copy blocking hit to hits
			processTouches(&this->block, 1);
		}
	}

	FORCEINLINE int32 GetNumHits() const
	{
		return Hits.Num();
	}

	FORCEINLINE HitType* GetHits()
	{
		return (HitType*)Hits.GetData();
	}

	FORCEINLINE const HitType* GetHits() const
	{
		return (const HitType*)Hits.GetData();
	}
};


/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsRaycastInputAdapater
{
	FPhysicsRaycastInputAdapater(const FVector& InStart, const FVector& InDir, const EHitFlags InFlags)
		: Start(U2PVector(InStart))
		, Dir(U2PVector(InDir))
		, OutputFlags(U2PHitFlags(InFlags))
	{

	}
	PxVec3 Start;
	PxVec3 Dir;
	PxHitFlags OutputFlags;
};

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsSweepInputAdapater
{
	FPhysicsSweepInputAdapater(const FTransform& InStartTM, const FVector& InDir, const EHitFlags InFlags)
		: StartTM(U2PTransform(InStartTM))
		, Dir(U2PVector(InDir))
		, OutputFlags(U2PHitFlags(InFlags))
	{

	}
	PxTransform StartTM;
	PxVec3 Dir;
	PxHitFlags OutputFlags;
};

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsOverlapInputAdapater
{
	FPhysicsOverlapInputAdapater(const FTransform& InPose)
		: GeomPose(U2PTransform(InPose))
	{

	}
	PxTransform GeomPose;
};

FORCEINLINE uint32 GetTriangleMeshExternalFaceIndex(const PxShape& Shape, uint32 InternalFaceIndex)
{
	PxTriangleMeshGeometry TriangleMeshGeometry;
	if (Shape.getTriangleMeshGeometry(TriangleMeshGeometry))
	{
		if (TriangleMeshGeometry.triangleMesh && InternalFaceIndex < TriangleMeshGeometry.triangleMesh->getNbTriangles())
		{
			if (const PxU32* TriangleRemap = TriangleMeshGeometry.triangleMesh->getTrianglesRemap())
			{
				return TriangleRemap[InternalFaceIndex];
			}
		}
	}

	return PhysXInterface::GetInvalidPhysicsFaceIndex();
}

FORCEINLINE float GetRadius(const PxCapsuleGeometry& Capsule)
{
	return Capsule.radius;
}

FORCEINLINE float GetHalfHeight(const PxCapsuleGeometry& Capsule)
{
	return Capsule.halfHeight;
}

FORCEINLINE PxTransform GetGlobalPose(const PxRigidActor& RigidActor)
{
	return RigidActor.getGlobalPose();
}

FORCEINLINE uint32 GetNumShapes(const PxRigidActor& RigidActor)
{
	return RigidActor.getNbShapes();
}

FORCEINLINE void GetShapes(const PxRigidActor& RigidActor, PxShape** ShapesBuffer, uint32 NumShapes)
{
	RigidActor.getShapes(ShapesBuffer, sizeof(ShapesBuffer[0]) * NumShapes);
}

FORCEINLINE void SetActor(PxActorShape& Hit, PxRigidActor* Actor)
{
	Hit.actor = Actor;
}

FORCEINLINE void SetShape(PxActorShape& Hit, PxShape* Shape)
{
	Hit.shape = Shape;
}

}

#if PHYSICS_INTERFACE_PHYSX
using namespace PhysXInterface;
#endif

#endif