// Copyright Epic Games, Inc. All Rights Reserved.
#include "FractureAutoUV.h"
#include "PlanarCutPlugin.h"

#include "Async/ParallelFor.h"

#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "Parameterization/UVPacking.h"

#include "Image/ImageOccupancyMap.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

namespace UE { namespace PlanarCut {

/**
 * Shared helper method to set 'active' subset of triangles for a collection, to be considered for texturing
 * @return number of active triangles
 */
int32 SetActiveTriangles(FGeometryCollection* Collection, TArray<bool>& ActiveTrianglesOut,
	bool bActivateInsideTriangles,
	bool bOddMaterialAreInside, TArrayView<int32> WhichMaterialsAreInside)
{
	TSet<int32> InsideMaterials;
	for (int32 MatID : WhichMaterialsAreInside)
	{
		InsideMaterials.Add(MatID);
	}

	ActiveTrianglesOut.Init(false, Collection->Indices.Num());
	int32 NumTriangles = 0;
	for (int TID = 0; TID < Collection->Indices.Num(); TID++)
	{
		if (!Collection->Visible[TID])
		{
			continue;
		}

		int32 MaterialID = Collection->MaterialID[TID];
		if (bOddMaterialAreInside && ((MaterialID % 2) == 0) == bActivateInsideTriangles)
		{
			continue;
		}
		if (WhichMaterialsAreInside.Num() > 0 && (InsideMaterials.Contains(MaterialID) != bActivateInsideTriangles))
		{
			continue;
		}

		ActiveTrianglesOut[TID] = true;
		NumTriangles++;
	}

	return NumTriangles;
}

// Adapter to use geometry collections in the UVPacker, and other GeometricObjects generic mesh processing code
// Note: Adapts the whole geometry collection as a single mesh, with triangles filtered by an ActiveTriangles mask
struct FGeomMesh : public UE::Geometry::FUVPacker::IUVMeshView
{
	FGeometryCollection* Collection;
	const TArrayView<bool> ActiveTriangles;
	int32 NumTriangles;
	TArray<FVector3d> GlobalVertices; // vertices transformed to global space

	// Construct a mesh from a geometry collection and a mask of active triangles
	FGeomMesh(FGeometryCollection* Collection, TArrayView<bool> ActiveTriangles, int32 NumTriangles) 
		: Collection(Collection), ActiveTriangles(ActiveTriangles), NumTriangles(NumTriangles)
	{
		InitVertices();
	}

	// Construct a mesh from an existing FGeomMesh and a mask of active triangles
	FGeomMesh(const FGeomMesh& OtherMesh, TArrayView<bool> ActiveTriangles, int32 NumTriangles)
		: Collection(OtherMesh.Collection), ActiveTriangles(ActiveTriangles), NumTriangles(NumTriangles)
	{
		GlobalVertices = OtherMesh.GlobalVertices;
	}

	void InitVertices()
	{
		// transform all vertices from local to global space
		TArray<FTransform> GlobalTransformArray;
		GeometryCollectionAlgo::GlobalMatrices(Collection->Transform, Collection->Parent, GlobalTransformArray);
		GlobalVertices.SetNum(Collection->Vertex.Num());
		for (int32 Idx = 0; Idx < GlobalVertices.Num(); Idx++)
		{
			GlobalVertices[Idx] = GlobalTransformArray[Collection->BoneMap[Idx]].TransformPosition(Collection->Vertex[Idx]);
		}
	}

	///
	/// The FUVPacker::IUVMeshView interface functions; this is the subset of functionality required for FUVPacker
	/// 

	virtual FIndex3i GetTriangle(int32 TID) const
	{
		return FIndex3i(Collection->Indices[TID]);
	}

	virtual FIndex3i GetUVTriangle(int32 TID) const
	{
		return FIndex3i(Collection->Indices[TID]);
	}

	virtual FVector3d GetVertex(int32 VID) const
	{
		return GlobalVertices[VID];
	}

	virtual FVector2f GetUV(int32 EID) const
	{
		return FVector2f(Collection->UV[EID]);
	}

	virtual void SetUV(int32 EID, FVector2f UVIn)
	{
		FVector2D& UV = Collection->UV[EID];
		UV.X = UVIn.X;
		UV.Y = UVIn.Y;
	}



	///
	/// Interface for templated mesh code, not part of the the IUVMeshView interface
	///
	inline bool IsTriangle(int32 TID) const
	{
		return ActiveTriangles[TID];
	}
	constexpr inline bool IsVertex(int32 VID) const
	{
		return true; // we don't have sparse vertices, so can always return true
	}
	inline int32 MaxTriangleID() const
	{
		return Collection->Indices.Num();
	}
	inline int32 MaxVertexID() const
	{
		return GlobalVertices.Num();
	}
	inline int32 TriangleCount() const
	{
		return NumTriangles;
	}
	inline int32 VertexCount() const
	{
		return GlobalVertices.Num();
	}
	constexpr inline int32 GetShapeTimestamp() const
	{
		return 0;
	}

	inline void GetTriVertices(int TID, FVector3d& V0, FVector3d& V1, FVector3d& V2) const
	{
		FIntVector TriRaw = Collection->Indices[TID];

		V0 = GlobalVertices[TriRaw.X];
		V1 = GlobalVertices[TriRaw.Y];
		V2 = GlobalVertices[TriRaw.Z];
	}
};

/// Like FGeomMesh, but the UVs are given as the vertices rather than accessed separately
/// Used by the texture sampling code
struct FGeomFlatUVMesh
{
	FGeometryCollection* Collection;
	const TArrayView<bool> ActiveTriangles;
	int32 NumTriangles;

	FGeomFlatUVMesh(FGeometryCollection* Collection, TArrayView<bool> ActiveTriangles, int32 NumTriangles)
		: Collection(Collection), ActiveTriangles(ActiveTriangles), NumTriangles(NumTriangles)
	{}

	inline FIndex3i GetTriangle(int32 TID) const
	{
		return FIndex3i(Collection->Indices[TID]);
	}

	inline FVector3d GetVertex(int32 VID) const
	{
		const FVector2D& UV = Collection->UV[VID];
		return FVector3d(UV.X, UV.Y, 0);
	}

	inline bool IsTriangle(int32 TID) const
	{
		return ActiveTriangles[TID];
	}
	constexpr inline bool IsVertex(int32 VID) const
	{
		return true; // we don't have sparse vertices, so can always return true
	}
	inline int32 MaxTriangleID() const
	{
		return Collection->Indices.Num();
	}
	inline int32 MaxVertexID() const
	{
		return Collection->UV.Num();
	}
	inline int32 TriangleCount() const
	{
		return NumTriangles;
	}
	inline int32 VertexCount() const
	{
		return Collection->UV.Num();
	}
	constexpr inline int32 GetShapeTimestamp() const
	{
		return 0;
	}

	inline void GetTriVertices(int TID, FVector3d& V0, FVector3d& V1, FVector3d& V2) const
	{
		FIntVector TriRaw = Collection->Indices[TID];

		V0 = GetVertex(TriRaw.X);
		V1 = GetVertex(TriRaw.Y);
		V2 = GetVertex(TriRaw.Z);
	}
};


bool UVLayout(
	FGeometryCollection& Collection,
	int32 UVRes,
	float GutterSize,
	bool bOnlyOddMaterials,
	TArrayView<int32> WhichMaterials
)
{
	TArray<bool> ActiveTriangles;
	int32 NumActive = SetActiveTriangles(&Collection, ActiveTriangles, true, bOnlyOddMaterials, WhichMaterials);
	FGeomMesh UVMesh(&Collection, ActiveTriangles, NumActive);

	TArray<TArray<int32>> UVIslands;
	UE::UVPacking::CreateUVIslandsFromMeshTopology<FGeomMesh>(UVMesh, UVIslands);

	UE::Geometry::FUVPacker Packer;
	Packer.bAllowFlips = false;
	// StandardPack doesn't support the Packer.GutterSize feature, and always tries to leave a 1 texel gutter.
	// To approximate a larger gutter, we tell it to consider a smaller output resolution --
	//  hoping that e.g. the UVs for a 1 pixel gutter at 256x256 are ~ a 2 pixel gutter at 512x512
	// TODO: If we make StandardPack support the GutterSize parameter, we can use that instead.
	Packer.TextureResolution = UVRes / FMathf::Max(1, GutterSize);
	return Packer.StandardPack(&UVMesh, UVIslands);
}

void TextureInternalSurfaces(
	FGeometryCollection& Collection,
	double MaxDistance,
	int32 GutterSize,
	TImageBuilder<FVector3f>& TextureOut,
	bool bOnlyOddMaterials,
	TArrayView<int32> WhichMaterials
)
{
	TArray<bool> InsideTriangles;
	int32 NumInsideTris = SetActiveTriangles(&Collection, InsideTriangles, true, bOnlyOddMaterials, WhichMaterials);
	FGeomFlatUVMesh UVMesh(&Collection, InsideTriangles, NumInsideTris);

	FImageOccupancyMap OccupancyMap;
	OccupancyMap.GutterSize = GutterSize;
	OccupancyMap.Initialize(TextureOut.GetDimensions());
	OccupancyMap.ComputeFromUVSpaceMesh(UVMesh, [](int32 TriangleID) { return TriangleID; });

	FGeomMesh InsideMesh(&Collection, InsideTriangles, NumInsideTris);

	TArray<bool> OutsideTriangles;
	int32 NumOutsideTris = SetActiveTriangles(&Collection, OutsideTriangles, false, bOnlyOddMaterials, WhichMaterials);
	FGeomMesh OutsideMesh(InsideMesh, OutsideTriangles, NumOutsideTris);
	TMeshAABBTree3<FGeomMesh> OutsideSpatial(&OutsideMesh);

	ParallelFor(OccupancyMap.Dimensions.GetHeight(),
		[&MaxDistance, &TextureOut, &UVMesh, &OccupancyMap, &InsideMesh, &OutsideSpatial](int32 Y)
	{
		for (int32 X = 0; X < OccupancyMap.Dimensions.GetWidth(); X++)
		{
			int64 LinearCoord = OccupancyMap.Dimensions.GetIndex(X, Y);
			if (OccupancyMap.IsInterior(LinearCoord))
			{
				int32 TID = OccupancyMap.TexelQueryTriangle[LinearCoord];
				FVector2f UV = OccupancyMap.TexelQueryUV[LinearCoord];
				// Note this is the slowest way to get barycentric coordinates out of a point and a 2D triangle,
				//   ... but it's the one we currently have that's robust to (and gives good answers on) degenerate triangles
				FDistPoint3Triangle3d DistQuery = TMeshQueries<FGeomFlatUVMesh>::TriangleDistance(UVMesh, TID, FVector3d(UV.X, UV.Y, 0));
				FVector3d Bary = DistQuery.TriangleBaryCoords;
				FTriangle3d Tri;
				InsideMesh.GetTriVertices(TID, Tri.V[0], Tri.V[1], Tri.V[2]);
				FVector3d InsidePoint = Tri.BarycentricPoint(Bary);
				double DistanceSq;
				OutsideSpatial.FindNearestTriangle(InsidePoint, DistanceSq, MaxDistance);
				float PercentDistance = FMathf::Min(1.0f, FMathf::Sqrt(DistanceSq) / (float)MaxDistance);
				ensure(FMath::IsFinite(PercentDistance));

				TextureOut.SetPixel(LinearCoord, FVector3f(PercentDistance, 0, 1));
			}
		}
	});

	for (TTuple<int64, int64>& GutterToInside : OccupancyMap.GutterTexels)
	{
		TextureOut.CopyPixel(GutterToInside.Value, GutterToInside.Key);
	}
}

}} // namespace UE::PlanarCut