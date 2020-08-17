// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"

#include "ConstrainedMeshDeformationSolver.h"


/**
 * FConstrainedMeshDeformer solves detail-preserving mesh deformation problems with arbitrary position constraints.
 * The initial Mesh Laplacians are defined as Biharmonic * VtxPositions
 * A direct solver is used, currently LU decomposition.
 *
 * Boundary vertices are fixed to their input positions.
 */
class DYNAMICMESH_API FConstrainedMeshDeformer : public FConstrainedMeshDeformationSolver
{
public:
	FConstrainedMeshDeformer(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme LaplacianType);
	~FConstrainedMeshDeformer() override {}

	bool Deform(TArray<FVector3d>& PositionBuffer) override;

private:

	FSOAPositions LaplacianVectors;
	FSOAPositions OriginalInteriorPositions;
};



/**
 * FSoftMeshDeformer solves detail-preserving mesh deformation problems with arbitrary position constraints.
 * The initial Mesh Laplacians are defined as Biharmonic * VtxPositions
 * A direct solver is used, currently LU decomposition.
 * Clamped Cotangent weights with Voronoi area are used.
 * Boundary Vertices are *not* fixed, they are included in the system and so should have soft constraints set similar to any other vertex
 */
class DYNAMICMESH_API FSoftMeshDeformer : public FSoftMeshDeformationSolver
{
public:
	FSoftMeshDeformer(const FDynamicMesh3& DynamicMesh);
	~FSoftMeshDeformer() override {}

	bool Deform(TArray<FVector3d>& PositionBuffer) override;

private:

	FSOAPositions LaplacianVectors;
	FSOAPositions OriginalPositions;
};
