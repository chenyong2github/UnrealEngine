// Copyright Epic Games, Inc. All Rights Reserved.

#include "CuttingOps/PlaneCutOp.h"

#include "Engine/StaticMesh.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "Operations/MeshPlaneCut.h"
#include "ConstrainedDelaunay2.h"


void FPlaneCutOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FPlaneCutOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);

	FMeshPlaneCut Cut(ResultMesh.Get(), LocalPlaneOrigin, LocalPlaneNormal);

	if (Progress->Cancelled())
	{
		return;
	}
	Cut.UVScaleFactor = 1.0 / ResultMesh->GetBounds().MaxDim();


	if (Progress->Cancelled())
	{
		return;
	}
	Cut.Cut();


	if (Progress->Cancelled())
	{
		return;
	}
	if (bFillCutHole)
	{
		Cut.HoleFill(ConstrainedDelaunayTriangulate<double>, bFillSpans);
	}
}