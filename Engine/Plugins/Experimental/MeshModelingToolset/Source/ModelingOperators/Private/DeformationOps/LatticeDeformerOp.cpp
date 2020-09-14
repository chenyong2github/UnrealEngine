// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationOps/LatticeDeformerOp.h"
#include "Operations/FFDLattice.h"

void FLatticeDeformerOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh);

	if (Progress->Cancelled())
	{
		return;
	}

	TArray<FVector3d> DeformedPositions;
	Lattice->GetDeformedMeshVertexPositions(LatticeControlPoints, DeformedPositions, bUseCubicInterpolation, Progress);

	check(ResultMesh->VertexCount() == DeformedPositions.Num());

	for (int vid : ResultMesh->VertexIndicesItr())
	{
		ResultMesh->SetVertex(vid, DeformedPositions[vid]);
	}
}

FLatticeDeformerOp::FLatticeDeformerOp(TSharedPtr<FDynamicMesh3> InOriginalMesh,
									   TSharedPtr<FFFDLattice> InLattice,
									   const TArray<FVector3d>& InLatticeControlPoints,
									   bool InUseCubicInterpolation) :
	Lattice(InLattice),
	OriginalMesh(InOriginalMesh),
	LatticeControlPoints(InLatticeControlPoints),
	bUseCubicInterpolation(InUseCubicInterpolation)
{}
