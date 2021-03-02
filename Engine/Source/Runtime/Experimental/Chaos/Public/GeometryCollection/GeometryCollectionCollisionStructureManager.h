// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "UObject/ObjectMacros.h"

namespace Chaos { class FErrorReporter; }

namespace Chaos
{
	class FTriangleMesh;
	class FLevelSet;

	template <typename T, int d>
	class TParticles;
	using FParticles = TParticles<FReal, 3>;
}

class CHAOS_API FCollisionStructureManager
{
public:
	FCollisionStructureManager();
	virtual ~FCollisionStructureManager() {}

	typedef TArray<Chaos::FVec3> FPoints;
	typedef Chaos::FBVHParticles FSimplicial;
	typedef Chaos::FImplicitObject FImplicit;

	/**
	 * Culls particles inside \p Implicit, and coincident particles (defined by 
	 * being within 1/20'th of the size of the domain from one another), truncates
	 * at \p CollisionParticlesMaxInput, and returns a bounding volume hierarchy
	 * of the remainder.
	 */
	static FSimplicial* NewSimplicial(
		const Chaos::FParticles& Vertices,
		const Chaos::FTriangleMesh& TriMesh,
		const Chaos::FImplicitObject* Implicit,
		const int32 CollisionParticlesMaxInput);
	
	/**
	 * Culls particles by importance (See \c FTriangleMesh::GetVertexImportanceOrdering()),
	 * and returns a bounding volume hierarchy of the remainder.
	 */
	static FSimplicial* NewSimplicial(
		const Chaos::FParticles& AllParticles,
		const TManagedArray<int32>& BoneMap,
		const ECollisionTypeEnum CollisionType,
		Chaos::FTriangleMesh& TriMesh,
		const float CollisionParticlesFraction);

	/**
	 * Calls \c SetDoCollide(false) and \c SetConvex(false) on \p Implicit if 
	 * \p CollisionType is \c ECollisionTypeEnum::Chaos_Surface_Volumetric.
	 */
	static void UpdateImplicitFlags(
		FImplicit* Implicit, 
		const ECollisionTypeEnum CollisionType);

	/**
	 * Build a box, sphere, or level set based on \p ImplicitType.
	 * 
	 *	\p ErrorReporter - level set only
	 *	\p MeshParticles - level set only
	 *	\p TriMesh - level set only
	 *	\p CollisionBounds - box and level set
	 *	\p Radius - sphere only
	 *	\p MinRes - level set only
	 *	\p MaxRes - level set only
	 *	\p CollisionObjectReduction - shrink percentage; value of 10 reduces by 
	 *	   10%, 0 does nothing, 100 shrinks to zero.
	 *	\p CollisionType - param forwarded to \c UpdateImplictFlags().
	 *	\p ImplicitType - type of implicit shape to build.
	 */
	static FImplicit * NewImplicit(
		Chaos::FErrorReporter ErrorReporter,
		const Chaos::FParticles& MeshParticles,
		const Chaos::FTriangleMesh& TriMesh,
		const FBox& CollisionBounds,
		const float Radius,
		const int32 MinRes,
		const int32 MaxRes,
		const float CollisionObjectReduction,
		const ECollisionTypeEnum CollisionType,
		const EImplicitTypeEnum ImplicitType);

	static FImplicit* NewImplicitBox(
		const FBox& CollisionBounds,
		const float CollisionObjectReduction,
		const ECollisionTypeEnum CollisionType);

	static FImplicit* NewImplicitSphere(
		const float Radius,
		const float CollisionObjectReduction,
		const ECollisionTypeEnum CollisionType);

	static FImplicit* NewImplicitLevelset(
		Chaos::FErrorReporter ErrorReporter,
		const Chaos::FParticles& MeshParticles,
		const Chaos::FTriangleMesh& TriMesh,
		const FBox& CollisionBounds,
		const int32 MinRes,
		const int32 MaxRes,
		const float CollisionObjectReduction,
		const ECollisionTypeEnum CollisionType);

	static Chaos::FLevelSet* NewLevelset(
		Chaos::FErrorReporter ErrorReporter,
		const Chaos::FParticles& MeshParticles,
		const Chaos::FTriangleMesh& TriMesh,
		const FBox& CollisionBounds,
		const int32 MinRes,
		const int32 MaxRes,
		const ECollisionTypeEnum CollisionType);

	static FVector CalculateUnitMassInertiaTensor(
		const FBox& BoundingBox,
		const float Radius,
		const EImplicitTypeEnum ImplicitType);

	static float CalculateVolume(
		const FBox& BoundingBox,
		const float Radius,
		const EImplicitTypeEnum ImplicitType);
};
