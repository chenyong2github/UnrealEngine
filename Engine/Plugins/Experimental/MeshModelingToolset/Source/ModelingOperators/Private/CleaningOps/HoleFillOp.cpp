// Copyright Epic Games, Inc. All Rights Reserved.

#include "CleaningOps/HoleFillOp.h"
#include "Operations/SimpleHoleFiller.h"
#include "Operations/PlanarHoleFiller.h"
#include "Operations/MinimalHoleFiller.h"
#include "ConstrainedDelaunay2.h"
#include "CompGeom/PolygonTriangulation.h"


void FHoleFillOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh, true, true, true, true);

	if (Loops.Num() == 0)
	{
		return;
	}

	if (Progress->Cancelled())
	{
		return;
	}

	NewTriangles.Reset();

	for (auto& Loop : Loops)
	{
		if (Loop.Edges.Num() == 0) 
		{ 
			continue;
		}

		int32 NewGroupID = ResultMesh->AllocateTriangleGroup();

		// Compute a best-fit plane of the boundary vertices
		TArray<FVector3d> VertexPositions;
		Loop.GetVertices(VertexPositions);
		FVector3d PlaneOrigin;
		FVector3d PlaneNormal;
		PolygonTriangulation::ComputePolygonPlane<double>(VertexPositions, PlaneNormal, PlaneOrigin);
		PlaneNormal *= -1.0;	// Previous function seems to orient the normal opposite to what's expected elsewhere

		// Now fill using the appropriate algorithm
		TUniquePtr<IHoleFiller> Filler;
		TArray<TArray<int>> VertexLoops;

		switch (FillType)
		{
		case EHoleFillOpFillType::TriangleFan:
			Filler = MakeUnique<FSimpleHoleFiller>(ResultMesh.Get(), Loop, FSimpleHoleFiller::EFillType::TriangleFan);
			break;
		case EHoleFillOpFillType::PolygonEarClipping:
			Filler = MakeUnique<FSimpleHoleFiller>(ResultMesh.Get(), Loop, FSimpleHoleFiller::EFillType::PolygonEarClipping);
			break;
		case EHoleFillOpFillType::Planar:
			VertexLoops.Add(Loop.Vertices);
			Filler = MakeUnique<FPlanarHoleFiller>(ResultMesh.Get(),
				&VertexLoops,
				ConstrainedDelaunayTriangulate<double>,
				PlaneOrigin,
				PlaneNormal);
			break;
		case EHoleFillOpFillType::Minimal:
			Filler = MakeUnique<FMinimalHoleFiller>(ResultMesh.Get(), Loop);
			break;
		}

		check(Filler);
		Filler->Fill(NewGroupID);

		// Compute normals and UVs
		if (ResultMesh->HasAttributes())
		{
			FDynamicMeshEditor Editor(ResultMesh.Get());
			Editor.SetTriangleNormals(Filler->NewTriangles, (FVector3f)(PlaneNormal));
			FFrame3d ProjectionFrame(PlaneOrigin, PlaneNormal);
			const int NumLayers = ResultMesh->Attributes()->NumUVLayers();
			for (int UVLayerIdx = 0; UVLayerIdx < NumLayers; ++UVLayerIdx)
			{
				Editor.SetTriangleUVsFromProjection(Filler->NewTriangles,
					ProjectionFrame,
					MeshUVScaleFactor,
					FVector2f::Zero(),
					true,
					UVLayerIdx);
			}
		}

		if (Progress->Cancelled())
		{
			return;
		}

		NewTriangles.Append(Filler->NewTriangles);

	}	// for Loops


}
