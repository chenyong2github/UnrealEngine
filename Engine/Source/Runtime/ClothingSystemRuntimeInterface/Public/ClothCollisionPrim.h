// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

/**
 *	Data for a single convex element
 *	A convex is a collection of planes, in which the clothing will attempt to stay outside of the
 *	shape created by the planes combined.
 */
USTRUCT()
struct FClothCollisionPrim_Convex
{
	GENERATED_BODY()

	FClothCollisionPrim_Convex()
		: BoneIndex(INDEX_NONE)
	{}

	UPROPERTY()
	TArray<FPlane> Planes;

	UPROPERTY()
	int32 BoneIndex;
};

/** Data for a single box primitive. */
USTRUCT()
struct FClothCollisionPrim_Box
{
	GENERATED_BODY()

	FClothCollisionPrim_Box()
		: BoneIndex(INDEX_NONE)
		, LocalMin(FVector::ZeroVector)
		, LocalMax(FVector::ZeroVector)
	{}

	UPROPERTY()
	int32 BoneIndex;

	UPROPERTY()
	FVector LocalMin;

	UPROPERTY()
	FVector LocalMax;
};

