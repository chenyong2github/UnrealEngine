// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/SelfUnionMeshesOp.h"

#include "Operations/MeshSelfUnion.h"

#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"

void FSelfUnionMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FSelfUnionMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	*ResultMesh = *CombinedMesh;

	if (Progress->Cancelled())
	{
		return;
	}

	FMeshSelfUnion Union(ResultMesh.Get());
	Union.WindingThreshold = WindingNumberThreshold;
	Union.bTrimFlaps = bTrimFlaps;
	bool bSuccess = Union.Compute();

	if (Progress->Cancelled())
	{
		return;
	}

	if (!bSuccess && bAttemptFixHoles)
	{
		FMeshBoundaryLoops OpenBoundary(ResultMesh.Get(), false);
		TSet<int> ConsiderEdges(Union.CreatedBoundaryEdges);
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
			FMinimalHoleFiller Filler(ResultMesh.Get(), Loop);
			Filler.Fill();
		}
		for (int EID : Union.CreatedBoundaryEdges)
		{
			if (ResultMesh->IsEdge(EID) && ResultMesh->IsBoundaryEdge(EID))
			{
				CreatedBoundaryEdges.Add(EID);
			}
		}
	}
	else
	{
		CreatedBoundaryEdges = Union.CreatedBoundaryEdges;
	}
}