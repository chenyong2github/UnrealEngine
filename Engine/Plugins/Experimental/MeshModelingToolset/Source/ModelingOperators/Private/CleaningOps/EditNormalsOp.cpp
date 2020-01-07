// Copyright Epic Games, Inc. All Rights Reserved.

#include "CleaningOps/EditNormalsOp.h"

#include "Engine/StaticMesh.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"


#include "MeshNormals.h"
#include "Operations/RepairOrientation.h"
#include "DynamicMeshAABBTree3.h"

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

	if (Progress->Cancelled())
	{
		return;
	}

	// if you split normals you must always recompute as well
	bool bNeedsRecompute = bRecomputeNormals || bSplitNormals;

	if (bFixInconsistentNormals)
	{
		FMeshRepairOrientation Repair(ResultMesh.Get());
		Repair.OrientComponents();

		if (Progress->Cancelled())
		{
			return;
		}
		FDynamicMeshAABBTree3 Tree(ResultMesh.Get());
		Repair.SolveGlobalOrientation(&Tree);
	}

	if (Progress->Cancelled())
	{
		return;
	}

	if (bInvertNormals)
	{
		for (int TID : ResultMesh->TriangleIndicesItr())
		{
			ResultMesh->ReverseTriOrientation(TID);
		}

		// also reverse the normal directions (but only if a recompute isn't going to do it for us below)
		if (!bNeedsRecompute)
		{
			FDynamicMeshNormalOverlay* Normals = ResultMesh->Attributes()->PrimaryNormals();
			for (int ElID : Normals->ElementIndicesItr())
			{
				auto El = Normals->GetElement(ElID);
				Normals->SetElement(ElID, -El);
			}
		}
	}

	if (Progress->Cancelled())
	{
		return;
	}

	float NormalDotProdThreshold = FMathf::Cos(NormalSplitThreshold * FMathf::DegToRad);

	FMeshNormals FaceNormals(ResultMesh.Get());
	if (bSplitNormals)
	{
		FaceNormals.ComputeTriangleNormals();
		const TArray<FVector3d>& Normals = FaceNormals.GetNormals();
		ResultMesh->Attributes()->PrimaryNormals()->CreateFromPredicate([&Normals, &NormalDotProdThreshold](int VID, int TA, int TB)
		{
			return Normals[TA].Dot(Normals[TB]) > NormalDotProdThreshold;
		}, 0);
	}

	if (Progress->Cancelled())
	{
		return;
	}

	bool bAreaWeight = (NormalCalculationMethod == ENormalCalculationMethod::AreaWeighted || NormalCalculationMethod == ENormalCalculationMethod::AreaAngleWeighting);
	bool bAngleWeight = (NormalCalculationMethod == ENormalCalculationMethod::AngleWeighted || NormalCalculationMethod == ENormalCalculationMethod::AreaAngleWeighting);

	if (bNeedsRecompute)
	{
		FMeshNormals MeshNormals(ResultMesh.Get());
		MeshNormals.RecomputeOverlayNormals(ResultMesh->Attributes()->PrimaryNormals(), bAreaWeight, bAngleWeight);
		MeshNormals.CopyToOverlay(ResultMesh->Attributes()->PrimaryNormals(), false);
	}

	if (Progress->Cancelled())
	{
		return;
	}

	if (bAllowSharpVertices)
	{
		ResultMesh->Attributes()->PrimaryNormals()->SplitVerticesWithPredicate([this, &FaceNormals, &NormalDotProdThreshold](int ElementID, int TriID)
		{
			FVector3f ElNormal;
			ResultMesh->Attributes()->PrimaryNormals()->GetElement(ElementID, ElNormal);
			return ElNormal.Dot(FVector3f(FaceNormals.GetNormals()[TriID])) <= NormalDotProdThreshold;
		},
			[this, &FaceNormals](int ElementIdx, int TriID, float* FillVect)
		{
			FVector3f N(FaceNormals.GetNormals()[TriID]);
			FillVect[0] = N.X;
			FillVect[1] = N.Y;
			FillVect[2] = N.Z;
		}
		);
	}

}