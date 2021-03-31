// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OrientedBoxTypes.h"
#include "SegmentTypes.h"
#include "CapsuleTypes.h"
#include "SphereTypes.h"
#include "DynamicMesh3.h"
#include "TransformTypes.h"


/**
 * Supported/known types of Simple Shapes
 */
enum class ESimpleShapeType
{
	Sphere = 2,
	Box = 4,
	Capsule = 8,
	Convex = 16
};
ENUM_CLASS_FLAGS(ESimpleShapeType);


/**
 * FSphereShape is a 3D sphere
 */
class DYNAMICMESH_API FSphereShape3d
{
public:
	FSphere3d Sphere;

	FSphereShape3d() = default;

	FSphereShape3d(const FSphere3d& SphereIn)
		: Sphere(SphereIn)
	{
	}

	ESimpleShapeType GetShapeType() const { return ESimpleShapeType::Sphere; }
};


/**
 * FBoxShape is a 3D oriented box
 */
struct DYNAMICMESH_API FBoxShape3d
{
	FOrientedBox3d Box;

	FBoxShape3d() = default;

	FBoxShape3d(const FOrientedBox3d& BoxIn)
		: Box(BoxIn)
	{
	}

	ESimpleShapeType GetShapeType() const { return ESimpleShapeType::Box; }
};


/**
 * FCapsuleShape is a 3D oriented capsule/sphyl
 */
struct DYNAMICMESH_API FCapsuleShape3d
{
	FCapsule3d Capsule;

	FCapsuleShape3d() = default;

	FCapsuleShape3d(const FCapsule3d& CapsuleIn)
		: Capsule(CapsuleIn)
	{
	}

	ESimpleShapeType GetShapeType() const { return ESimpleShapeType::Capsule; }
};


/**
 * FConvexShape is a 3D convex hull, currently stored as a triangle mesh
 */
struct DYNAMICMESH_API FConvexShape3d
{
	FDynamicMesh3 Mesh;

	FConvexShape3d() = default;

	FConvexShape3d(const FDynamicMesh3& MeshIn)
		: Mesh(MeshIn)
	{
	}

	FConvexShape3d(FDynamicMesh3&& MeshIn)
		: Mesh(MoveTemp(MeshIn))
	{
	}

	ESimpleShapeType GetShapeType() const { return ESimpleShapeType::Convex; }
};


/**
 * FSimpleShapeSet stores a set of simple geometry shapes useful for things like collision detection/etc.
 * Various functions set-processing operations are supported.
 */
struct DYNAMICMESH_API FSimpleShapeSet3d
{
	TArray<FSphereShape3d> Spheres;
	TArray<FBoxShape3d> Boxes;
	TArray<FCapsuleShape3d> Capsules;
	TArray<FConvexShape3d> Convexes;

	/** @return total number of elements in all sets */
	int32 TotalElementsNum() const { return Spheres.Num() + Boxes.Num() + Capsules.Num() + Convexes.Num(); }

	/**
	 * Append elements of another shape set
	 */
	void Append(const FSimpleShapeSet3d& OtherShapeSet);


	/**
	 * Append elements of another shape set with given transform applied
	 */
	void Append(const FSimpleShapeSet3d& OtherShapeSet, const FTransform3d& Transform);


	/**
	 * Append elements of another shape set with given transforms applied
	 */
	void Append(const FSimpleShapeSet3d& OtherShapeSet, const TArray<FTransform3d>& TransformSequence);


	/**
	 * Remove any of the elements that are fully contained in larger elements
	 */
	void RemoveContainedGeometry();

	/**
	 * Sort the elements by volume and then discard all but the largest MaximumCount elements
	 */
	void FilterByVolume(int32 MaximumCount);


	/**
	 * Transform shape elements. This will be a best-effort as if there is non-uniform scaling only Convexes can be transformed correctly
	 */
	void ApplyTransform(const FTransform3d& Transform);
};

