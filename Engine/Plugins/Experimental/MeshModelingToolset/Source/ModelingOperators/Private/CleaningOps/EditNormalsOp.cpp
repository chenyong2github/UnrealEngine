// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CleaningOps/EditNormalsOp.h"

#include "Engine/StaticMesh.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"


#include "MeshNormals.h"


void FEditNormalsOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FEditNormalsOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}
	bool bDiscardAttributes = false;
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);

	if (!ensureMsgf(ResultMesh->HasAttributes(), TEXT("Attributes not found on mesh? Conversion should always create them, so this operator should not need to do so.")))
	{
		ResultMesh->EnableAttributes();
	}

	if (bSplitNormals)
	{
		FMeshNormals FaceNormals(ResultMesh.Get());
		FaceNormals.ComputeTriangleNormals();
		const TArray<FVector3d>& Normals = FaceNormals.GetNormals();
		float Threshold = FMathf::Cos(NormalSplitThreshold * FMathf::DegToRad);
		ResultMesh->Attributes()->PrimaryNormals()->CreateFromPredicate([&Normals, &Threshold](int VID, int TA, int TB)
		{
			return Normals[TA].Dot(Normals[TB]) > Threshold;
		}, 0, bAllowSharpVertices);
	}

	bool bAreaWeight = (NormalCalculationMethod == ENormalCalculationMethod::AreaWeighted || NormalCalculationMethod == ENormalCalculationMethod::AreaAngleWeighting);
	bool bAngleWeight = (NormalCalculationMethod == ENormalCalculationMethod::AngleWeighted || NormalCalculationMethod == ENormalCalculationMethod::AreaAngleWeighting);

	if (bSplitNormals || bRecomputeNormals) // split normals requires recompute, as it will nuke whatever normals were in the original overlay
	{
		FMeshNormals MeshNormals(ResultMesh.Get());
		MeshNormals.RecomputeOverlayNormals(ResultMesh->Attributes()->PrimaryNormals(), bAreaWeight, bAngleWeight);
		MeshNormals.CopyToOverlay(ResultMesh->Attributes()->PrimaryNormals(), bInvertNormals);
	}
	else if (bInvertNormals)
	{
		// just invert with no recompute
		FDynamicMeshNormalOverlay* Normals = ResultMesh->Attributes()->PrimaryNormals();
		for (int ElID : Normals->ElementIndicesItr())
		{
			auto El = Normals->GetElement(ElID);
			Normals->SetElement(ElID, -El);
		}
	}

}