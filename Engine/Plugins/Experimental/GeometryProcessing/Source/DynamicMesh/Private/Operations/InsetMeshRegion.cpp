// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/InsetMeshRegion.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "Selections/MeshVertexSelection.h"
#include "DynamicMeshChangeTracker.h"
#include "Selections/MeshConnectedComponents.h"
#include "Distance/DistLine3Line3.h"
#include "DynamicSubmesh3.h"
#include "Solvers/ConstrainedMeshDeformer.h"
#include "DynamicMeshAABBTree3.h"
#include "MeshTransforms.h"

FInsetMeshRegion::FInsetMeshRegion(FDynamicMesh3* mesh) : Mesh(mesh)
{
}


bool FInsetMeshRegion::Apply()
{
	FMeshNormals Normals;
	bool bHaveVertexNormals = Mesh->HasVertexNormals();
	if (!bHaveVertexNormals)
	{
		Normals = FMeshNormals(Mesh);
		Normals.ComputeVertexNormals();
	}

	FMeshConnectedComponents RegionComponents(Mesh);
	RegionComponents.FindConnectedTriangles(Triangles);

	bool bAllOK = true;
	InsetRegions.SetNum(RegionComponents.Num());
	for (int32 k = 0; k < RegionComponents.Num(); ++k)
	{
		FInsetInfo& Region = InsetRegions[k];
		Region.InitialTriangles = MoveTemp(RegionComponents.Components[k].Indices);
		if (ApplyInset(Region, (bHaveVertexNormals) ? nullptr : &Normals) == false)
		{
			bAllOK = false;
		}
		else
		{
			AllModifiedTriangles.Append(Region.InitialTriangles);
			for (TArray<int32>& RegionTris : Region.StitchTriangles)
			{
				AllModifiedTriangles.Append(RegionTris);
			}
		}
	}

	return bAllOK;


}


bool FInsetMeshRegion::ApplyInset(FInsetInfo& Region, FMeshNormals* UseNormals)
{
	FMeshRegionBoundaryLoops InitialLoops(Mesh, Region.InitialTriangles, false);
	bool bOK = InitialLoops.Compute();
	if (bOK == false)
	{
		return false;
	}

	int32 NumInitialLoops = InitialLoops.GetLoopCount();

	if (ChangeTracker)
	{
		ChangeTracker->SaveTriangles(Region.InitialTriangles, true);
	}

	FDynamicMeshEditor Editor(Mesh);

	TArray<FDynamicMeshEditor::FLoopPairSet> LoopPairs;
	bOK = Editor.DisconnectTriangles(Region.InitialTriangles, LoopPairs, true);
	if (bOK == false)
	{
		return false;
	}

	// make copy of separated submesh for deformation
	// (could we defer this copy until we know we need it?)
	FDynamicSubmesh3 SubmeshCalc(Mesh, Region.InitialTriangles, (int)EMeshComponents::None, false);
	FDynamicMesh3& Submesh = SubmeshCalc.GetSubmesh();
	bool bHaveInteriorVerts = false;
	for (int32 vid : Submesh.VertexIndicesItr())
	{
		if (Submesh.IsBoundaryVertex(vid) == false)
		{
			bHaveInteriorVerts = true;
			break;
		}
	}

	// inset vertices
	for (FDynamicMeshEditor::FLoopPairSet& LoopPair : LoopPairs)
	{
		int32 NumEdges = LoopPair.InnerEdges.Num();
		TArray<FLine3d> InsetLines;
		InsetLines.SetNum(NumEdges);

		for (int32 k = 0; k < NumEdges; ++k)
		{
			const FDynamicMesh3::FEdge EdgeVT = Mesh->GetEdge(LoopPair.InnerEdges[k]);
			FVector3d A = Mesh->GetVertex(EdgeVT.Vert[0]);
			FVector3d B = Mesh->GetVertex(EdgeVT.Vert[1]);
			FVector3d EdgeDir = (A - B).Normalized();
			FVector3d Midpoint = (A + B) * 0.5;
			int32 EdgeTri = EdgeVT.Tri[0];
			FVector3d Normal, Centroid; double Area;
			Mesh->GetTriInfo(EdgeTri, Normal, Area, Centroid);

			FVector3d InsetDir = Normal.Cross(EdgeDir);
			if ((Centroid - Midpoint).Dot(InsetDir) < 0)
			{
				InsetDir = -InsetDir;
			}

			InsetLines[k] = FLine3d(Midpoint + InsetDistance * InsetDir, EdgeDir);
		}

		int32 NumVertices = LoopPair.InnerVertices.Num();
		for (int32 vi = 0; vi < NumVertices; ++vi)
		{
			const FLine3d& PrevLine = InsetLines[(vi == 0) ? (NumEdges-1) : (vi-1)];
			const FLine3d& NextLine = InsetLines[vi];

			if (FMathd::Abs(PrevLine.Direction.Dot(NextLine.Direction)) > 0.999)
			{
				FVector3d CurPos = Mesh->GetVertex(LoopPair.InnerVertices[vi]);
				FVector3d NewPos = NextLine.NearestPoint(CurPos);
				Mesh->SetVertex(LoopPair.InnerVertices[vi], NewPos);
			}
			else
			{
				FDistLine3Line3d Distance(PrevLine, NextLine);
				double DistSqr = Distance.GetSquared();

				FVector3d CurPos = Mesh->GetVertex(LoopPair.InnerVertices[vi]);
				FVector3d NewPos = 0.5 * (Distance.Line1ClosestPoint + Distance.Line2ClosestPoint);
				Mesh->SetVertex(LoopPair.InnerVertices[vi], NewPos);
			}
		}


	};

	// stitch each loop
	Region.BaseLoops.SetNum(NumInitialLoops);
	Region.InsetLoops.SetNum(NumInitialLoops);
	Region.StitchTriangles.SetNum(NumInitialLoops);
	Region.StitchPolygonIDs.SetNum(NumInitialLoops);
	TArray<TArray<FIndex2i>> QuadLoops;
	QuadLoops.Reserve(NumInitialLoops);
	int32 LoopIndex = 0;
	for (FDynamicMeshEditor::FLoopPairSet& LoopPair : LoopPairs)
	{
		TArray<int32>& BaseLoopV = LoopPair.OuterVertices;
		TArray<int32>& InsetLoopV = LoopPair.InnerVertices;
		int32 NumLoopV = BaseLoopV.Num();

		// allocate a new group ID for each pair of input group IDs, and build up list of new group IDs along loop
		TArray<int32> NewGroupIDs;
		TArray<int32> EdgeGroups;
		TMap<TPair<int32, int32>, int32> NewGroupsMap;
		for (int32 k = 0; k < NumLoopV; ++k)
		{
			int32 InsetEdgeID = Mesh->FindEdge(InsetLoopV[k], InsetLoopV[(k + 1) % NumLoopV]);
			int32 InsetGroupID = Mesh->GetTriangleGroup(Mesh->GetEdgeT(InsetEdgeID).A);

			// base edge may not exist if we inset entire region. In that case just use single GroupID
			int32 BaseEdgeID = Mesh->FindEdge(BaseLoopV[k], BaseLoopV[(k + 1) % NumLoopV]);
			int32 BaseGroupID = (BaseEdgeID >= 0) ? Mesh->GetTriangleGroup(Mesh->GetEdgeT(BaseEdgeID).A) : InsetGroupID;

			TPair<int32, int32> GroupPair(FMathd::Min(BaseGroupID, InsetGroupID), FMathd::Max(BaseGroupID, InsetGroupID));
			if (NewGroupsMap.Contains(GroupPair) == false)
			{
				int32 NewGroupID = Mesh->AllocateTriangleGroup();
				NewGroupIDs.Add(NewGroupID);
				NewGroupsMap.Add(GroupPair, NewGroupID);
			}
			EdgeGroups.Add(NewGroupsMap[GroupPair]);
		}

		// stitch the loops
		FDynamicMeshEditResult StitchResult;
		Editor.StitchVertexLoopsMinimal(InsetLoopV, BaseLoopV, StitchResult);

		// set the groups of the new quads along the stitch
		int32 NumNewQuads = StitchResult.NewQuads.Num();
		for (int32 k = 0; k < NumNewQuads; k++)
		{
			Mesh->SetTriangleGroup(StitchResult.NewQuads[k].A, EdgeGroups[k]);
			Mesh->SetTriangleGroup(StitchResult.NewQuads[k].B, EdgeGroups[k]);
		}

		// save the stitch triangles set and associated group IDs
		StitchResult.GetAllTriangles(Region.StitchTriangles[LoopIndex]);
		Region.StitchPolygonIDs[LoopIndex] = NewGroupIDs;

		QuadLoops.Add(MoveTemp(StitchResult.NewQuads));

		Region.BaseLoops[LoopIndex].InitializeFromVertices(Mesh, BaseLoopV);
		Region.InsetLoops[LoopIndex].InitializeFromVertices(Mesh, InsetLoopV);
		LoopIndex++;
	}

	// if we have interior vertices or just want to try to resolve foldovers we
	// do a Laplacian solve using the inset positions determined geometrically
	// as weighted soft constraints.
	if ( (bHaveInteriorVerts || Softness > 0.0) && bSolveRegionInteriors )
	{
		bool bReprojectInset = bReproject;
		bool bReprojectInterior = bReproject;
		bool bSolveBoundary = (Softness > 0.0);

		// Build AABBTree for initial surface so that we can reproject onto it.
		// (conceivably this could be cached during interactive operations, also not
		//  necessary if we are not projecting!)
		FDynamicMesh3 ProjectSurface(Submesh);
		FDynamicMeshAABBTree3 Projection(&ProjectSurface, bReproject);

		// if we are reprojecting, do inset border immediately so that the measurements below
		// use the projected values
		if (bReprojectInset)
		{
			for (const FEdgeLoop& Loop : Region.InsetLoops)
			{
				for (int32 BaseVID : Loop.Vertices)
				{
					int32 SubmeshVID = SubmeshCalc.MapVertexToSubmesh(BaseVID);
					Mesh->SetVertex(BaseVID, Projection.FindNearestPoint(Mesh->GetVertex(BaseVID)));
				}
			}
		}

		// compute area of inserted quad-strip border
		double TotalBorderQuadArea = 0;
		int32 NumLoops = LoopPairs.Num();
		for (int32 li = 0; li < NumLoops; ++li)
		{
			int32 NumQuads = QuadLoops[li].Num();
			for (int32 k = 0; k < NumQuads; k++)
			{
				TotalBorderQuadArea += Mesh->GetTriArea(QuadLoops[li][k].A);
				TotalBorderQuadArea += Mesh->GetTriArea(QuadLoops[li][k].B);
			}
		}

		// Figure how much area chnaged by subtracting area of quad-strip from original area.
		// (quad-strip area seems implausibly high at larger distances, ie becomes larger than initial area. Possibly due to sawtooth-shaped profile
		//  of non-planar quads - measure each quad in planar projection?)
		FVector2d VolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Submesh);
		double InitialArea = VolArea.Y;
		double TargetArea = FMathd::Max(0, InitialArea - TotalBorderQuadArea);
		double AreaRatio = TargetArea / InitialArea;
		double LinearAreaScale = FMathd::Max(0.1, FMathd::Sqrt(AreaRatio));

		// compute deformation
		TUniquePtr<UE::Solvers::IConstrainedLaplacianMeshSolver> Solver = UE::MeshDeformation::ConstructSoftMeshDeformer(Submesh);

		// configure area correction based on scaling parameter
		double AreaCorrectT = FMathd::Clamp(AreaCorrection, 0.0, 1.0);
		LinearAreaScale = (1 - AreaCorrectT) * 1.0 + (AreaCorrectT)*LinearAreaScale;
		Solver->UpdateLaplacianScale(LinearAreaScale);
		
		// Want to convert [0,1] softness parameter to a per-boundary-vertex Weight. 
		// Trying to use Vertex Count and Scaling factor to normalize for scale
		// (really should scale mesh down to consistent size, but this is messy due to mapping back to Mesh)
		// Laplacian scale above also impacts this...and perhaps we should only be counting boundary vertices??
		double UnitScalingMeasure = FMathd::Max(0.01, FMathd::Sqrt(VolArea.Y / 6.0));
		double NonlinearT = FMathd::Pow(Softness, 2.0);
		double ScaledPower = (NonlinearT / 50.0) * (double)Submesh.VertexCount() * UnitScalingMeasure;
		double Weight = (ScaledPower < FMathf::ZeroTolerance) ? 100.0 : (1.0 / ScaledPower);

		// add constraints on all the boundary vertices
		for (const FEdgeLoop& Loop : Region.InsetLoops)
		{
			for (int32 BaseVID : Loop.Vertices)
			{
				int32 SubmeshVID = SubmeshCalc.MapVertexToSubmesh(BaseVID);
				FVector3d CurPosition = Mesh->GetVertex(BaseVID);
				Solver->AddConstraint(SubmeshVID, Weight, CurPosition, bSolveBoundary == false);
			}
		}

		// solve for deformed (and possibly reprojected) positions and update mesh
		TArray<FVector3d> DeformedPositions;
		if (Solver->Deform(DeformedPositions))
		{
			for (int32 SubmeshVID : Submesh.VertexIndicesItr())
			{
				if (bSolveBoundary || Solver->IsConstrained(SubmeshVID) == false)
				{
					int32 BaseVID = SubmeshCalc.MapVertexToBaseMesh(SubmeshVID);

					FVector3d SolvePosition = DeformedPositions[SubmeshVID];
					if (bReprojectInterior)
					{
						SolvePosition = Projection.FindNearestPoint(SolvePosition);
					}

					Mesh->SetVertex(BaseVID, SolvePosition);
				}
			}
		}
	}


	// calculate UVs/etc
	if (Mesh->HasAttributes())
	{
		int32 NumLoops = LoopPairs.Num();
		for ( int32 li = 0; li < NumLoops; ++li)
		{
			FDynamicMeshEditor::FLoopPairSet& LoopPair = LoopPairs[li];
			TArray<int32>& BaseLoopV = LoopPair.OuterVertices;
			TArray<int32>& InsetLoopV = LoopPair.InnerVertices;

			// for each polygon we created in stitch, set UVs and normals
			// TODO copied from FExtrudeMesh, doesn't really make sense in this context...
			float AccumUVTranslation = 0;
			FFrame3d FirstProjectFrame;
			FVector3d FrameUp;

			int32 NumQuads = QuadLoops[li].Num();
			for (int32 k = 0; k < NumQuads; k++)
			{
				FVector3f Normal = Editor.ComputeAndSetQuadNormal( QuadLoops[li][k], true);

				// align axis 0 of projection frame to first edge, then for further edges,
				// rotate around 'up' axis to keep normal aligned and frame horizontal
				FFrame3d ProjectFrame;
				if (k == 0)
				{
					FVector3d FirstEdge = Mesh->GetVertex(BaseLoopV[1]) - Mesh->GetVertex(BaseLoopV[0]);
					FirstEdge.Normalize();
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
					AccumUVTranslation += Mesh->GetVertex(BaseLoopV[k]).Distance(Mesh->GetVertex(BaseLoopV[k - 1]));
				}

				// translate horizontally such that vertical spans are adjacent in UV space (so textures tile/wrap properly)
				float TranslateU = UVScaleFactor * AccumUVTranslation;
				Editor.SetQuadUVsFromProjection(QuadLoops[li][k], ProjectFrame, UVScaleFactor, FVector2f(TranslateU, 0));
			}
		}
	}

	return true;
}


