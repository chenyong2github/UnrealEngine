// Copyright Epic Games, Inc. All Rights Reserved.

#include "CuttingOps/EdgeLoopInsertionOp.h"

#include "Util/ProgressCancel.h"

void FEdgeLoopInsertionOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FEdgeLoopInsertionOp::GetLoopEdgeLocations(TArray<TPair<FVector3d, FVector3d>>& EndPointPairsOut) const
{
	EndPointPairsOut.Reset();
	for (int32 Eid : LoopEids)
	{
		TPair<FVector3d, FVector3d> Endpoints;
		ResultMesh->GetEdgeV(Eid, Endpoints.Key, Endpoints.Value);
		EndPointPairsOut.Add(MoveTemp(Endpoints));
	}
}

void FEdgeLoopInsertionOp::CalculateResult(FProgressCancel* Progress)
{
	bSucceeded = false;

	if (Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh, true, true, true, true);
	ResultTopology = MakeShared<FGroupTopology>();
	*ResultTopology = *OriginalTopology;
	ResultTopology->RetargetOnClonedMesh(ResultMesh.Get());

	if (Progress->Cancelled())
	{
		return;
	}

	if (InputLengths.Num() > 0)
	{
		FGroupEdgeInserter::FEdgeLoopInsertionParams Params;
		Params.Mesh = ResultMesh.Get();
		Params.Topology = ResultTopology.Get();
		Params.GroupEdgeID = GroupEdgeID;
		Params.Mode = Mode;
		Params.SortedInputLengths = &InputLengths;
		Params.bInputsAreProportions = bInputsAreProportions;
		Params.StartCornerID = StartCornerID;
		Params.VertexTolerance = VertexTolerance;

		FGroupEdgeInserter Inserter;
		bSucceeded = Inserter.InsertEdgeLoops(Params, &LoopEids, Progress);
	}
}
