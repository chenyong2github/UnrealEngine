// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/ConstrainedMeshSmoother.h"
#include "Solvers/Internal/LaplacianMeshSmoother.h"


TUniquePtr<IConstrainedMeshSolver> UE::MeshDeformation::ConstructConstrainedMeshSmoother(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh)
{
	TUniquePtr<IConstrainedMeshSolver> Deformer(new FBiHarmonicMeshSmoother(DynamicMesh, WeightScheme));
	return Deformer;
}

