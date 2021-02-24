// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/BooleanMeshesOp.h"

#include "Operations/MeshBoolean.h"

#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"

void FBooleanMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FBooleanMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}
	check(Meshes.Num() == 2 && Transforms.Num() == 2);
	
	int FirstIdx = 0;
	if (Operation == ECSGOperation::DifferenceBA || Operation == ECSGOperation::TrimB)
	{
		FirstIdx = 1;
	}
	int OtherIdx = 1 - FirstIdx;

	FMeshBoolean::EBooleanOp Op;
	// convert UI enum to algorithm enum
	switch (Operation)
	{
	case ECSGOperation::DifferenceAB:
	case ECSGOperation::DifferenceBA:
		Op = FMeshBoolean::EBooleanOp::Difference;
		break;
	case ECSGOperation::TrimA:
	case ECSGOperation::TrimB:
		Op = FMeshBoolean::EBooleanOp::TrimInside;
		break;
	case ECSGOperation::Union:
		Op = FMeshBoolean::EBooleanOp::Union;
		break;
	case ECSGOperation::Intersect:
		Op = FMeshBoolean::EBooleanOp::Intersect;
		break;
	default:
		check(false); // all conversion cases should be implemented
		Op = FMeshBoolean::EBooleanOp::Union;
	}

	FMeshBoolean MeshBoolean(Meshes[FirstIdx].Get(), (FTransform3d)Transforms[FirstIdx], Meshes[OtherIdx].Get(), (FTransform3d)Transforms[OtherIdx], ResultMesh.Get(), Op);
	if (Progress->Cancelled())
	{
		return;
	}

	MeshBoolean.bPutResultInInputSpace = false;
	MeshBoolean.Progress = Progress;
	bool bSuccess = MeshBoolean.Compute();
	ResultTransform = MeshBoolean.ResultTransform;

	if (Progress->Cancelled())
	{
		return;
	}

	if (MeshBoolean.CreatedBoundaryEdges.Num() > 0 && bAttemptFixHoles)
	{
		FMeshBoundaryLoops OpenBoundary(MeshBoolean.Result, false);
		TSet<int> ConsiderEdges(MeshBoolean.CreatedBoundaryEdges);
		OpenBoundary.EdgeFilterFunc = [&ConsiderEdges](int EID)
		{
			return ConsiderEdges.Contains(EID);
		};
		OpenBoundary.Compute();

		if (Progress->Cancelled())
		{
			return;
		}

		for (FEdgeLoop& Loop : OpenBoundary.Loops)
		{
			FMinimalHoleFiller Filler(MeshBoolean.Result, Loop);
			Filler.Fill();
		}
		for (int EID : MeshBoolean.CreatedBoundaryEdges)
		{
			if (MeshBoolean.Result->IsEdge(EID) && MeshBoolean.Result->IsBoundaryEdge(EID))
			{
				CreatedBoundaryEdges.Add(EID);
			}
		}
	}
	else
	{
		CreatedBoundaryEdges = MeshBoolean.CreatedBoundaryEdges;
	}
}