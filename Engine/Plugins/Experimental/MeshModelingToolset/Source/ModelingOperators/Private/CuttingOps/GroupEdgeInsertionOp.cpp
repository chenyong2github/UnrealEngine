// Copyright Epic Games, Inc. All Rights Reserved.

#include "CuttingOps/GroupEdgeInsertionOp.h"
#include "MeshRegionBoundaryLoops.h"

#include "Util/ProgressCancel.h"

void FGroupEdgeInsertionOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FGroupEdgeInsertionOp::GetEdgeLocations(TArray<TPair<FVector3d, FVector3d>>& EndPointPairsOut) const
{
	EndPointPairsOut.Reset();
	for (int32 Eid : Eids)
	{
		TPair<FVector3d, FVector3d> Endpoints;
		ResultMesh->GetEdgeV(Eid, Endpoints.Key, Endpoints.Value);
		EndPointPairsOut.Add(MoveTemp(Endpoints));
	}
}

void FGroupEdgeInsertionOp::CalculateResult(FProgressCancel* Progress)
{
	bSucceeded = false;

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh, true, true, true, true);
	ResultTopology = MakeShared<FGroupTopology>();
	*ResultTopology = *OriginalTopology;
	ResultTopology->RetargetOnClonedMesh(ResultMesh.Get());

	if (bShowingBaseMesh || (Progress && Progress->Cancelled()))
	{
		return;
	}

	FGroupEdgeInserter Inserter;
	FGroupEdgeInserter::FGroupEdgeInsertionParams Params;
	Params.Mesh = ResultMesh.Get();
	Params.Topology = ResultTopology.Get();
	Params.Mode = Mode;
	Params.VertexTolerance = VertexTolerance;
	Params.StartPoint = StartPoint;
	Params.EndPoint = EndPoint;
	Params.GroupID = CommonGroupID;
	Params.GroupBoundaryIndex = CommonBoundaryIndex;

	bSucceeded = Inserter.InsertGroupEdge(Params, &Eids, Progress);
}
