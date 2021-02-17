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

	FLatticeDeformerOp(TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> InOriginalMesh,
					   TSharedPtr<FFFDLattice, ESPMode::ThreadSafe> InLattice,
					   const TArray<FVector3d>& InLatticeControlPoints,
					   ELatticeInterpolation InInterpolationType);

	// FDynamicMeshOperator implementation
	void CalculateResult(FProgressCancel* Progress) override;

protected:

	// Inputs
	const TSharedPtr<const FFFDLattice, ESPMode::ThreadSafe> Lattice;
	const TSharedPtr<const FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	const TArray<FVector3d> LatticeControlPoints;
	ELatticeInterpolation InterpolationType;
};

