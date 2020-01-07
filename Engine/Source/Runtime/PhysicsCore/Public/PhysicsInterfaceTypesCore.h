// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "PhysicsInterfaceDeclaresCore.h"

#include "Chaos/CollisionFilterData.h"

struct FBodyInstance;

struct FActorCreationParams
{
	FActorCreationParams()
		: Scene(nullptr)
		, BodyInstance(nullptr)
		, InitialTM(FTransform::Identity)
		, bStatic(false)
		, bQueryOnly(false)
		, bEnableGravity(false)
		, DebugName(nullptr)
	{}

	FPhysScene* Scene;
	FBodyInstance* BodyInstance;
	FTransform InitialTM;
	bool bStatic;
	bool bQueryOnly;
	bool bEnableGravity;
	char* DebugName;
};

/**
* Type of query for object type or trace type
* Trace queries correspond to trace functions with TravelChannel/ResponseParams
* Object queries correspond to trace functions with Object types
*/
enum class ECollisionQuery : uint8
{
	ObjectQuery = 0,
	TraceQuery = 1
};

enum class ECollisionShapeType : uint8
{
	Sphere,
	Plane,
	Box,
	Capsule,
	Convex,
	Trimesh,
	Heightfield,
	None
};

/** Helper struct holding physics body filter data during initialisation */
struct FBodyCollisionFilterData
{
	FCollisionFilterData SimFilter;
	FCollisionFilterData QuerySimpleFilter;
	FCollisionFilterData QueryComplexFilter;
};

struct FBodyCollisionFlags
{
	FBodyCollisionFlags()
		: bEnableSimCollisionSimple(false)
		, bEnableSimCollisionComplex(false)
		, bEnableQueryCollision(false)
	{
	}

	bool bEnableSimCollisionSimple;
	bool bEnableSimCollisionComplex;
	bool bEnableQueryCollision;
};


/** Helper object to hold initialisation data for shapes */
struct FBodyCollisionData
{
	FBodyCollisionFilterData CollisionFilterData;
	FBodyCollisionFlags CollisionFlags;
};

static void SetupNonUniformHelper(FVector InScale3D, float& OutMinScale, float& OutMinScaleAbs, FVector& OutScale3DAbs)
{
	// if almost zero, set min scale
	// @todo fixme
	if (InScale3D.IsNearlyZero())
	{
		// set min scale
		InScale3D = FVector(0.1f);
	}

	OutScale3DAbs = InScale3D.GetAbs();
	OutMinScaleAbs = OutScale3DAbs.GetMin();

	OutMinScale = FMath::Max3(InScale3D.X, InScale3D.Y, InScale3D.Z) < 0.f ? -OutMinScaleAbs : OutMinScaleAbs;	//if all three values are negative make minScale negative

	if (FMath::IsNearlyZero(OutMinScale))
	{
		// only one of them can be 0, we make sure they have mini set up correctly
		OutMinScale = 0.1f;
		OutMinScaleAbs = 0.1f;
	}
}

/** Util to determine whether to use NegX version of mesh, and what transform (rotation) to apply. */
static bool CalcMeshNegScaleCompensation(const FVector& InScale3D, FTransform& OutTransform)
{
	OutTransform = FTransform::Identity;

	if (InScale3D.Y > 0.f)
	{
		if (InScale3D.Z > 0.f)
		{
			// no rotation needed
		}
		else
		{
			// y pos, z neg
			OutTransform.SetRotation(FQuat(FVector(0.0f, 1.0f, 0.0f), PI));
			//OutTransform.q = PxQuat(PxPi, PxVec3(0,1,0));
		}
	}
	else
	{
		if (InScale3D.Z > 0.f)
		{
			// y neg, z pos
			//OutTransform.q = PxQuat(PxPi, PxVec3(0,0,1));
			OutTransform.SetRotation(FQuat(FVector(0.0f, 0.0f, 1.0f), PI));
		}
		else
		{
			// y neg, z neg
			//OutTransform.q = PxQuat(PxPi, PxVec3(1,0,0));
			OutTransform.SetRotation(FQuat(FVector(1.0f, 0.0f, 0.0f), PI));
		}
	}

	// Use inverted mesh if determinant is negative
	return (InScale3D.X * InScale3D.Y * InScale3D.Z) < 0.f;
}