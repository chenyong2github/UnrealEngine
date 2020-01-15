// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/UVProjectionOp.h"

#include "Engine/StaticMesh.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "MatrixTypes.h"

#include "Async/ParallelFor.h"

#include "MeshNormals.h"
#include "DynamicMeshEditor.h"


void FUVProjectionOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FUVProjectionOp::CalculateResult(FProgressCancel* Progress)
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

	// TODO: get transform from mesh vertex space to projection primitive space
	FTransform ToProjPrimXF = FTransform(ResultTransform) * ProjectionTransform.Inverse();
	FTransform3d ToProjPrim(ToProjPrimXF);

	TArray<FVector3d> TransformedVertices; TransformedVertices.SetNumUninitialized(ResultMesh->VertexCount());
	ParallelFor(ResultMesh->VertexCount(), [this, &ToProjPrim, &TransformedVertices](int32 VertexID)
	{
		if (ResultMesh->IsVertex(VertexID))
		{
			TransformedVertices[VertexID] = ToProjPrim.TransformPosition(ResultMesh->GetVertex(VertexID));
		}
	});
	
	
	int NumUVLayers = ResultMesh->Attributes()->NumUVLayers();
	int LayerIndex = 0; // TODO: support non-zero UV layer, make this a member of FUVProjectionOp and a setting in the UI (will require updating dmesh attribute code)
	FDynamicMeshUVOverlay* UVLayer = ResultMesh->Attributes()->GetUVLayer(LayerIndex);
	UVLayer->ClearElements();

	FVector2f Scale = FVector2f(.5,.5)*UVScale, Offset = FVector2f(.5,.5)+UVOffset;
	// project to major axis
	auto ProjAxis = [&Scale, &Offset](const FVector3d& P, int Ax1, int Ax2, float Ax1Scale, float Ax2Scale)
	{
		return FVector2f(float(P[Ax1]) * Ax1Scale * Scale.X + Offset.X, float(P[Ax2]) * Ax2Scale * Scale.Y + Offset.Y);
	};

	int MaxVertexID = ResultMesh->MaxVertexID();
	int MaxTriangleID = ResultMesh->MaxTriangleID();
	if (ProjectionMethod == EUVProjectionMethod::Plane)
	{
		UVLayer->BeginUnsafeElementsInsert();
		// 1:1 with vertices; note we cannot just delete the UV layer and switch to vertex UVs because that would delete the normals layer too
		for (int VID = MaxVertexID-1; VID >= 0; VID--)
		{
			if (ResultMesh->IsVertex(VID))
			{
				FVector2f UV = ProjAxis(TransformedVertices[VID], 0, 1, 1, 1);
				UVLayer->InsertElement(VID, (float*)UV, VID, true);
			}
		}
		UVLayer->EndUnsafeElementsInsert();
		for (int TID = 0; TID < MaxTriangleID; TID++)
		{
			if (ResultMesh->IsTriangle(TID))
			{
				UVLayer->SetTriangle(TID, ResultMesh->GetTriangle(TID));
			}
		}
	}
	else
	{	
		// All cases in this branch require normals 
		// compute normals on the transformed vertices
		TArray<FVector3d> TransformedNormals; TransformedNormals.SetNumUninitialized(ResultMesh->TriangleCount());
		ParallelFor(ResultMesh->TriangleCount(), [this, &TransformedVertices, &TransformedNormals](int32 TriangleID)
		{
			if (ResultMesh->IsTriangle(TriangleID))
			{
				FIndex3i Triangle = ResultMesh->GetTriangle(TriangleID);
				TransformedNormals[TriangleID] = VectorUtil::Normal(TransformedVertices[Triangle.A], TransformedVertices[Triangle.B], TransformedVertices[Triangle.C]);
			}
		});
		TMap<FIndex2i, int> VertIdxAndBucketIDToElementID;

		if (ProjectionMethod == EUVProjectionMethod::Cube)
		{
			int Minor1s[3] = { 1, 0, 0 };
			int Minor2s[3] = { 2, 2, 1 };
			int Minor1Flip[3] = { -1, 1, 1 };
			int Minor2Flip[3] = { -1, -1, 1 };
			for (int TID = 0; TID < MaxTriangleID; TID++)
			{
				if (!ResultMesh->IsTriangle(TID))
				{
					continue;
				}
				const FVector3d& N = TransformedNormals[TID];
				FVector3d NAbs(FMathd::Abs(N.X), FMathd::Abs(N.Y), FMathd::Abs(N.Z));
				int MajorAxis = NAbs[0] > NAbs[1] ? (NAbs[0] > NAbs[2] ? 0 : 2) : (NAbs[1] > NAbs[2] ? 1 : 2);
				double MajorAxisSign = FMathd::Sign(N[MajorAxis]);
				int Minor1 = Minor1s[MajorAxis];
				int Minor2 = Minor2s[MajorAxis];
				int Bucket = MajorAxis;
				if (MajorAxisSign > 0)
				{
					Bucket += 3;
				}
				FIndex3i OverlayTri;
				FIndex3i SourceTri = ResultMesh->GetTriangle(TID);
				for (int SubIdx = 0; SubIdx < 3; SubIdx++)
				{
					FIndex2i ElementKey(SourceTri[SubIdx], Bucket);
					const int* ElementIdx = VertIdxAndBucketIDToElementID.Find(ElementKey);
					if (!ElementIdx)
					{
						FVector3d LocalV = TransformedVertices[SourceTri[SubIdx]];
						int NewElementIdx = UVLayer->AppendElement(ProjAxis(LocalV, Minor1, Minor2, MajorAxisSign*Minor1Flip[MajorAxis], Minor2Flip[MajorAxis]), ElementKey.A);
						VertIdxAndBucketIDToElementID.Add(ElementKey, NewElementIdx);
						OverlayTri[SubIdx] = NewElementIdx;
					}
					else
					{
						OverlayTri[SubIdx] = *ElementIdx;
					}
				}
				UVLayer->SetTriangle(TID, OverlayTri);
			}
		}
		else if (ProjectionMethod == EUVProjectionMethod::Cylinder)
		{
			double DotThresholdRejectFromPlane = FMathd::Cos(FMathd::DegToRad * CylinderProjectToTopOrBottomAngleThreshold);
			for (int TID = 0; TID < MaxTriangleID; TID++)
			{
				if (!ResultMesh->IsTriangle(TID))
				{
					continue;
				}
				const FVector3d& N = TransformedNormals[TID];
				FIndex3i OverlayTri;
				FIndex3i SourceTri = ResultMesh->GetTriangle(TID);
				if (FMathd::Abs(N.Z) > DotThresholdRejectFromPlane)
				{
					int MajorAxis = 2;
					double MajorAxisSign = FMathd::Sign(N[MajorAxis]);
					// project to +/- Z
					int Bucket = MajorAxisSign > 0 ? 1 : 0;
					int Minor1 = 0;
					int Minor2 = 1;

					// TODO: extract common major axis plane projection sub-function from cube projection & this?
					for (int SubIdx = 0; SubIdx < 3; SubIdx++)
					{
						FIndex2i ElementKey(SourceTri[SubIdx], Bucket);
						const int* ElementIdx = VertIdxAndBucketIDToElementID.Find(ElementKey);
						if (!ElementIdx)
						{
							FVector3d LocalV = TransformedVertices[SourceTri[SubIdx]];
							int NewElementIdx = UVLayer->AppendElement(ProjAxis(LocalV, Minor1, Minor2, MajorAxisSign, 1), ElementKey.A);
							VertIdxAndBucketIDToElementID.Add(ElementKey, NewElementIdx);
							OverlayTri[SubIdx] = NewElementIdx;
						}
						else
						{
							OverlayTri[SubIdx] = *ElementIdx;
						}
					}
				}
				else
				{
					// project to cylinder
					int Bucket = 3;
					FVector3d Centroid = (TransformedVertices[SourceTri.A] + TransformedVertices[SourceTri.B] + TransformedVertices[SourceTri.C]) * (1.0 / 3.0);
					double CentroidAngle = FMathd::Atan2Positive(Centroid.Y, Centroid.X);
					for (int SubIdx = 0; SubIdx < 3; SubIdx++)
					{
						FVector3d LocalV = TransformedVertices[SourceTri[SubIdx]];
						double VAngle = FMathd::Atan2Positive(LocalV.Y, LocalV.X);
						double AngleDiff = VAngle - CentroidAngle;
						if (FMathd::Abs(AngleDiff) > FMathd::Pi) {
							if (AngleDiff < 0)
							{
								VAngle += FMathd::TwoPi;
								Bucket--;
							}
							else
							{
								VAngle -= FMathd::TwoPi;
								Bucket++;
							}
						}
						FIndex2i ElementKey(SourceTri[SubIdx], Bucket);
						const int* ElementIdx = VertIdxAndBucketIDToElementID.Find(ElementKey);
						if (!ElementIdx)
						{
							FVector2f UV(-(float(VAngle)*FMathf::InvPi - 1.0f)*Scale.X + Offset.X, -float(LocalV.Z)*Scale.Y + Offset.Y);
							int NewElementIdx = UVLayer->AppendElement(UV, ElementKey.A);
							VertIdxAndBucketIDToElementID.Add(ElementKey, NewElementIdx);
							OverlayTri[SubIdx] = NewElementIdx;
						}
						else
						{
							OverlayTri[SubIdx] = *ElementIdx;
						}
					}
				}

				UVLayer->SetTriangle(TID, OverlayTri);
			}
		}


		// rescale UVs to an average world-space target length (and then re-multiply by the UVScale param)
		if (bWorldSpaceUVScale)
		{
			FDynamicMeshEditor Editor(ResultMesh.Get());
			// global rescale with a 'standard' global scale factor
			Editor.RescaleAttributeUVs(.01, true, 0, ResultTransform);
			// apply the 2D uvscale after
			for (int ElID : UVLayer->ElementIndicesItr())
			{
				UVLayer->SetElement(ElID, UVLayer->GetElement(ElID) * UVScale);
			}
		}
	}

}
