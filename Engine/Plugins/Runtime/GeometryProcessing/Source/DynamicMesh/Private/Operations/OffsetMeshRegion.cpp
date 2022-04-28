// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/OffsetMeshRegion.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "Selections/MeshVertexSelection.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "Selections/MeshConnectedComponents.h"
#include "Operations/ExtrudeMesh.h"
#include "DynamicSubmesh3.h"

using namespace UE::Geometry;

namespace OffsetMeshRegionLocals
{
	bool EdgesAreParallel(FDynamicMesh3* Mesh, int32 Eid1, int32 Eid2)
	{
		FIndex2i Vids1 = Mesh->GetEdgeV(Eid1);
		FIndex2i Vids2 = Mesh->GetEdgeV(Eid2);

		FVector3d Vec1 = Mesh->GetVertex(Vids1.A) - Mesh->GetVertex(Vids1.B);
		FVector3d Vec2 = Mesh->GetVertex(Vids2.A) - Mesh->GetVertex(Vids2.B);
		if (!Vec1.Normalize(KINDA_SMALL_NUMBER) || !Vec2.Normalize(KINDA_SMALL_NUMBER))
		{
			// A degenerate edge is parallel enough for our purposes
			return true;
		}
		return FMath::Abs(Vec1.Dot(Vec2)) >= 1 - KINDA_SMALL_NUMBER;
	}
}

FOffsetMeshRegion::FOffsetMeshRegion(FDynamicMesh3* mesh) : Mesh(mesh)
{
}

bool FOffsetMeshRegion::Apply()
{
	FMeshNormals Normals;
	bool bHaveVertexNormals = Mesh->HasVertexNormals();
	if (!bHaveVertexNormals && ExtrusionVectorType == EVertexExtrusionVectorType::VertexNormal)
	{
		Normals = FMeshNormals(Mesh);
		Normals.ComputeVertexNormals();
	}

	FMeshConnectedComponents RegionComponents(Mesh);
	RegionComponents.FindConnectedTriangles(Triangles);

	bool bAllOK = true;
	OffsetRegions.SetNum(RegionComponents.Num());
	for (int k = 0; k < RegionComponents.Num(); ++k)
	{
		FOffsetInfo& Region = OffsetRegions[k];
		Region.OffsetTids = MoveTemp(RegionComponents.Components[k].Indices);

		if (bOffsetFullComponentsAsSolids)
		{
			TArray<int32> AllTriangles;
			FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, Region.OffsetTids, AllTriangles);
			Region.bIsSolid = AllTriangles.Num() == Region.OffsetTids.Num();
		}

		bool bRegionOK = ApplyOffset(Region, (bHaveVertexNormals) ? nullptr : &Normals);

		bAllOK = bAllOK && bRegionOK;
	}

	return bAllOK;


}

bool FOffsetMeshRegion::ApplyOffset(FOffsetInfo& Region, FMeshNormals* UseNormals)
{
	// Store offset groups
	if (Mesh->HasTriangleGroups())
	{
		for (int32 Tid : Region.OffsetTids)
		{
			Region.OffsetGroups.AddUnique(Mesh->GetTriangleGroup(Tid));
		}
	}

	FMeshRegionBoundaryLoops InitialLoops(Mesh, Region.OffsetTids, false);
	bool bOK = InitialLoops.Compute();
	if (bOK == false)
	{
		return false;
	}

	AllModifiedAndNewTriangles.Append(Region.OffsetTids);

	// Before we start changing triangles, prepare by allocating group IDs that we'll use
	// for the stitched sides (doing it before changes to the mesh allows user-provided
	// functions to operate on the original mesh).
	TArray<TArray<int32>> LoopsEdgeGroups;
	TArray<int32> NewGroupIDs;
	LoopsEdgeGroups.SetNum(InitialLoops.Loops.Num());
	for (int32 i = 0; i < InitialLoops.Loops.Num(); ++i)
	{
		TArray<int32>& LoopEids = InitialLoops.Loops[i].Edges;
		int32 NumEids = LoopEids.Num();

		if (!ensure(NumEids > 2))
		{
			// Shouldn't actually happen because we're extruding triangles
			continue;
		}

		TArray<int32>& CurrentEdgeGroups = LoopsEdgeGroups[i];
		CurrentEdgeGroups.SetNumUninitialized(NumEids);
		CurrentEdgeGroups[0] = Mesh->AllocateTriangleGroup();
		NewGroupIDs.Add(CurrentEdgeGroups[0]);

		// Propagate the group backwards first so we don't allocate an unnecessary group
		// at the end and then have to fix it.
		int32 LastDifferentGroupIndex = NumEids - 1;
		while (LastDifferentGroupIndex > 0
			&& LoopEdgesShouldHaveSameGroup(LoopEids[0], LoopEids[LastDifferentGroupIndex]))
		{
			CurrentEdgeGroups[LastDifferentGroupIndex] = CurrentEdgeGroups[0];
			--LastDifferentGroupIndex;
		}

		// Now add new groups forward
		for (int32 j = 1; j <= LastDifferentGroupIndex; ++j)
		{
			if (!LoopEdgesShouldHaveSameGroup(LoopEids[j], LoopEids[j - 1]))
			{
				CurrentEdgeGroups[j] = Mesh->AllocateTriangleGroup();
				NewGroupIDs.Add(CurrentEdgeGroups[j]);
			}
			else
			{
				CurrentEdgeGroups[j] = CurrentEdgeGroups[j-1];
			}
		}
	}

	FDynamicMeshEditor Editor(Mesh);
	TArray<FDynamicMeshEditor::FLoopPairSet> LoopPairs;

	FDynamicMeshEditResult DuplicateResult;
	if (Region.bIsSolid)
	{
		// In the solid case, we want to duplicate the region.
		FMeshIndexMappings IndexMap;
		Editor.DuplicateTriangles(Region.OffsetTids, IndexMap, DuplicateResult);

		AllModifiedAndNewTriangles.Append(DuplicateResult.NewTriangles);

		// Populate LoopPairs
		for (FEdgeLoop& BaseLoop : InitialLoops.Loops)
		{
			LoopPairs.Add(FDynamicMeshEditor::FLoopPairSet());
			FDynamicMeshEditor::FLoopPairSet& LoopPair = LoopPairs.Last();

			// Which loops we choose as the outer/inner will determine whether the
			// sides are stitched inside out or not. The original Tids are the ones
			// that are offset. In a positive offset, we want old as the "outer" and
			// new as "inner". In negative offset, we want the reverse to allow our
			// stitching code to still have the sides face outward.
			TArray<int32>* OriginalVertsOut = &LoopPair.OuterVertices;
			TArray<int32>* OriginalEdgesOut = &LoopPair.OuterEdges;
			TArray<int32>* NewVertsOut = &LoopPair.InnerVertices;
			TArray<int32>* NewEdgesOut = &LoopPair.InnerEdges;
			
			if (bIsPositiveOffset)
			{
				Swap(OriginalVertsOut, NewVertsOut);
				Swap(OriginalEdgesOut, NewEdgesOut);
			}

			*OriginalVertsOut = BaseLoop.Vertices;
			*OriginalEdgesOut = BaseLoop.Edges;
			
			for (int32 Vid : BaseLoop.Vertices)
			{
				NewVertsOut->Add(IndexMap.GetNewVertex(Vid));
			}

			FEdgeLoop OtherLoop;
			bOK = ensure(OtherLoop.InitializeFromVertices(Mesh, *NewVertsOut, false));
			*NewEdgesOut = OtherLoop.Edges;
		}
	}
	else
	{
		bOK = Editor.DisconnectTriangles(Region.OffsetTids, LoopPairs, true /*bHandleBoundaryVertices*/);
	}

	if (bOK == false)
	{
		return false;
	}

	FMeshVertexSelection SelectionV(Mesh);
	SelectionV.SelectTriangleVertices(Region.OffsetTids);
	TArray<int32> SelectedVids = SelectionV.AsArray();

	// If we need to, assemble the vertex vectors for us to use (before we actually start moving things)
	TArray<FVector3d> VertexExtrudeVectors;
	if (ExtrusionVectorType == EVertexExtrusionVectorType::SelectionTriNormalsAngleWeightedAverage
		|| ExtrusionVectorType == EVertexExtrusionVectorType::SelectionTriNormalsAngleWeightedAdjusted)
	{
		VertexExtrudeVectors.SetNumUninitialized(SelectedVids.Num());

		// Used to test which triangles are in selection
		TSet<int32> TriangleSet(Region.OffsetTids);

		for (int32 i = 0; i < SelectedVids.Num(); ++i)
		{
			int32 Vid = SelectedVids[i];
			FVector3d ExtrusionVector = FVector3d::Zero();

			// Get angle-weighted normalized average vector
			for (int32 Tid : Mesh->VtxTrianglesItr(Vid))
			{
				if (TriangleSet.Contains(Tid))
				{
					double Angle = Mesh->GetTriInternalAngleR(Tid, Mesh->GetTriangle(Tid).IndexOf(Vid));
					ExtrusionVector += Angle * Mesh->GetTriNormal(Tid);
				}
			}
			ExtrusionVector.Normalize();

			if (ExtrusionVectorType == EVertexExtrusionVectorType::SelectionTriNormalsAngleWeightedAdjusted)
			{
				// Perform an angle-weighted adjustment of the vector length. For each triangle normal, the
				// length needs to be multiplied by 1/cos(theta) to place the vertex in the plane that it
				// would be in if the face was moved a unit along triangle normal (where theta is angle of
				// triangle normal to the current extrusion vector).
				double AngleSum = 0;
				double Adjustment = 0;
				for (int32 Tid : Mesh->VtxTrianglesItr(Vid))
				{
					if (TriangleSet.Contains(Tid))
					{
						double Angle = Mesh->GetTriInternalAngleR(Tid, Mesh->GetTriangle(Tid).IndexOf(Vid));
						double CosTheta = Mesh->GetTriNormal(Tid).Dot(ExtrusionVector);

						double InvertedMaxScaleFactor = FMath::Max(FMathd::ZeroTolerance, 1.0 / MaxScaleForAdjustingTriNormalsOffset);
						if (CosTheta <= InvertedMaxScaleFactor)
						{
							CosTheta = InvertedMaxScaleFactor;
						}
						Adjustment += Angle / CosTheta;

						// For the average at the end
						AngleSum += Angle;
					}
				}
				Adjustment /= AngleSum;
				ExtrusionVector *= Adjustment;
			}

			VertexExtrudeVectors[i] = ExtrusionVector;
		}
	}

	// Perform the actual vertex displacement.
	for (int32 i = 0; i < SelectedVids.Num(); ++i)
	{
		int32 Vid = SelectedVids[i];
		FVector OldPosition = Mesh->GetVertex(Vid);
		FVector ExtrusionVector = FVector::Zero();

		switch (ExtrusionVectorType)
		{
		case EVertexExtrusionVectorType::VertexNormal:
			ExtrusionVector = FVector(Mesh->GetVertexNormal(Vid));
			break;
		case EVertexExtrusionVectorType::SelectionTriNormalsAngleWeightedAverage:
		case EVertexExtrusionVectorType::SelectionTriNormalsAngleWeightedAdjusted:
			ExtrusionVector = VertexExtrudeVectors[i];
			break;
		}

		FVector3d NewPosition = OffsetPositionFunc(OldPosition, ExtrusionVector, Vid);
		Mesh->SetVertex(Vid, NewPosition);
	}

	// Stitch the loops

	bool bSuccess = true;
	int NumInitialLoops = InitialLoops.GetLoopCount();
	Region.BaseLoops.SetNum(NumInitialLoops);
	Region.OffsetLoops.SetNum(NumInitialLoops);
	Region.StitchTriangles.SetNum(NumInitialLoops);
	Region.StitchPolygonIDs.SetNum(NumInitialLoops);
	int32 LoopIndex = 0;
	for (int32 i = 0; i < LoopPairs.Num(); ++i)
	{
		FDynamicMeshEditor::FLoopPairSet& LoopPair = LoopPairs[i];
		const TArray<int32>& EdgeGroups = LoopsEdgeGroups[i];

		TArray<int32>& BaseLoopV = LoopPair.OuterVertices;
		TArray<int32>& OffsetLoopV = LoopPair.InnerVertices;
		int NumLoopV = BaseLoopV.Num();

		// stitch the loops
		FDynamicMeshEditResult StitchResult;
		bool bStitchSuccess = Editor.StitchVertexLoopsMinimal(OffsetLoopV, BaseLoopV, StitchResult);
		if (!bStitchSuccess)
		{
			bSuccess = false;
			continue;
		}

		// set the groups of the new quads along the stitch
		int NumNewQuads = StitchResult.NewQuads.Num();
		for (int32 k = 0; k < NumNewQuads; k++)
		{
			Mesh->SetTriangleGroup(StitchResult.NewQuads[k].A, EdgeGroups[k]);
			Mesh->SetTriangleGroup(StitchResult.NewQuads[k].B, EdgeGroups[k]);
		}

		// save the stitch triangles set and associated group IDs
		StitchResult.GetAllTriangles(Region.StitchTriangles[LoopIndex]);
		Region.StitchPolygonIDs[LoopIndex] = NewGroupIDs;

		AllModifiedAndNewTriangles.Append(Region.StitchTriangles[LoopIndex]);

		// for each polygon we created in stitch, set UVs and normals
		if (Mesh->HasAttributes())
		{
			float AccumUVTranslation = 0;
			FFrame3d FirstProjectFrame;
			FVector3d FrameUp;

			for (int k = 0; k < NumNewQuads; k++)
			{
				FVector3f Normal = Editor.ComputeAndSetQuadNormal(StitchResult.NewQuads[k], true);

				// align axis 0 of projection frame to first edge, then for further edges,
				// rotate around 'up' axis to keep normal aligned and frame horizontal
				FFrame3d ProjectFrame;
				if (k == 0)
				{
					FVector3d FirstEdge = Mesh->GetVertex(BaseLoopV[1]) - Mesh->GetVertex(BaseLoopV[0]);
					Normalize(FirstEdge);
					FirstProjectFrame = FFrame3d(FVector3d::Zero(), (FVector3d)Normal);
					FirstProjectFrame.ConstrainedAlignAxis(0, FirstEdge, (FVector3d)Normal);
					FrameUp = FirstProjectFrame.GetAxis(1);
					ProjectFrame = FirstProjectFrame;
				}
				else
				{
					ProjectFrame = FirstProjectFrame;
					ProjectFrame.ConstrainedAlignAxis(2, (FVector3d)Normal, FrameUp);
				}

				if (k > 0)
				{
					AccumUVTranslation += Distance(Mesh->GetVertex(BaseLoopV[k]), Mesh->GetVertex(BaseLoopV[k - 1]));
				}

				// translate horizontally such that vertical spans are adjacent in UV space (so textures tile/wrap properly)
				float TranslateU = UVScaleFactor * AccumUVTranslation;
				Editor.SetQuadUVsFromProjection(StitchResult.NewQuads[k], ProjectFrame, UVScaleFactor, FVector2f(TranslateU, 0));
			}
		}

		Region.BaseLoops[LoopIndex].InitializeFromVertices(Mesh, BaseLoopV);
		Region.OffsetLoops[LoopIndex].InitializeFromVertices(Mesh, OffsetLoopV);
		LoopIndex++;
	}

	if (Region.bIsSolid)
	{
		if (bIsPositiveOffset)
		{
			// Flip the "bottom" of the region to face outwards
			Editor.ReverseTriangleOrientations(DuplicateResult.NewTriangles, true);
		}
		else
		{
			Editor.ReverseTriangleOrientations(Region.OffsetTids, true);
		}
	}

	return bSuccess;
}

bool FOffsetMeshRegion::EdgesSeparateSameGroupsAndAreColinearAtBorder(FDynamicMesh3* Mesh, 
	int32 Eid1, int32 Eid2, bool bCheckColinearityAtBorder)
{
	FIndex2i Tris1 = Mesh->GetEdgeT(Eid1);
	FIndex2i Groups1(Mesh->GetTriangleGroup(Tris1.A),
		Tris1.B == IndexConstants::InvalidID ? IndexConstants::InvalidID : Mesh->GetTriangleGroup(Tris1.B));

	FIndex2i Tris2 = Mesh->GetEdgeT(Eid2);
	FIndex2i Groups2(Mesh->GetTriangleGroup(Tris2.A), 
		Tris2.B == IndexConstants::InvalidID ? IndexConstants::InvalidID : Mesh->GetTriangleGroup(Tris2.B));

	if (bCheckColinearityAtBorder
		&& Groups1.A == Groups2.A
		&& Groups1.B == IndexConstants::InvalidID
		&& Groups2.B == IndexConstants::InvalidID)
	{
		return OffsetMeshRegionLocals::EdgesAreParallel(Mesh, Eid1, Eid2);
	}
	else return (Groups1.A == Groups2.A && Groups1.B == Groups2.B)
		|| (Groups1.A == Groups2.B && Groups1.B == Groups2.A);
}

