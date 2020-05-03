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
