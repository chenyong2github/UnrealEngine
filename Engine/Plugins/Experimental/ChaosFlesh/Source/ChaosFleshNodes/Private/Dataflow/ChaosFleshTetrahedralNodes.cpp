// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshTetrahedralNodes.h"

#include "Chaos/Deformable/Utilities.h"
#include "ChaosFlesh/FleshCollection.h"
#include "CompGeom/ExactPredicates.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Engine/StaticMesh.h"
#include "Generate/IsosurfaceStuffing.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Spatial/FastWinding.h"

namespace Dataflow
{
	void ChaosFleshTetrahedralNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGenerateTetrahedralCollectionDataflowNodes);
	}
}

// Helper to get the boundary of a tet mesh, useful for debugging / verifying output
TArray<FIntVector3> GetSurfaceTriangles(const TArray<FIntVector4>& Tets)
{
	// Rotate the vector so the first element is the smallest
	auto RotVec = [](const FIntVector3& F) -> FIntVector3
	{
		int32 MinIdx = F.X < F.Y ? (F.X < F.Z ? 0 : 2) : (F.Y < F.Z ? 1 : 2);
		return FIntVector3(F[MinIdx], F[(MinIdx + 1) % 3], F[(MinIdx + 2) % 3]);
	};
	// Reverse the winding while keeping the first element unchanged
	auto RevVec = [](const FIntVector3& F) -> FIntVector3
	{
		return FIntVector3(F.X, F.Z, F.Y);
	};

	TSet<FIntVector3> FacesSet;
	for (int32 TetIdx = 0; TetIdx < Tets.Num(); ++TetIdx)
	{
		FIntVector3 TetF[4];
		Chaos::Utilities::GetTetFaces(Tets[TetIdx], TetF[0], TetF[1], TetF[2], TetF[3], false);
		for (int32 SubIdx = 0; SubIdx < 4; ++SubIdx)
		{
			FIntVector3 Key = RotVec(TetF[SubIdx]);
			if (FacesSet.Contains(Key))
			{
				FacesSet.Remove(Key);
			}
			else
			{
				FacesSet.Add(RevVec(Key));
			}
		}
	}
	return FacesSet.Array();
}

void FGenerateTetrahedralCollectionDataflowNodes::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		TUniquePtr<FFleshCollection> InCollection(GetValue<DataType>(Context, &Collection).NewCopy<FFleshCollection>());

#if WITH_EDITORONLY_DATA
		if (StaticMesh && NumCells>0 && (-.5<= OffsetPercent && OffsetPercent<=0.5))
		{
			// make a mesh description for UE::Geometry tools
			UE::Geometry::FDynamicMesh3 DynamicMesh;
			FMeshDescriptionToDynamicMesh GetSourceMesh;
			bool bUsingHiResSource = StaticMesh->IsHiResMeshDescriptionValid();
			const FMeshDescription* UseSourceMeshDescription =
				(bUsingHiResSource) ? StaticMesh->GetHiResMeshDescription() : StaticMesh->GetMeshDescription(0);
			GetSourceMesh.Convert(UseSourceMeshDescription, DynamicMesh);


			// Tet mesh generation
			UE::Geometry::TIsosurfaceStuffing<double> IsosurfaceStuffing;
			UE::Geometry::FDynamicMeshAABBTree3 Spatial(&DynamicMesh);
			UE::Geometry::TFastWindingTree<UE::Geometry::FDynamicMesh3> FastWinding(&Spatial);
			UE::Geometry::FAxisAlignedBox3d Bounds = Spatial.GetBoundingBox();
			IsosurfaceStuffing.Bounds = FBox(Bounds);
			double CellSize = Bounds.MaxDim() / NumCells;
			IsosurfaceStuffing.CellSize = CellSize;
			IsosurfaceStuffing.IsoValue = .5+OffsetPercent;
			IsosurfaceStuffing.Implicit = [&FastWinding, &Spatial](FVector3d Pos)
			{
				FVector3d Nearest = Spatial.FindNearestPoint(Pos);
				double WindingSign = FastWinding.FastWindingNumber(Pos) - .5;
				return FVector3d::Distance(Nearest, Pos) * FMathd::SignNonZero(WindingSign);
			};

			IsosurfaceStuffing.Generate();
			if (IsosurfaceStuffing.Tets.Num() > 0)
			{
				TArray<FVector> Vertices; Vertices.SetNumUninitialized(IsosurfaceStuffing.Vertices.Num());
				TArray<FIntVector4> Elements; Elements.SetNumUninitialized(IsosurfaceStuffing.Tets.Num());
				TArray<FIntVector3> SurfaceElements = GetSurfaceTriangles(IsosurfaceStuffing.Tets);

				for (int32 Tdx = 0; Tdx < IsosurfaceStuffing.Tets.Num(); ++Tdx)
				{
					Elements[Tdx] = IsosurfaceStuffing.Tets[Tdx];
				}
				for (int32 Vdx = 0; Vdx < IsosurfaceStuffing.Vertices.Num(); ++Vdx)
				{
					Vertices[Vdx] = IsosurfaceStuffing.Vertices[Vdx];
				}

				TUniquePtr<FTetrahedralCollection> TetCollection(FTetrahedralCollection::NewTetrahedralCollection(Vertices, SurfaceElements, Elements));
				InCollection->AppendGeometry(*TetCollection.Get());
			}
		}
#else
		ensureMsgf(false,TEXT("FGenerateTetrahedralCollectionDataflowNodes is a editor only node."));
#endif

		SetValue<DataType>(Context, *(FManagedArrayCollection*)InCollection.Get(), &Collection);
	}
}
