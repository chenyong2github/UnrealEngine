// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/ConstrainedMeshDeformer.h"
#include "Solvers/Internal/LaplacianMeshSmoother.h"


TUniquePtr<IConstrainedMeshSolver> UE::MeshDeformation::ConstructConstrainedMeshDeformer(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh)
{
	TUniquePtr<IConstrainedMeshSolver> Deformer(new FConstrainedMeshDeformer(DynamicMesh, WeightScheme));
	return Deformer;
}

