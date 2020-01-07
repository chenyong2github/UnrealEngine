// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsMaterial_PhysX.h"

namespace ImmediatePhysics_PhysX
{
	struct FMaterialHandle;
}

namespace ImmediatePhysics_PhysX
{
	/** Holds shape data*/
	struct FShape
	{
		PxTransform LocalTM;
		FMaterial* Material;
		FMaterial InternalMaterial;
		PxGeometry* Geometry;
		PxVec3 BoundsOffset;
		float BoundsMagnitude;
		void* UserData;

		FShape()
			: LocalTM(PxTransform(PxIDENTITY::PxIdentity))
			, Material(nullptr)
			, Geometry(nullptr)
			, BoundsOffset(PxVec3(PxIDENTITY::PxIdentity))
			, BoundsMagnitude(0.0f)
			, UserData(nullptr)
		{}

		FShape(const PxTransform& InLocalTM, const PxVec3& InBoundsOffset, const float InBoundsMagnitude, PxGeometry* InGeometry, FMaterial* InMaterial = nullptr)
			: LocalTM(InLocalTM)
			, Material(InMaterial)
			, Geometry(InGeometry)
			, BoundsOffset(InBoundsOffset)
			, BoundsMagnitude(InBoundsMagnitude)
			, UserData(nullptr)
		{
		}

		FShape(const PxTransform& InLocalTM, const PxVec3& InBoundsOffset, const float InBoundsMagnitude, PxGeometry* InGeometry, FMaterial InMaterial)
			: LocalTM(InLocalTM)
			, InternalMaterial(InMaterial)
			, Geometry(InGeometry)
			, BoundsOffset(InBoundsOffset)
			, BoundsMagnitude(InBoundsMagnitude)
			, UserData(nullptr)
		{
			Material = &InternalMaterial;
		}
	};

}

#endif // WITH_PHYSX