// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsEngine/AggregateGeom.h"
#include "Containers/ArrayView.h"

struct FKSphereElem;

namespace physx
{
	class PxSphereGeometry;
	class PxTransform;

	class PxSphereGeometry;
	class PxBoxGeometry;
	class PxCapsuleGeometry;
	class PxConvexMeshGeometry;
	class PxTriangleMesh;
	class PxTriangleMeshGeometry;
}

class UBodySetup;

/** Helper struct for iterating over shapes in a body setup.*/
struct ENGINE_API FBodySetupShapeIterator
{
	// Only valid for PhysX and Chaos - will fail to compile when used otherwise
	using TTransform = TChooseClass<WITH_PHYSX && PHYSICS_INTERFACE_PHYSX, physx::PxTransform, TChooseClass<WITH_CHAOS, FTransform, void>::Result>::Result;

	FBodySetupShapeIterator();

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	FBodySetupShapeIterator(const FVector& InScale3D, const FTransform& InRelativeTM, bool bDoubleSidedTrimeshes);
	/** Iterates over the elements array and creates the needed geometry and local pose. Note that this memory is on the stack so it's illegal to use it by reference outside the lambda */
	template <typename ElemType, typename GeomType>
	void ForEachShape(const TArrayView<ElemType>& Elements, TFunctionRef<void(const ElemType& Elem, const GeomType& Geom, const TTransform& LocalPose, float ContactOffset, float RestOffset)> VisitorFunc) const;

	/** Helper function to determine contact offset params */
	static void GetContactOffsetParams(float& InOutContactOffsetFactor, float& InOutMinContactOffset, float& InOutMaxContactOffset);

private:

	template <typename ElemType, typename GeomType> bool PopulateGeometryAndTransform(const ElemType& Elem, GeomType& Geom, TTransform& OutTM) const;
	template <typename GeomType> float ComputeContactOffset(const GeomType& Geom) const;
	template <typename ElemType> float ComputeRestOffset(const ElemType& Geom) const;
	template <typename ElemType> FString GetDebugName() const;
#endif

private:
	FVector Scale3D;
	const FTransform& RelativeTM;

	float MinScaleAbs;
	float MinScale;
	FVector ShapeScale3DAbs;
	FVector ShapeScale3D;

	float ContactOffsetFactor;
	float MinContactOffset;
	float MaxContactOffset;

	bool bDoubleSidedTriMeshGeo;
};

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

/// @cond DOXYGEN_WARNINGS

//Explicit export of template instantiation 
extern template ENGINE_API void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKSphereElem>&, TFunctionRef<void(const FKSphereElem&, const physx::PxSphereGeometry&, const FBodySetupShapeIterator::TTransform&, float, float)>) const;
extern template ENGINE_API void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKBoxElem>&, TFunctionRef<void(const FKBoxElem&, const physx::PxBoxGeometry&, const FBodySetupShapeIterator::TTransform&, float, float)>) const;
extern template ENGINE_API void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKSphylElem>&, TFunctionRef<void(const FKSphylElem&, const physx::PxCapsuleGeometry&, const FBodySetupShapeIterator::TTransform&, float, float)>) const;
extern template ENGINE_API void FBodySetupShapeIterator::ForEachShape(const TArrayView<FKConvexElem>&, TFunctionRef<void(const FKConvexElem&, const physx::PxConvexMeshGeometry&, const FBodySetupShapeIterator::TTransform&, float, float)>) const;
extern template ENGINE_API void FBodySetupShapeIterator::ForEachShape(const TArrayView<physx::PxTriangleMesh*>&, TFunctionRef<void(physx::PxTriangleMesh* const &, const physx::PxTriangleMeshGeometry&, const FBodySetupShapeIterator::TTransform&, float, float)>) const;

/// @endcond

#endif //WITH_PHYSX