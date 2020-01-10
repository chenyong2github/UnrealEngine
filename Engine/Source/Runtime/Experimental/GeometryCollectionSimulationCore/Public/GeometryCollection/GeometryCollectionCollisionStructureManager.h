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
	template <typename T>
	class TTriangleMesh;
	template <typename T, int d>
	class TLevelSet;
	template <typename T, int d>
	class TParticles;
}

class GEOMETRYCOLLECTIONSIMULATIONCORE_API FCollisionStructureManager
{
public:
	FCollisionStructureManager();
	virtual ~FCollisionStructureManager() {}

	typedef TArray<Chaos::TVector<float, 3>> FPoints;
	typedef Chaos::TBVHParticles<float,3> FSimplicial;
	typedef Chaos::FImplicitObject FImplicit;

	static FSimplicial* NewSimplicial(
		const Chaos::TParticles<float, 3>& Vertices,
		Chaos::TTriangleMesh<float>& TriMesh,
		const Chaos::FImplicitObject* Implicit,
		int32 CollisionParticlesMaxInput
	);
	
	
	static FSimplicial * NewSimplicial(
		const Chaos::TParticles<float,3>& AllParticles,
		const TManagedArray<int32>& BoneMap,
		const ECollisionTypeEnum CollisionType,
		Chaos::TTriangleMesh<float>& TriMesh,
		const float CollisionParticlesFraction
	);

	static FImplicit * NewImplicit(
		Chaos::FErrorReporter& ErrorReporter,
		const Chaos::TParticles<float, 3>& MeshParticles,
		const Chaos::TTriangleMesh<float>& TriMesh,
		const FBox& CollisionBoundsArray,
		const float Radius,
		const int32 MinRes,
		const int32 MaxRes,
		const float CollisionObjectReduction,
		const ECollisionTypeEnum CollisionType,
		const EImplicitTypeEnum ImplicitType
	);

	static FVector CalculateUnitMassInertiaTensor(
		const FBox& BoundingBox,
		const float Radius,
		const EImplicitTypeEnum ImplicitType
	);

	static float CalculateVolume(
		const FBox& BoundingBox,
		const float Radius,
		const EImplicitTypeEnum ImplicitType
	);


	static Chaos::TLevelSet<float, 3>* NewLevelset(
		Chaos::FErrorReporter& ErrorReporter,
		const Chaos::TParticles<float, 3>& MeshParticles,
		const Chaos::TTriangleMesh<float>& TriMesh,
		const FBox& CollisionBounds,
		int32 MinRes,
		int32 MaxRes,
		ECollisionTypeEnum CollisionType
	);

	static void UpdateImplicitFlags(FImplicit* Implicit, ECollisionTypeEnum CollisionType);
};
