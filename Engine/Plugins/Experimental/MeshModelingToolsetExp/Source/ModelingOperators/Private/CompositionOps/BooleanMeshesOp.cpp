// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/BooleanMeshesOp.h"

#include "Operations/MeshBoolean.h"
#include "MeshSimplification.h"
#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FBooleanMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FBooleanMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}
	check(Meshes.Num() == 2 && Transforms.Num() == 2);
	
	int FirstIdx = 0;
	if ((!bTrimMode && CSGOperation == ECSGOperation::DifferenceBA) || (bTrimMode && TrimOperation == ETrimOperation::TrimB))
	{
		FirstIdx = 1;
	}
	int OtherIdx = 1 - FirstIdx;

	FMeshBoolean::EBooleanOp Op;
	// convert UI enum to algorithm enum
	if (bTrimMode)
	{
		switch (TrimSide)
		{
		case ETrimSide::RemoveInside:
			Op = FMeshBoolean::EBooleanOp::TrimInside;
			break;
		case ETrimSide::RemoveOutside:
			Op = FMeshBoolean::EBooleanOp::TrimOutside;
			break;
		default:
			check(false);
			Op = FMeshBoolean::EBooleanOp::TrimInside;
		}
	}
	else
	{
		switch (CSGOperation)
		{
		case ECSGOperation::DifferenceAB:
		case ECSGOperation::DifferenceBA:
			Op = FMeshBoolean::EBooleanOp::Difference;
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
	}

	FMeshBoolean MeshBoolean(Meshes[FirstIdx].Get(), (FTransform3d)Transforms[FirstIdx], Meshes[OtherIdx].Get(), (FTransform3d)Transforms[OtherIdx], ResultMesh.Get(), Op);
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	MeshBoolean.bPutResultInInputSpace = false;
	MeshBoolean.bSimplifyAlongNewEdges = bTryCollapseExtraEdges;
	MeshBoolean.Progress = Progress;
	bool bSuccess = MeshBoolean.Compute();
	ResultTransform = MeshBoolean.ResultTransform;

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	CreatedBoundaryEdges = MeshBoolean.CreatedBoundaryEdges;

	// try to fill cracks/holes in boolean result
	if (CreatedBoundaryEdges.Num() > 0 && bAttemptFixHoles)
	{
		FMeshBoundaryLoops OpenBoundary(MeshBoolean.Result, false);
		TSet<int> ConsiderEdges(CreatedBoundaryEdges);
		OpenBoundary.EdgeFilterFunc = [&ConsiderEdges](int EID)
		{
			return ConsiderEdges.Contains(EID);
		};
		OpenBoundary.Compute();

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		for (FEdgeLoop& Loop : OpenBoundary.Loops)
		{
			FMinimalHoleFiller Filler(MeshBoolean.Result, Loop);
			Filler.Fill();
		}

		TArray<int32> UpdatedBoundaryEdges;
		for (int EID : CreatedBoundaryEdges)
		{
			if (MeshBoolean.Result->IsEdge(EID) && MeshBoolean.Result->IsBoundaryEdge(EID))
			{
				UpdatedBoundaryEdges.Add(EID);
			}
		}
		CreatedBoundaryEdges = MoveTemp(UpdatedBoundaryEdges);
	}
}