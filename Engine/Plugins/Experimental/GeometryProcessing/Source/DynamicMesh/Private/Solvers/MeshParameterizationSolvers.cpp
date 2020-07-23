// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/MeshParameterizationSolvers.h"
#include "Solvers/Internal/MeshUVSolver.h"


TUniquePtr<UE::Solvers::IConstrainedMeshUVSolver> UE::MeshDeformation::ConstructNaturalConformalParamSolver(const FDynamicMesh3& DynamicMesh)
{
	TUniquePtr<UE::Solvers::IConstrainedMeshUVSolver> Solver(new FConstrainedMeshUVSolver(DynamicMesh, EUVSolveType::NaturalConformal));
	return Solver;
}

