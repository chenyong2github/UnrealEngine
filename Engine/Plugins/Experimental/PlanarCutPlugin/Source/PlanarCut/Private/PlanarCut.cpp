// Copyright Epic Games, Inc. All Rights Reserved.
#include "PlanarCut.h"
#include "PlanarCutPlugin.h"

#include "Async/ParallelFor.h"
#include "Spatial/FastWinding.h"
#include "Spatial/PointHashGrid3.h"
#include "Spatial/MeshSpatialSort.h"
#include "Util/IndexUtil.h"
#include "Arrangement2d.h"
#include "MeshAdapter.h"
#include "FrameTypes.h"
#include "Polygon2.h"
#include "CompGeom/PolygonTriangulation.h"

#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"

#include "DisjointSet.h"

#include "DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshAABBTree3.h"
#include "Selections/MeshConnectedComponents.h"
#include "MeshTransforms.h"
#include "Operations/MeshBoolean.h"
#include "Operations/MeshSelfUnion.h"
#include "Operations/MergeCoincidentMeshEdges.h"
#include "MeshBoundaryLoops.h"
#include "QueueRemesher.h"
#include "DynamicVertexAttribute.h"
#include "MeshNormals.h"
#include "MeshTangents.h"
#include "ConstrainedDelaunay2.h"

#include "Engine/EngineTypes.h"
#include "StaticMeshOperations.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "Algo/Rotate.h"

#include "GeometryMeshConversion.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


using namespace UE::PlanarCut;


// logic from FMeshUtility::GenerateGeometryCollectionFromBlastChunk, sets material IDs based on construction pattern that external materials have even IDs and are matched to internal materials at InternalID = ExternalID+1
int32 FInternalSurfaceMaterials::GetDefaultMaterialIDForGeometry(const FGeometryCollection& Collection, int32 GeometryIdx) const
{
	int32 FaceStart = 0;
	int32 FaceEnd = Collection.Indices.Num();
	if (GeometryIdx > -1)
	{
		FaceStart = Collection.FaceStart[GeometryIdx];
		FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
	}

	// find most common non interior material
	TMap<int32, int32> MaterialIDCount;
	int32 MaxCount = 0;
	int32 MostCommonMaterialID = -1;
	const TManagedArray<int32>&  MaterialID = Collection.MaterialID;
	for (int i = FaceStart; i < FaceEnd; ++i)
	{
		int32 CurrID = MaterialID[i];
		int32 &CurrCount = MaterialIDCount.FindOrAdd(CurrID);
		CurrCount++;

		if (CurrCount > MaxCount)
		{
			MaxCount = CurrCount;
			MostCommonMaterialID = CurrID;
		}
	}

	// no face case?
	if (MostCommonMaterialID == -1)
	{
		MostCommonMaterialID = 0;
	}

	// We know that the internal materials are the ones that come right after the surface materials
	// #todo(dmp): formalize the mapping between material and internal material, perhaps on the GC
	// if the most common material is an internal material, then just use this
	int32 InternalMaterialID = MostCommonMaterialID % 2 == 0 ? MostCommonMaterialID + 1 : MostCommonMaterialID;

	return InternalMaterialID;
}

void FInternalSurfaceMaterials::SetUVScaleFromCollection(const FGeometryCollection& Collection, int32 GeometryIdx)
{
	int32 FaceStart = 0;
	int32 FaceEnd = Collection.Indices.Num();
	if (GeometryIdx > -1)
	{
		FaceStart = Collection.FaceStart[GeometryIdx];
		FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
	}
	float UVDistance = 0;
	float WorldDistance = 0;
	for (int32 FaceIdx = FaceStart; FaceIdx < FaceEnd; FaceIdx++)
	{
		const FIntVector& Tri = Collection.Indices[FaceIdx];
		WorldDistance += FVector::Distance(Collection.Vertex[Tri.X], Collection.Vertex[Tri.Y]);
		UVDistance += FVector2D::Distance(Collection.UVs[Tri.X][0], Collection.UVs[Tri.Y][0]);
		WorldDistance += FVector::Distance(Collection.Vertex[Tri.Z], Collection.Vertex[Tri.Y]);
		UVDistance += FVector2D::Distance(Collection.UVs[Tri.Z][0], Collection.UVs[Tri.Y][0]);
		WorldDistance += FVector::Distance(Collection.Vertex[Tri.X], Collection.Vertex[Tri.Z]);
		UVDistance += FVector2D::Distance(Collection.UVs[Tri.X][0], Collection.UVs[Tri.Z][0]);
	}

	if (WorldDistance > 0)
	{
		GlobalUVScale =  UVDistance / WorldDistance;
	}
	if (GlobalUVScale <= 0)
	{
		GlobalUVScale = 1;
	}
}



FPlanarCells::FPlanarCells(const FPlane& P)
{
	NumCells = 2;
	AddPlane(P, 0, 1);
}

FPlanarCells::FPlanarCells(const TArrayView<const FVector> Sites, FVoronoiDiagram& Voronoi)
{
	TArray<FVoronoiCellInfo> VoronoiCells;
	Voronoi.ComputeAllCells(VoronoiCells);

	AssumeConvexCells = true;
	NumCells = VoronoiCells.Num();
	for (int32 CellIdx = 0; CellIdx < NumCells; CellIdx++)
	{
		int32 LocalVertexStart = -1;

		const FVoronoiCellInfo& CellInfo = VoronoiCells[CellIdx];
		int32 CellFaceVertexIndexStart = 0;
		for (int32 CellFaceIdx = 0; CellFaceIdx < CellInfo.Neighbors.Num(); CellFaceIdx++, CellFaceVertexIndexStart += 1 + CellInfo.Faces[CellFaceVertexIndexStart])
		{
			int32 NeighborIdx = CellInfo.Neighbors[CellFaceIdx];
			if (CellIdx < NeighborIdx)  // Filter out faces that we expect to get by symmetry
			{
				continue;
			}

			FVector Normal = CellInfo.Normals[CellFaceIdx];
			if (Normal.IsZero())
			{
				if (NeighborIdx > -1)
				{
					Normal = Sites[NeighborIdx] - Sites[CellIdx];
					bool bNormalizeSucceeded = Normal.Normalize();
					ensureMsgf(bNormalizeSucceeded, TEXT("Voronoi diagram should not have Voronoi sites so close together!"));
				}
				else
				{
					// degenerate face on border; likely almost zero area so hopefully it won't matter if we just don't add it
					continue;
				}
			}
			FPlane P(Normal, FVector::DotProduct(Normal, CellInfo.Vertices[CellInfo.Faces[CellFaceVertexIndexStart + 1]]));
			if (LocalVertexStart < 0)
			{
				LocalVertexStart = PlaneBoundaryVertices.Num();
				PlaneBoundaryVertices.Append(CellInfo.Vertices);
			}
			TArray<int32> PlaneBoundary;
			int32 FaceSize = CellInfo.Faces[CellFaceVertexIndexStart];
			for (int32 i = 0; i < FaceSize; i++)
			{
				int32 CellVertexIdx = CellInfo.Faces[CellFaceVertexIndexStart + 1 + i];
				PlaneBoundary.Add(LocalVertexStart + CellVertexIdx);
			}

			AddPlane(P, CellIdx, NeighborIdx, PlaneBoundary);
		}
	}
}

FPlanarCells::FPlanarCells(const TArrayView<const FBox> Boxes)
{
	AssumeConvexCells = true;
	NumCells = Boxes.Num();
	TArray<FBox> BoxesCopy(Boxes);
	
	for (int32 BoxIdx = 0; BoxIdx < NumCells; BoxIdx++)
	{
		const FBox &Box = Boxes[BoxIdx];
		const FVector &Min = Box.Min;
		const FVector &Max = Box.Max;

		int32 VIdx = PlaneBoundaryVertices.Num();
		PlaneBoundaryVertices.Add(Min);
		PlaneBoundaryVertices.Add(FVector(Max.X, Min.Y, Min.Z));
		PlaneBoundaryVertices.Add(FVector(Max.X, Max.Y, Min.Z));
		PlaneBoundaryVertices.Add(FVector(Min.X, Max.Y, Min.Z));

		PlaneBoundaryVertices.Add(FVector(Min.X, Min.Y, Max.Z));
		PlaneBoundaryVertices.Add(FVector(Max.X, Min.Y, Max.Z));
		PlaneBoundaryVertices.Add(Max);
		PlaneBoundaryVertices.Add(FVector(Min.X, Max.Y, Max.Z));

		AddPlane(FPlane(FVector(0, 0, -1), -Min.Z), BoxIdx, -1, { VIdx + 0, VIdx + 1, VIdx + 2, VIdx + 3 });
		AddPlane(FPlane(FVector(0, 0, 1),	Max.Z), BoxIdx, -1, { VIdx + 4, VIdx + 7, VIdx + 6, VIdx + 5 });
		AddPlane(FPlane(FVector(0, -1, 0), -Min.Y), BoxIdx, -1, { VIdx + 0, VIdx + 4, VIdx + 5, VIdx + 1 });
		AddPlane(FPlane(FVector(0, 1, 0),	Max.Y), BoxIdx, -1, { VIdx + 3, VIdx + 2, VIdx + 6, VIdx + 7 });
		AddPlane(FPlane(FVector(-1, 0, 0), -Min.X), BoxIdx, -1, { VIdx + 0, VIdx + 3, VIdx + 7, VIdx + 4 });
		AddPlane(FPlane(FVector(1, 0, 0),	Max.X), BoxIdx, -1, { VIdx + 1, VIdx + 5, VIdx + 6, VIdx + 2 });
	}
}

FPlanarCells::FPlanarCells(const FBox& Region, const FIntVector& CubesPerAxis)
{
	AssumeConvexCells = true;
	NumCells = CubesPerAxis.X * CubesPerAxis.Y * CubesPerAxis.Z;

	// cube X, Y, Z integer indices to a single cell index
	auto ToIdx = [](const FIntVector &PerAxis, int32 Xi, int32 Yi, int32 Zi)
	{
		if (Xi < 0 || Xi >= PerAxis.X || Yi < 0 || Yi >= PerAxis.Y || Zi < 0 || Zi >= PerAxis.Z)
		{
			return -1;
		}
		else
		{
			return Xi + Yi * (PerAxis.X) + Zi * (PerAxis.X * PerAxis.Y);
		}
	};

	auto ToIdxUnsafe = [](const FIntVector &PerAxis, int32 Xi, int32 Yi, int32 Zi)
	{
		return Xi + Yi * (PerAxis.X) + Zi * (PerAxis.X * PerAxis.Y);
	};

	FIntVector VertsPerAxis = CubesPerAxis + FIntVector(1);
	PlaneBoundaryVertices.SetNum(VertsPerAxis.X * VertsPerAxis.Y * VertsPerAxis.Z);

	FVector Diagonal = Region.Max - Region.Min;
	FVector CellSizes(
		Diagonal.X / CubesPerAxis.X,
		Diagonal.Y / CubesPerAxis.Y,
		Diagonal.Z / CubesPerAxis.Z
	);
	int32 VertIdx = 0;
	for (int32 Zi = 0; Zi < VertsPerAxis.Z; Zi++)
	{
		for (int32 Yi = 0; Yi < VertsPerAxis.Y; Yi++)
		{
			for (int32 Xi = 0; Xi < VertsPerAxis.X; Xi++)
			{
				PlaneBoundaryVertices[VertIdx] = Region.Min + FVector(Xi * CellSizes.X, Yi * CellSizes.Y, Zi * CellSizes.Z);
				ensure(VertIdx == ToIdxUnsafe(VertsPerAxis, Xi, Yi, Zi));
				VertIdx++;
			}
		}
	}
	float Z = Region.Min.Z;
	int32 ZSliceSize = VertsPerAxis.X * VertsPerAxis.Y;
	int32 VIdxOffs[8] = { 0, 1, VertsPerAxis.X + 1, VertsPerAxis.X, ZSliceSize, ZSliceSize + 1, ZSliceSize + VertsPerAxis.X + 1, ZSliceSize + VertsPerAxis.X };
	for (int32 Zi = 0; Zi < CubesPerAxis.Z; Zi++, Z += CellSizes.Z)
	{
		float Y = Region.Min.Y;
		float ZN = Z + CellSizes.Z;
		for (int32 Yi = 0; Yi < CubesPerAxis.Y; Yi++, Y += CellSizes.Y)
		{
			float X = Region.Min.X;
			float YN = Y + CellSizes.Y;
			for (int32 Xi = 0; Xi < CubesPerAxis.X; Xi++, X += CellSizes.X)
			{
				float XN = X + CellSizes.X;
				int VIdx = ToIdxUnsafe(VertsPerAxis, Xi, Yi, Zi);
				int BoxIdx = ToIdxUnsafe(CubesPerAxis, Xi, Yi, Zi);

				AddPlane(FPlane(FVector(0, 0, -1), -Z), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi, Zi-1), { VIdx + VIdxOffs[0], VIdx + VIdxOffs[1], VIdx + VIdxOffs[2], VIdx + VIdxOffs[3] });
				AddPlane(FPlane(FVector(0, 0, 1), ZN), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi, Zi+1), { VIdx + VIdxOffs[4], VIdx + VIdxOffs[7], VIdx + VIdxOffs[6], VIdx + VIdxOffs[5] });
				AddPlane(FPlane(FVector(0, -1, 0), -Y), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi-1, Zi), { VIdx + VIdxOffs[0], VIdx + VIdxOffs[4], VIdx + VIdxOffs[5], VIdx + VIdxOffs[1] });
				AddPlane(FPlane(FVector(0, 1, 0), YN), BoxIdx, ToIdx(CubesPerAxis, Xi, Yi+1, Zi), { VIdx + VIdxOffs[3], VIdx + VIdxOffs[2], VIdx + VIdxOffs[6], VIdx + VIdxOffs[7] });
				AddPlane(FPlane(FVector(-1, 0, 0), -X), BoxIdx, ToIdx(CubesPerAxis, Xi-1, Yi, Zi), { VIdx + VIdxOffs[0], VIdx + VIdxOffs[3], VIdx + VIdxOffs[7], VIdx + VIdxOffs[4] });
				AddPlane(FPlane(FVector(1, 0, 0), XN), BoxIdx, ToIdx(CubesPerAxis, Xi+1, Yi, Zi), { VIdx + VIdxOffs[1], VIdx + VIdxOffs[5], VIdx + VIdxOffs[6], VIdx + VIdxOffs[2] });
			}
		}
	}
}

FPlanarCells::FPlanarCells(const FBox &Region, const TArrayView<const FColor> Image, int32 Width, int32 Height)
{
	const double SimplificationTolerance = 0.0; // TODO: implement simplification and make tolerance a param

	const FColor OutsideColor(0, 0, 0);

	int32 NumPix = Width * Height;
	check(Image.Num() == NumPix);

	// Union Find adapted from PBDRigidClustering.cpp version; customized to pixel grouping
	struct UnionFindInfo
	{
		int32 GroupIdx;
		int32 Size;
	};

	TArray<UnionFindInfo> PixCellUnions; // union find info per pixel
	TArray<int32> PixCells;  // Cell Index per pixel (-1 for OutsideColor pixels)

	PixCellUnions.SetNumUninitialized(NumPix);
	PixCells.SetNumUninitialized(NumPix);
	for (int32 i = 0; i < NumPix; ++i)
	{
		if (Image[i] == OutsideColor)
		{
			PixCellUnions[i].GroupIdx = -1;
			PixCellUnions[i].Size = 0;
			PixCells[i] = -1;
		}
		else
		{
			PixCellUnions[i].GroupIdx = i;
			PixCellUnions[i].Size = 1;
			PixCells[i] = -2;
		}
	}
	auto FindGroup = [&](int Idx) {
		int GroupIdx = Idx;

		int findIters = 0;
		while (PixCellUnions[GroupIdx].GroupIdx != GroupIdx)
		{
			ensure(findIters++ < 10); // if this while loop iterates more than a few times, there's probably a bug in the unionfind
			PixCellUnions[GroupIdx].GroupIdx = PixCellUnions[PixCellUnions[GroupIdx].GroupIdx].GroupIdx;
			GroupIdx = PixCellUnions[GroupIdx].GroupIdx;
		}

		return GroupIdx;
	};
	auto MergeGroup = [&](int A, int B) {
		int GroupA = FindGroup(A);
		int GroupB = FindGroup(B);
		if (GroupA == GroupB)
		{
			return;
		}
		if (PixCellUnions[GroupA].Size > PixCellUnions[GroupB].Size)
		{
			Swap(GroupA, GroupB);
		}
		PixCellUnions[GroupA].GroupIdx = GroupB;
		PixCellUnions[GroupB].Size += PixCellUnions[GroupA].Size;
	};
	// merge non-outside neighbors into groups
	int32 YOffs[4] = { -1, 0, 0, 1 };
	int32 XOffs[4] = { 0, -1, 1, 0 };
	for (int32 Yi = 0; Yi < Height; Yi++)
	{
		for (int32 Xi = 0; Xi < Width; Xi++)
		{
			int32 Pi = Xi + Yi * Width;
			if (PixCells[Pi] == -1) // outside cell
			{
				continue;
			}
			for (int Oi = 0; Oi < 4; Oi++)
			{
				int32 Yn = Yi + YOffs[Oi];
				int32 Xn = Xi + XOffs[Oi];
				int32 Pn = Xn + Yn * Width;
				if (Xn < 0 || Xn >= Width || Yn < 0 || Yn >= Height || PixCells[Pn] == -1) // outside nbr
				{
					continue;
				}
				
				MergeGroup(Pi, Pn);
			}
		}
	}
	// assign cell indices from compacted group IDs
	NumCells = 0;
	for (int32 Pi = 0; Pi < NumPix; Pi++)
	{
		if (PixCells[Pi] == -1)
		{
			continue;
		}
		int32 GroupID = FindGroup(Pi);
		if (PixCells[GroupID] == -2)
		{
			PixCells[GroupID] = NumCells++;
		}
		PixCells[Pi] = PixCells[GroupID];
	}

	// Dimensions of pixel corner data
	int32 CWidth = Width + 1;
	int32 CHeight = Height + 1;
	int32 NumCorners = CWidth * CHeight;
	TArray<int32> CornerIndices;
	CornerIndices.SetNumZeroed(NumCorners);

	TArray<TMap<int32, TArray<int32>>> PerCellBoundaryEdgeArrays;
	TArray<TArray<TArray<int32>>> CellBoundaryCorners;
	PerCellBoundaryEdgeArrays.SetNum(NumCells);
	CellBoundaryCorners.SetNum(NumCells);
	
	int32 COffX1[4] = { 1,0,1,0 };
	int32 COffX0[4] = { 0,0,1,1 };
	int32 COffY1[4] = { 0,0,1,1 };
	int32 COffY0[4] = { 0,1,0,1 };
	for (int32 Yi = 0; Yi < Height; Yi++)
	{
		for (int32 Xi = 0; Xi < Width; Xi++)
		{
			int32 Pi = Xi + Yi * Width;
			int32 Cell = PixCells[Pi];
			if (Cell == -1) // outside cell
			{
				continue;
			}
			for (int Oi = 0; Oi < 4; Oi++)
			{
				int32 Yn = Yi + YOffs[Oi];
				int32 Xn = Xi + XOffs[Oi];
				int32 Pn = Xn + Yn * Width;
				
				// boundary edge found
				if (Xn < 0 || Xn >= Width || Yn < 0 || Yn >= Height || PixCells[Pn] != PixCells[Pi])
				{
					int32 C0 = Xi + COffX0[Oi] + CWidth * (Yi + COffY0[Oi]);
					int32 C1 = Xi + COffX1[Oi] + CWidth * (Yi + COffY1[Oi]);
					TArray<int32> Chain = { C0, C1 };
					int32 Last;
					while (PerCellBoundaryEdgeArrays[Cell].Contains(Last = Chain.Last()))
					{
						Chain.Pop(false);
						Chain.Append(PerCellBoundaryEdgeArrays[Cell][Last]);
						PerCellBoundaryEdgeArrays[Cell].Remove(Last);
					}
					if (Last == C0)
					{
						CellBoundaryCorners[Cell].Add(Chain);
					}
					else
					{
						PerCellBoundaryEdgeArrays[Cell].Add(Chain[0], Chain);
					}
				}
			}
		}
	}

	FVector RegionDiagonal = Region.Max - Region.Min;

	for (int32 CellIdx = 0; CellIdx < NumCells; CellIdx++)
	{
		ensure(CellBoundaryCorners[CellIdx].Num() > 0); // there must not be any regions with no boundary
		ensure(PerCellBoundaryEdgeArrays[CellIdx].Num() == 0); // all boundary edge array should have been consumed and turned to full boundary loops
		ensureMsgf(CellBoundaryCorners[CellIdx].Num() == 1, TEXT("Have not implemented support for regions with holes!"));

		int32 BoundaryStart = PlaneBoundaryVertices.Num();
		const TArray<int32>& Bounds = CellBoundaryCorners[CellIdx][0];
		int32 Dx = 0, Dy = 0;
		auto CornerIdxToPos = [&](int32 CornerID)
		{
			int32 Xi = CornerID % CWidth;
			int32 Yi = CornerID / CWidth;
			return FVector2D(
				Region.Min.X + Xi * RegionDiagonal.X / float(Width),
				Region.Min.Y + Yi * RegionDiagonal.Y / float(Height)
			);
		};
		
		FVector2D LastP = CornerIdxToPos(Bounds[0]);
		int32 NumBoundVerts = 0;
		TArray<int32> FrontBound;
		for (int32 BoundIdx = 1; BoundIdx < Bounds.Num(); BoundIdx++)
		{
			FVector2D NextP = CornerIdxToPos(Bounds[BoundIdx]);
			FVector2D Dir = NextP - LastP;
			Dir.Normalize();
			int BoundSkip = BoundIdx;
			while (++BoundSkip < Bounds.Num())
			{
				FVector2D SkipP = CornerIdxToPos(Bounds[BoundSkip]);
				if (FVector2D::DotProduct(SkipP - NextP, Dir) < 1e-6)
				{
					break;
				}
				NextP = SkipP;
				BoundIdx = BoundSkip;
			}
			PlaneBoundaryVertices.Add(FVector(NextP.X, NextP.Y, Region.Min.Z));
			PlaneBoundaryVertices.Add(FVector(NextP.X, NextP.Y, Region.Max.Z));
			int32 Front = BoundaryStart + NumBoundVerts * 2;
			int32 Back = Front + 1;
			FrontBound.Add(Front);
			if (NumBoundVerts > 0)
			{
				AddPlane(FPlane(PlaneBoundaryVertices.Last(), FVector(Dir.Y, -Dir.X, 0)), CellIdx, -1, {Back, Front, Front - 2, Back - 2});
			}

			NumBoundVerts++;
			LastP = NextP;
		}

		// add the last edge, connecting the start and end
		FVector2D Dir = CornerIdxToPos(Bounds[1]) - LastP;
		Dir.Normalize();
		AddPlane(FPlane(PlaneBoundaryVertices.Last(), FVector(Dir.Y, -Dir.X, 0)), CellIdx, -1, {BoundaryStart+1, BoundaryStart, BoundaryStart+NumBoundVerts*2-2, BoundaryStart+NumBoundVerts*2-1});

		// add the front and back faces
		AddPlane(FPlane(Region.Min, FVector(0, 0, -1)), CellIdx, -1, FrontBound);
		TArray<int32> BackBound; BackBound.SetNum(FrontBound.Num());
		for (int32 Idx = 0, N = BackBound.Num(); Idx < N; Idx++)
		{
			BackBound[Idx] = FrontBound[N - 1 - Idx] + 1;
		}
		AddPlane(FPlane(Region.Max, FVector(0, 0, 1)), CellIdx, -1, BackBound);
	}


	AssumeConvexCells = false; // todo could set this to true if the 2D shape of each image region is convex
}




// Simpler invocation of CutWithPlanarCells w/ reasonable defaults
int32 CutWithPlanarCells(
	FPlanarCells& Cells,
	FGeometryCollection& Source,
	int32 TransformIdx,
	double Grout,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection,
	bool bIncludeOutsideCellInOutput,
	float CheckDistanceAcrossOutsideCellForProximity,
	bool bSetDefaultInternalMaterialsFromCollection
)
{
	TArray<int32> TransformIndices { TransformIdx };
	return CutMultipleWithPlanarCells(Cells, Source, TransformIndices, Grout, CollisionSampleSpacing, TransformCollection, bIncludeOutsideCellInOutput, CheckDistanceAcrossOutsideCellForProximity, bSetDefaultInternalMaterialsFromCollection);
}


int32 CutMultipleWithMultiplePlanes(
	const TArrayView<const FPlane>& Planes,
	FInternalSurfaceMaterials& InternalSurfaceMaterials,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	double Grout,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection,
	bool bSetDefaultInternalMaterialsFromCollection
)
{
	int32 OrigNumGeom = Collection.FaceCount.Num();
	int32 CurNumGeom = OrigNumGeom;

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		InternalSurfaceMaterials.SetUVScaleFromCollection(Collection);
	}

	if (!Collection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Collection.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	FTransform CollectionToWorld = TransformCollection.Get(FTransform::Identity);

	FDynamicMeshCollection MeshCollection(&Collection, TransformIndices, CollectionToWorld);

	int32 NewGeomStartIdx = -1;
	NewGeomStartIdx = MeshCollection.CutWithMultiplePlanes(Planes, Grout, CollisionSampleSpacing, &Collection, InternalSurfaceMaterials, bSetDefaultInternalMaterialsFromCollection);

	Collection.ReindexMaterials();
	return NewGeomStartIdx;
}

// Cut multiple Geometry groups inside a GeometryCollection with PlanarCells, and add each cut cell back to the GeometryCollection as a new child of their source Geometry
int32 CutMultipleWithPlanarCells(
	FPlanarCells& Cells,
	FGeometryCollection& Source,
	const TArrayView<const int32>& TransformIndices,
	double Grout,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection,
	bool bIncludeOutsideCellInOutput,
	float CheckDistanceAcrossOutsideCellForProximity,
	bool bSetDefaultInternalMaterialsFromCollection
)
{
	if (!Source.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Source.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		Cells.InternalSurfaceMaterials.SetUVScaleFromCollection(Source);
	}

	FTransform CollectionToWorld = TransformCollection.Get(FTransform::Identity);

	FDynamicMeshCollection MeshCollection(&Source, TransformIndices, CollectionToWorld);
	double OnePercentExtend = MeshCollection.Bounds.MaxDim() * .01;
	FCellMeshes CellMeshes(Source.NumUVLayers(), Cells, MeshCollection.Bounds, Grout, OnePercentExtend, bIncludeOutsideCellInOutput);

	int32 NewGeomStartIdx = -1;

	NewGeomStartIdx = MeshCollection.CutWithCellMeshes(Cells.InternalSurfaceMaterials, Cells.PlaneCells, CellMeshes, &Source, bSetDefaultInternalMaterialsFromCollection, CollisionSampleSpacing);

	Source.ReindexMaterials();
	return NewGeomStartIdx;
}

int32 CutWithMesh(
	FMeshDescription* CuttingMesh,
	FTransform CuttingMeshTransform,
	FInternalSurfaceMaterials& InternalSurfaceMaterials,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection,
	bool bSetDefaultInternalMaterialsFromCollection
)
{
	int32 NewGeomStartIdx = -1;

	// populate the BaseMesh with a conversion of the input mesh.
	FMeshDescriptionToDynamicMesh Converter;
	FDynamicMesh3 FullMesh; // full-featured conversion of the source mesh
	Converter.Convert(CuttingMesh, FullMesh, true);
	bool bHasInvalidNormals, bHasInvalidTangents;
	FStaticMeshOperations::AreNormalsAndTangentsValid(*CuttingMesh, bHasInvalidNormals, bHasInvalidTangents);
	if (bHasInvalidNormals || bHasInvalidTangents)
	{
		FDynamicMeshAttributeSet& Attribs = *FullMesh.Attributes();
		FDynamicMeshNormalOverlay* NTB[3]{ Attribs.PrimaryNormals(), Attribs.PrimaryTangents(), Attribs.PrimaryBiTangents() };
		if (bHasInvalidNormals)
		{
			FMeshNormals::InitializeOverlayToPerVertexNormals(NTB[0], false);
		}
		FMeshTangentsf Tangents(&FullMesh);
		Tangents.ComputeTriVertexTangents(NTB[0], Attribs.PrimaryUV(), { true, true });
		Tangents.CopyToOverlays(FullMesh);
	}

	FDynamicMesh3 DynamicCuttingMesh; // version of mesh that is split apart at seams to be compatible w/ geometry collection, with corresponding attributes set
	int32 NumUVLayers = Collection.NumUVLayers();
	SetGeometryCollectionAttributes(DynamicCuttingMesh, NumUVLayers);

	// Note: This conversion will likely go away, b/c I plan to switch over to doing the boolean operations on the fuller rep, but the code can be adapted
	//		 to the dynamic mesh -> geometry collection conversion phase, as this same splitting will then need to happen there.
	if (ensure(FullMesh.HasAttributes() && FullMesh.Attributes()->NumUVLayers() >= 1 && FullMesh.Attributes()->NumNormalLayers() == 3))
	{
		if (!ensure(FullMesh.IsCompact()))
		{
			FullMesh.CompactInPlace();
		}
		// Triangles array is 1:1 with the input mesh
		TArray<FIndex3i> Triangles; Triangles.Init(FIndex3i::Invalid(), FullMesh.TriangleCount());
		
		FDynamicMesh3& OutMesh = DynamicCuttingMesh;
		FDynamicMeshAttributeSet& Attribs = *FullMesh.Attributes();
		FDynamicMeshNormalOverlay* NTB[3]{ Attribs.PrimaryNormals(), Attribs.PrimaryTangents(), Attribs.PrimaryBiTangents() };
		FDynamicMeshUVOverlay* UV = Attribs.PrimaryUV();
		TMap<FIndex4i, int> ElIDsToVID;
		int OrigMaxVID = FullMesh.MaxVertexID();
		for (int VID = 0; VID < OrigMaxVID; VID++)
		{
			check(FullMesh.IsVertex(VID));
			FVector3d Pos = FullMesh.GetVertex(VID);

			ElIDsToVID.Reset();
			FullMesh.EnumerateVertexTriangles(VID, [&FullMesh, &Triangles, &OutMesh, &NTB, &UV, &ElIDsToVID, Pos, VID, NumUVLayers](int32 TID)
			{
				FIndex3i InTri = FullMesh.GetTriangle(TID);
				int VOnT = IndexUtil::FindTriIndex(VID, InTri);
				FIndex4i ElIDs(
					NTB[0]->GetTriangle(TID)[VOnT],
					NTB[1]->GetTriangle(TID)[VOnT],
					NTB[2]->GetTriangle(TID)[VOnT],
					UV->GetTriangle(TID)[VOnT]);
				const int* FoundVID = ElIDsToVID.Find(ElIDs);

				FIndex3i& OutTri = Triangles[TID];
				if (FoundVID)
				{
					OutTri[VOnT] = *FoundVID;
				}
				else
				{
					FVector3f Normal = NTB[0]->GetElement(ElIDs.A);
					FVertexInfo Info(Pos, Normal, FVector3f(1, 1, 1));

					int OutVID = OutMesh.AppendVertex(Info);
					OutTri[VOnT] = OutVID;
					AugmentedDynamicMesh::SetTangent(OutMesh, OutVID, Normal, NTB[1]->GetElement(ElIDs.B), NTB[2]->GetElement(ElIDs.C));
					for (int32 UVLayerIdx = 0; UVLayerIdx < NumUVLayers; UVLayerIdx++)
					{
						AugmentedDynamicMesh::SetUV(OutMesh, OutVID, UV->GetElement(ElIDs.D), UVLayerIdx);
					}
					ElIDsToVID.Add(ElIDs, OutVID);
				}
			});
		}

		FDynamicMeshMaterialAttribute* OutMaterialID = OutMesh.Attributes()->GetMaterialID();
		for (int TID = 0; TID < Triangles.Num(); TID++)
		{
			FIndex3i& Tri = Triangles[TID];
			int AddedTID = OutMesh.AppendTriangle(Tri);
			if (ensure(AddedTID > -1))
			{
				OutMaterialID->SetValue(AddedTID, -1); // just use a single negative material ID by convention to indicate internal material
				AugmentedDynamicMesh::SetVisibility(OutMesh, AddedTID, true);
			}
		}
	}

	if (!Collection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		const FManagedArrayCollection::FConstructionParameters GeometryDependency(FGeometryCollection::GeometryGroup);
		Collection.AddAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup, GeometryDependency);
	}

	if (bSetDefaultInternalMaterialsFromCollection)
	{
		InternalSurfaceMaterials.SetUVScaleFromCollection(Collection);
	}

	ensureMsgf(!InternalSurfaceMaterials.NoiseSettings.IsSet(), TEXT("Noise settings not yet supported for mesh-based fracture"));

	FTransform CollectionToWorld = TransformCollection.Get(FTransform::Identity);

	FDynamicMeshCollection MeshCollection(&Collection, TransformIndices, CollectionToWorld);
	FCellMeshes CellMeshes(NumUVLayers, DynamicCuttingMesh, InternalSurfaceMaterials, CuttingMeshTransform);

	TArray<TPair<int32, int32>> CellConnectivity;
	CellConnectivity.Add(TPair<int32, int32>(0, -1)); // there's only one 'inside' cell (0), so all cut surfaces are connecting the 'inside' cell (0) to the 'outside' cell (-1)

	NewGeomStartIdx = MeshCollection.CutWithCellMeshes(InternalSurfaceMaterials, CellConnectivity, CellMeshes, &Collection, bSetDefaultInternalMaterialsFromCollection, CollisionSampleSpacing);

	Collection.ReindexMaterials();
	return NewGeomStartIdx;
}


void RecomputeNormalsAndTangents(bool bOnlyTangents, FGeometryCollection& Collection, const TArrayView<const int32>& TransformIndices,
								 bool bOnlyOddMaterials, const TArrayView<const int32>& WhichMaterials)
{
	FTransform CellsToWorld = FTransform::Identity;

	FDynamicMeshCollection MeshCollection(&Collection, TransformIndices, CellsToWorld, true);

	for (int MeshIdx = 0; MeshIdx < MeshCollection.Meshes.Num(); MeshIdx++)
	{
		FDynamicMesh3& Mesh = MeshCollection.Meshes[MeshIdx].AugMesh;
		AugmentedDynamicMesh::ComputeTangents(Mesh, bOnlyOddMaterials, WhichMaterials, !bOnlyTangents);
	}

	MeshCollection.UpdateAllCollections(Collection);

	Collection.ReindexMaterials();
}



int32 AddCollisionSampleVertices(double CollisionSampleSpacing, FGeometryCollection& Collection, const TArrayView<const int32>& TransformIndices)
{
	FTransform CellsToWorld = FTransform::Identity;

	FDynamicMeshCollection MeshCollection(&Collection, TransformIndices, CellsToWorld);

	MeshCollection.AddCollisionSamples(CollisionSampleSpacing);

	MeshCollection.UpdateAllCollections(Collection);

	Collection.ReindexMaterials();

	// TODO: This function does not create any new bones, so we could change it to not return anything
	return INDEX_NONE;
}


void ConvertToMeshDescription(
	FMeshDescription& MeshOut,
	FTransform& TransformOut,
	bool bCenterPivot,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices
)
{
	FTransform CellsToWorld = FTransform::Identity;
	TransformOut = FTransform::Identity;

	FDynamicMeshCollection MeshCollection(&Collection, TransformIndices, CellsToWorld);
	
	FDynamicMesh3 CombinedMesh;
	SetGeometryCollectionAttributes(CombinedMesh, Collection.NumUVLayers());
	CombinedMesh.Attributes()->EnableTangents();

	int32 NumMeshes = MeshCollection.Meshes.Num();
	for (int32 MeshIdx = 0; MeshIdx < NumMeshes; MeshIdx++)
	{
		FDynamicMesh3& Mesh = MeshCollection.Meshes[MeshIdx].AugMesh;
		const FTransform& ToCollection = MeshCollection.Meshes[MeshIdx].ToCollection;

		// transform from the local geometry to the collection space
		// unless it's just one mesh that we're going to center later anyway
		if (!bCenterPivot || NumMeshes > 1)
		{
			MeshTransforms::ApplyTransform(Mesh, (FTransform3d)ToCollection);
		}

		FMeshNormals::InitializeOverlayToPerVertexNormals(Mesh.Attributes()->PrimaryNormals(), true);
		AugmentedDynamicMesh::InitializeOverlayToPerVertexUVs(Mesh, Collection.NumUVLayers());
		AugmentedDynamicMesh::InitializeOverlayToPerVertexTangents(Mesh);

		FMergeCoincidentMeshEdges EdgeMerge(&Mesh);
		EdgeMerge.Apply();
		
		if (MeshIdx > 0)
		{
			FDynamicMeshEditor MeshAppender(&CombinedMesh);
			FMeshIndexMappings IndexMaps_Unused;
			MeshAppender.AppendMesh(&Mesh, IndexMaps_Unused);
		}
		else
		{
			CombinedMesh = Mesh;
		}
	}

	if (bCenterPivot)
	{
		FAxisAlignedBox3d Bounds = CombinedMesh.GetCachedBounds();
		FVector3d Translate = -Bounds.Center();
		MeshTransforms::Translate(CombinedMesh, Translate);
		TransformOut = FTransform((FVector)-Translate);
	}

	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(&CombinedMesh, MeshOut, true);
}