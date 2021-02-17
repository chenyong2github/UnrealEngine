// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationOps/LatticeDeformerOp.h"
#include "Operations/FFDLattice.h"

void FLatticeDeformerOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh);

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	TArray<FVector3d> DeformedPositions;
	FLatticeExecutionInfo ExecutionInfo = FLatticeExecutionInfo();
	ExecutionInfo.bParallel = true;
	Lattice->GetDeformedMeshVertexPositions(LatticeControlPoints, DeformedPositions, InterpolationType, ExecutionInfo, Progress);

	check(ResultMesh->VertexCount() == DeformedPositions.Num());

	for (int vid : ResultMesh->VertexIndicesItr())
	{
		ResultMesh->SetVertex(vid, DeformedPositions[vid]);
	}
}

FLatticeDeformerOp::FLatticeDeformerOp(TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> InOriginalMesh,
									   TSharedPtr<FFFDLattice, ESPMode::ThreadSafe> InLattice,
									   const TArray<FVector3d>& InLatticeControlPoints,
									   ELatticeInterpolation InInterpolationType) :
	Lattice(InLattice),
	OriginalMesh(InOriginalMesh),
	LatticeControlPoints(InLatticeControlPoints),
	InterpolationType(InInterpolationType)
{}
