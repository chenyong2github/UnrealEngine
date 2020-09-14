// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseOps/SimpleMeshProcessingBaseOp.h"
#include "DynamicMeshEditor.h"
#include "MeshNormals.h"
#include "MeshWeights.h"
#include "MeshBoundaryLoops.h"
#include "Operations/JoinMeshLoops.h"
#include "Solvers/ConstrainedMeshDeformer.h"
#include "Implicit/GridInterpolant.h"
#include "Operations/FFDLattice.h"

class MODELINGOPERATORS_API FLatticeDeformerOp : public FDynamicMeshOperator
{
public:

	FLatticeDeformerOp(TSharedPtr<FDynamicMesh3> InOriginalMesh,
					   TSharedPtr<FFFDLattice> InLattice,
					   const TArray<FVector3d>& InLatticeControlPoints,
					   bool InUseCubicInterpolation);

	// FDynamicMeshOperator implementation
	void CalculateResult(FProgressCancel* Progress) override;

protected:

	// Inputs
	const TSharedPtr<const FFFDLattice> Lattice;
	const TSharedPtr<const FDynamicMesh3> OriginalMesh;
	const TArray<FVector3d> LatticeControlPoints;
	bool bUseCubicInterpolation;
};

