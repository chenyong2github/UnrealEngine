// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/SelfUnionMeshesOp.h"

#include "Operations/MeshSelfUnion.h"

#include "MeshSimplification.h"

#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"

void FSelfUnionMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FSelfUnionMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	*ResultMesh = *CombinedMesh;

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	FMeshSelfUnion Union(ResultMesh.Get());
	Union.WindingThreshold = WindingNumberThreshold;
	Union.bTrimFlaps = bTrimFlaps;
	Union.bTrackAllNewEdges = (bTryCollapseExtraEdges);
	bool bSuccess = Union.Compute();

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	CreatedBoundaryEdges = Union.CreatedBoundaryEdges;

	// Boolean operation is based on edge splits, which results in spurious vertices
	// along straight intersection edges. Try to collapse away those extra vertices.
	if (bTryCollapseExtraEdges)
	{
		FDynamicMesh3* TargetMesh = ResultMesh.Get();

		FQEMSimplification Simplifier(TargetMesh);
		Simplifier.bAllowSeamCollapse = true;
		if (TargetMesh->Attributes())
		{
			TargetMesh->Attributes()->SplitAllBowties();		// eliminate any bowties that might have formed on UV seams.
		}

		FMeshConstraints Constraints;
		FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, *TargetMesh,
			EEdgeRefineFlags::NoConstraint, EEdgeRefineFlags::NoConstraint, EEdgeRefineFlags::NoConstraint,
			true, true, true);
		Simplifier.SetExternalConstraints(MoveTemp(Constraints));

		Simplifier.SimplifyToMinimalPlanar(TryCollapseExtraEdgesPlanarThresh,
			[&Union](int32 eid) { return Union.AllNewEdges.Contains(eid); });

		// update boundary-edge set as we may have collapsed some during this process
		TArray<int32> UpdatedBoundaryEdges;
		for (int32 eid : CreatedBoundaryEdges)
		{
			if (ResultMesh->IsEdge(eid))
			{
				UpdatedBoundaryEdges.Add(eid);
			}
		}
		CreatedBoundaryEdges = MoveTemp(UpdatedBoundaryEdges);
	}

	if (!bSuccess && bAttemptFixHoles)
	{
		FMeshBoundaryLoops OpenBoundary(ResultMesh.Get(), false);
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
			FMinimalHoleFiller Filler(ResultMesh.Get(), Loop);
			Filler.Fill();
		}

		TArray<int32> UpdatedBoundaryEdges;
		for (int EID : Union.CreatedBoundaryEdges)
		{
			if (ResultMesh->IsEdge(EID) && ResultMesh->IsBoundaryEdge(EID))
			{
				UpdatedBoundaryEdges.Add(EID);
			}
		}
		CreatedBoundaryEdges = MoveTemp(UpdatedBoundaryEdges);
	}

}