// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "UObject/ObjectMacros.h"
#include "ClothCollisionPrim.generated.h"

/** Data for a single sphere primitive in the clothing simulation. This can either be a 
 *  sphere on its own, or part of a capsule referenced by the indices in FClothCollisionPrim_Capsule
 */
USTRUCT()
struct FClothCollisionPrim_Sphere
{
	GENERATED_BODY()

	FClothCollisionPrim_Sphere()
		: BoneIndex(INDEX_NONE)
		, Radius(0.0f)
		, LocalPosition(0, 0, 0)
	{}

	UPROPERTY()
	int32 BoneIndex;

	UPROPERTY()
	float Radius;

	UPROPERTY()
	FVector LocalPosition;
};

/** Data for a single connected sphere primitive. This should be configured after all spheres have
 *  been processed as they are really just indexing the existing spheres
 */
USTRUCT()
struct FClothCollisionPrim_SphereConnection
{
	GENERATED_BODY()

	FClothCollisionPrim_SphereConnection()
	{
		SphereIndices[0] = INDEX_NONE;
		SphereIndices[1] = INDEX_NONE;
	}

	UPROPERTY()
	int32 SphereIndices[2];
};

/** Data for a convex face. */
USTRUCT()
struct FClothCollisionPrim_ConvexFace
{
	GENERATED_BODY()

	FClothCollisionPrim_ConvexFace(): Plane(ForceInit) {}

	UPROPERTY()
	FPlane Plane;

	UPROPERTY()
	TArray<int32> Indices;
};

/**
 *	Data for a single convex element
 *	A convex is a collection of planes, in which the clothing will attempt to stay outside of the
 *	shape created by the planes combined.
 */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMEINTERFACE_API FClothCollisionPrim_Convex
{
	GENERATED_BODY()

	FClothCollisionPrim_Convex()
		: BoneIndex(INDEX_NONE)
	{}

	/** Rebuild the surface point array from the existing planes.
	 *  This is an expensive function (O(n^4) per number of planes).
	 */
	UE_DEPRECATED(4.27, "RebuildSurfacePoints is now deprecated as it doesn't provide enough data to regenerate the indices required by FKConvexElem and FConvex.")
	void RebuildSurfacePoints();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FPlane> Planes_DEPRECATED;
#endif

	UPROPERTY()
	TArray<FClothCollisionPrim_ConvexFace> Faces;

	UPROPERTY()
	TArray<FVector> SurfacePoints;  // Surface points, used by Chaos and also for visualization

	UPROPERTY()
	int32 BoneIndex;
};

/** Data for a single box primitive. */
USTRUCT()
struct FClothCollisionPrim_Box
{
	GENERATED_BODY()

	FClothCollisionPrim_Box()
		: LocalPosition(FVector::ZeroVector)
		, LocalRotation(FQuat::Identity)
		, HalfExtents(FVector::ZeroVector)
		, BoneIndex(INDEX_NONE)
	{}

	UPROPERTY()
	FVector LocalPosition;

	UPROPERTY()
	FQuat LocalRotation;

	UPROPERTY()
	FVector HalfExtents;

	UPROPERTY()
	int32 BoneIndex;
};
