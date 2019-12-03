// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	ResultMesh->Copy(*OriginalMesh, true, true, true, true);
	
	FMeshPlaneCut Cut(ResultMesh.Get(), LocalPlaneOrigin, LocalPlaneNormal);

	if (Progress->Cancelled())
	{
		return;
	}
	Cut.UVScaleFactor = UVScaleFactor;


	if (Progress->Cancelled())
	{
		return;
	}
	if (bKeepBothHalves)
	{
		int MaxSubObjectID = -1;
		TDynamicMeshScalarTriangleAttribute<int>* SubObjectAttrib = static_cast<TDynamicMeshScalarTriangleAttribute<int>*>(ResultMesh->Attributes()->GetAttachedAttribute(SubObjectsAttribIndex));
		for (int TID : ResultMesh->TriangleIndicesItr())
		{
			MaxSubObjectID = FMath::Max(MaxSubObjectID, SubObjectAttrib->GetValue(TID));
		}
		Cut.CutWithoutDelete(true, OtherHalfOffsetDistance, SubObjectAttrib, MaxSubObjectID+1);
	}
	else
	{
		Cut.Cut();
	}


	if (Progress->Cancelled())
	{
		return;
	}
	if (bFillCutHole)
	{
		Cut.HoleFill(ConstrainedDelaunayTriangulate<double>, bFillSpans);
	}

	if (Progress->Cancelled())
	{
		return;
	}
	if (bFillCutHole && bKeepBothHalves)
	{
		TDynamicMeshScalarTriangleAttribute<int>* SubObjectAttrib = static_cast<TDynamicMeshScalarTriangleAttribute<int>*>(ResultMesh->Attributes()->GetAttachedAttribute(SubObjectsAttribIndex));
		Cut.TransferTriangleLabelsToHoleFillTriangles(SubObjectAttrib);
	}
}